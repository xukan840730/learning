import cv2
import numpy as np
import common
import region as rg
import debug as dbg
import chain
import edgel as el
import fit_line as fl
import imutils
import cProfile, pstats, io

def profile(fnc):
    """ A decorator """

    def inner(*args, **kwargs):
        pr = cProfile.Profile()
        pr.enable()
        retval = fnc(*args, **kwargs)
        pr.disable()
        s = io.StringIO()
        sortby = 'cumulative'
        ps = pstats.Stats(pr, stream=s).sort_stats(sortby)
        ps.print_stats()
        print(s.getvalue())
        return retval

    return inner

# -----------------------------------------------------------------------------------------#
def process_image(image_u8):
    image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)
    image_height = image_grayscale.shape[0]
    image_width = image_grayscale.shape[1]

    image_blur_u8 = cv2.GaussianBlur(image_grayscale, (5, 5), 0)
    image_blur_f = image_blur_u8.astype(np.float32) / 255.0
    # image_b, image_g, image_r = cv2.split(image)

    # create grayscale histogram
    image_blur_u8_flat = image_blur_u8.flatten()

    # dbg.proto_histogram(image_blur_u8_flat)
    # dbg.debug_histogram(image_blur_u8_flat)

    sobel_hori_f = cv2.Sobel(image_blur_f, cv2.CV_64F, 1, 0, ksize=1)
    sobel_vert_f = cv2.Sobel(image_blur_f, cv2.CV_64F, 0, 1, ksize=1)

    threshold_grad = 0.02
    sobel_mag_dbg = dbg.debug_sobel2(sobel_hori_f, sobel_vert_f)
    sobel_mag_dbg2 = (sobel_mag_dbg * 255.0).astype(np.uint8)
    return sobel_mag_dbg2
    # dbg.debug_sobel(image_grayscale, sobel_hori_f, sobel_vert_f, threshold_grad)

    sobel_grad_f = cv2.merge((sobel_hori_f, sobel_vert_f))
    sobel_grad_mag = np.zeros(sobel_hori_f.shape)
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            sobel_grad_mag[ix, iy] = np.linalg.norm(sobel_grad_f[ix, iy])

    sobel_local_max = np.zeros(sobel_hori_f.shape, dtype=float)
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            if ix == 0 or ix == sobel_hori_f.shape[0] -1 or iy == 0 or iy == sobel_hori_f.shape[1] - 1:
                sobel_local_max[ix, iy] = 1.0
            elif sobel_grad_mag[ix, iy] > threshold_grad:
                if (sobel_grad_mag[ix, iy] >= sobel_grad_mag[ix - 1, iy] and sobel_grad_mag[ix, iy] >= sobel_grad_mag[ix + 1, iy]) or \
                    (sobel_grad_mag[ix, iy] >= sobel_grad_mag[ix, iy - 1] and sobel_grad_mag[ix, iy] >= sobel_grad_mag[ix, iy + 1]):
                    sobel_local_max[ix, iy] = 1.0

    sobel_grad_mod = sobel_grad_mag + sobel_local_max

    # sobel_grad_max = np.max(sobel_grad_mag)
    # sobel_grad_norm = sobel_grad_mag / sobel_grad_max
    # sobel_grad_mag = sobel_grad_norm

    image_grad_mag = sobel_grad_mod

    visited_global = np.zeros(image_grayscale.shape, dtype=bool)
    expand_regions = list()

    # skipped_region = np.zeros(visited_global.shape, dtype=bool)

    # get expansions from bottom row
    for ix in range(1):
        # for iy in range(244, 245):
        # for iy in range(0, 1):
        for iy in range(0, image_width):
            starting_pos = (image_height - 2 - ix, iy)
            # starting_pos = (image_height - 170, 225)

            # debug print
            # for idbg in range(0, 6):
            #     dbg_pos = (starting_pos[0] - idbg, starting_pos[1] - 3)
            #     print('(' + str(dbg_pos[0]) + ', ' + str(dbg_pos[1]) + '): ' + str(sobel_grad_f[dbg_pos]))

            # if pixel is in a region, skip to next pixel
            if visited_global[starting_pos]:
                continue;

            # or if pixel has great gradient, skip to next pixel
            grad_mag = image_grad_mag[starting_pos]
            if (grad_mag >= threshold_grad):
                # skipped_region[starting_pos] = True
                # print('skipping: (' + str(starting_pos[0]) + ', ' + str(starting_pos[1]) + ')')
                continue;

            print('start expansion from: (' + str(starting_pos[0]) + ', ' + str(starting_pos[1]) + ')')

            visited_global[starting_pos] = True

            verbose = False
            # if starting_pos[1] == 244:
            #     verbose = True

            new_region = np.zeros(visited_global.shape, dtype=bool)
            rg.expand_v3(starting_pos, visited_global, image_grad_mag, image_blur_f, threshold_grad, new_region, verbose)
            expand_regions.append(new_region)

            # fill global mask with new mask
            visited_global = np.bitwise_or(visited_global, new_region)

    print(len(expand_regions))

    return expand_regions

#-----------------------------------------------------------------------------------#
# def edgel_equal(edgel0, edgel1):
#     edgel0_edges = edgel0['edge']
#     edgel1_edges = edgel1['edge']
#
#     if edge_equal(edgel0_edges[0], edgel1_edges[0]) and edge_equal(edgel0_edges[1], edgel1_edges[1]):
#         return True
#
#     if edge_equal(edgel0_edges[0], edgel1_edges[1]) and edge_equal(edgel0_edges[1], edgel1_edges[0]):
#         return True
#
#     return False

#-----------------------------------------------------------------------------------#
# def find_edgel_in_list(e, list_a):
#     for a in list_a:
#         if edgel_equal(e, a):
#             return True
#     return False

#-----------------------------------------------------------------------------------#
def laplacian(image_f):
    pyramid_l1 = cv2.pyrDown(image_f)
    l1_expanded = cv2.pyrUp(pyramid_l1)
    lapl = cv2.subtract(image_f, l1_expanded)
    return lapl

#-----------------------------------------------------------------------------------#
def build_end_pts(lapl, roi):
    roi_row_0 = roi[0][0]
    roi_row_1 = roi_row_0 + roi[1][0]
    roi_col_0 = roi[0][1]
    roi_col_1 = roi_col_0 + roi[1][1]

    # build hori edge end points
    end_pts_hori = {}
    for irow in range(roi_row_0, roi_row_1):
        for icol in range(roi_col_0, roi_col_1 - 1):
            p0 = (irow, icol)
            p1 = (irow, icol + 1)
            val0 = lapl[p0]
            val1 = lapl[p1]
            bval0 = val0 > 0.0
            bval1 = val1 > 0.0
            if bval0 != bval1:
                end_pt_y = (np.float32(p0[1]) * val1 - np.float32(p1[1]) * val0) / (val1 - val0)
                end_pts_hori[(irow, (icol, icol + 1))] = end_pt_y

    # build vert edge end points
    end_pts_vert = {}
    for icol in range(roi_col_0, roi_col_1):
        for irow in range(roi_row_0, roi_row_1 - 1):
            p0 = (irow, icol)
            p1 = (irow + 1, icol)
            val0 = lapl[p0]
            val1 = lapl[p1]
            bval0 = val0 > 0.0
            bval1 = val1 > 0.0
            if bval0 != bval1:
                end_pt_x = (np.float32(p0[0]) * val1 - np.float32(p1[0]) * val0) / (val1 - val0)
                end_pts_vert[((irow, irow + 1), icol)] = end_pt_x

    return end_pts_hori, end_pts_vert


#-----------------------------------------------------------------------------------#
def process_image2(image_u8):
    image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)
    image_height = image_grayscale.shape[0]
    image_width = image_grayscale.shape[1]

    sigma = 1.5
    image_blur_u8 = cv2.GaussianBlur(image_grayscale, (9, 9), sigma)
    image_blur_f = image_blur_u8.astype(np.float32) / 255.0

    small_width = image_width // 2
    small_image_f = imutils.resize(image_blur_f, width=small_width)

    # build laplacian pyramid.
    lapl = laplacian(small_image_f)
    # dbg_lapl = dbg.debug_laplacian(lapl) * 255.0
    # cv2.imshow('dbg_lapl', dbg_lapl)

    reg_row_0 = int(lapl.shape[0] * 0.40)
    reg_row_1 = int(lapl.shape[0] * 0.65)
    # region of interest
    roi = ((reg_row_0, 0), (reg_row_1 - reg_row_0, lapl.shape[1]))

    end_pts_hori, end_pts_vert = build_end_pts(lapl, roi)
    edgels_matx, grad_mag_max = el.build_edgels(lapl, end_pts_hori, end_pts_vert)

    # build linked chain from edgels

    chains = list()
    i_visited = 0
    for irow in range(lapl.shape[0]):
        for icol in range(lapl.shape[1]):
            edgel_list = edgels_matx[irow][icol]
            if len(edgel_list) == 0:
                continue

            for edgel in edgel_list:
                # skip already visited edgel
                if edgel['visited']:
                    continue

                # print((irow, icol))

                # use 2 list so they can be easily linked together
                new_chain = chain.link_edgel(edgels_matx, edgel, lapl.shape)
                new_chain['chain_index'] = len(chains)
                # print(new_chain)
                chains.append(new_chain)

            # update iteration count
            i_visited += 1

    # print(len(chains))

    threshold = grad_mag_max / 10.0

    for c in chains:
        chain_grad_mag = c['grad_mag_max']
        if chain_grad_mag > threshold:
            fl.chain_fit_lines(c)
            fl.rate_lines(c, grad_mag_max)

    sorted_lines = fl.sort_fit_lines(chains)

    # for sl in sorted_lines:
    #     print(sl)

    # dbg_image = dbg.debug_edgels(lapl, chains, threshold) * 255.0
    dbg_image = dbg.debug_sorted_lines(lapl, chains, sorted_lines) * 255.0

    result_image = imutils.resize(dbg_image, width=image_width)
    return result_image.astype(np.uint8)
