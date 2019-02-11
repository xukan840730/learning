import cv2
import numpy as np
import region as rg
import debug as dbg
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
def edge_equal(e0, e1):
    e0_pt0 = e0[0]
    e1_pt0 = e1[0]
    if e0_pt0[0] != e1_pt0[0] or e0_pt0[1] != e1_pt0[1]:
        return False

    e0_pt1 = e0[1]
    e1_pt1 = e1[1]
    if e0_pt1[0] != e1_pt1[0] or e0_pt1[1] != e1_pt1[1]:
        return False

    return True

#-----------------------------------------------------------------------------------#
def get_adj_quad(curr_quad_idx, edge):
    irow = curr_quad_idx[0]
    icol = curr_quad_idx[1]

    test_edge0 = ((irow, icol), (irow, icol + 1))
    if edge_equal(test_edge0, edge):
        return (irow - 1, icol)

    test_edge1 = ((irow, icol + 1), (irow + 1, icol + 1))
    if edge_equal(test_edge1, edge):
        return (irow, icol + 1)

    test_edge2 = ((irow + 1, icol), (irow + 1, icol + 1))
    if edge_equal(test_edge2, edge):
        return (irow + 1, icol)

    test_edge3 = ((irow, icol), (irow + 1, icol))
    if edge_equal(test_edge3, edge):
        return (irow, icol - 1)

    assert(False)

#-----------------------------------------------------------------------------------#
def link_edgel(edgels_dict, e_key, shape):
    chain_a = list()
    chain_b = list()

    first_edgel = edgels_dict[e_key]
    first_edgel['visited'] = True
    chain_a.append(e_key)
    chain_b.append(e_key)
    grad_mag_max = first_edgel['grad_mag']

    frontiers_a = list()
    frontiers_b = list()
    frontiers_a.append(first_edgel)
    frontiers_b.append(first_edgel)

    iter_count = 0
    while len(frontiers_a) > 0:
        new_frontiers = list()

        each_edgel = frontiers_a.pop(0)
        edgel_idx = each_edgel['quad_idx']
        edges = each_edgel['edge']

        e0 = edges[0]
        next_idx0 = get_adj_quad(edgel_idx, e0)
        if next_idx0 in edgels_dict:
            if next_idx0[0] >= 0 and next_idx0[0] < shape[0] and next_idx0[1] >= 0 and next_idx0[1] < shape[1]:
                if (next_idx0 not in chain_a) and (next_idx0 not in chain_b):
                    next_edgel = edgels_dict[next_idx0]
                    next_edgel['visited'] = True
                    grad_mag = next_edgel['grad_mag']
                    if grad_mag > grad_mag_max:
                        grad_mag_max = grad_mag
                    new_frontiers.append(next_edgel)
                    chain_a.append(next_idx0)

        if iter_count > 0:
            e1 = edges[1]
            next_idx1 = get_adj_quad(edgel_idx, e1)
            if next_idx1 in edgels_dict:
                if next_idx1[0] >= 0 and next_idx1[0] < shape[0] and next_idx1[1] >= 0 and next_idx1[1] < shape[1]:
                    if (next_idx1 not in chain_a) and (next_idx1 not in chain_b):
                        next_edgel = edgels_dict[next_idx1]
                        next_edgel['visited'] = True
                        grad_mag = next_edgel['grad_mag']
                        if grad_mag > grad_mag_max:
                            grad_mag_max = grad_mag
                        new_frontiers.append(next_edgel)
                        chain_a.append(next_idx1)

        iter_count += 1
        frontiers_a = new_frontiers

    iter_count = 0
    while len(frontiers_b) > 0:
        new_frontiers = list()

        each_edgel = frontiers_b.pop(0)
        edgel_idx = each_edgel['quad_idx']
        edges = each_edgel['edge']

        if iter_count > 0:
            e0 = edges[0]
            next_idx0 = get_adj_quad(edgel_idx, e0)
            if next_idx0 in edgels_dict:
                if next_idx0[0] >= 0 and next_idx0[0] < shape[0] and next_idx0[1] >= 0 and next_idx0[1] < shape[1]:
                    if (next_idx0 not in chain_a) and (next_idx0 not in chain_b):
                        next_edgel = edgels_dict[next_idx0]
                        next_edgel['visited'] = True
                        grad_mag = next_edgel['grad_mag']
                        if grad_mag > grad_mag_max:
                            grad_mag_max = grad_mag
                        new_frontiers.append(next_edgel)
                        chain_b.append(next_idx0)

        e1 = edges[1]
        next_idx1 = get_adj_quad(edgel_idx, e1)
        if next_idx1 in edgels_dict:
            if next_idx1[0] >= 0 and next_idx1[0] < shape[0] and next_idx1[1] >= 0 and next_idx1[1] < shape[1]:
                if (next_idx1 not in chain_a) and (next_idx1 not in chain_b):
                    next_edgel = edgels_dict[next_idx1]
                    next_edgel['visited'] = True
                    grad_mag = next_edgel['grad_mag']
                    if grad_mag > grad_mag_max:
                        grad_mag_max = grad_mag
                    new_frontiers.append(next_edgel)
                    chain_b.append(next_idx1)

        frontiers_b = new_frontiers

    final_chain = chain_a.copy()
    final_chain.reverse()
    final_chain.pop(len(final_chain) - 1)
    final_chain.extend(chain_b)

    result = {}
    result['chain'] = final_chain
    result['grad_mag_max'] = grad_mag_max

    return result

#-----------------------------------------------------------------------------------#
def laplacian(image_f):
    pyramid_l1 = cv2.pyrDown(image_f)
    l1_expanded = cv2.pyrUp(pyramid_l1)
    lapl = cv2.subtract(image_f, l1_expanded)
    return lapl

#-----------------------------------------------------------------------------------#
def build_end_pts(lapl):
    # build hori edge end points
    end_pts_hori = {}
    for irow in range(lapl.shape[0]):
        for icol in range(lapl.shape[1] - 1):
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
    for icol in range(lapl.shape[1]):
        for irow in range(lapl.shape[0] - 1):
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
def normalize(v):
    norm = np.linalg.norm(v)
    if norm == 0:
       return np.zeros(v.shape)
    return v / norm

#-----------------------------------------------------------------------------------#
def get_end_pt(e, end_pts_hori, end_pts_vert):
    end_pt = np.array([0, 0], dtype=np.float32)

    pt0 = e[0]
    pt1 = e[1]

    if pt0[0] == pt1[0]:
        irow = pt0[0]
        icol = 0
        if pt0[1] < pt1[1]:
            assert(pt0[1] + 1 == pt1[1])
            icol = pt0[1]
        else:
            assert (pt0[1] - 1 == pt1[1])
            icol = pt1[1]
        val = end_pts_hori[(irow, (icol, icol + 1))]
        end_pt = np.array([irow, val], dtype=np.float32)

    elif pt0[1] == pt1[1]:
        irow = 0
        if pt0[0] < pt1[0]:
            assert (pt0[0] + 1 == pt1[0])
            irow = pt0[0]
        else:
            assert (pt0[0] - 1 == pt1[0])
            irow = pt1[0]
        icol = e[0][1]
        val = end_pts_vert[((irow, irow + 1), icol)]
        end_pt = np.array([val, icol], dtype=np.float32)
    else:
        assert(False)

    return end_pt

#-----------------------------------------------------------------------------------#
def make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol):
    end_pt0 = get_end_pt(edge0, end_pts_hori, end_pts_vert)
    end_pt1 = get_end_pt(edge1, end_pts_hori, end_pts_vert)

    edges = [edge0, edge1]

    edgel = {}
    edgel['quad_idx'] = (irow, icol)
    edgel['end_pts'] = (end_pt0, end_pt1)
    edgel['mid_pt'] = (end_pt0 + end_pt1) * 0.5
    edgel['edge'] = edges

    grad_hori = 0.0
    grad_vert = 0.0
    for e in edges:
        edge_pt0 = e[0]
        edge_pt1 = e[1]
        if edge_pt1[1] == edge_pt0[1] + 1:
            grad_hori += lapl[edge_pt1] - lapl[edge_pt0]
        elif edge_pt1[0] == edge_pt0[0] + 1:
            grad_vert += lapl[edge_pt1] - lapl[edge_pt0]
        elif edge_pt1[1] == edge_pt0[1] - 1:
            grad_hori += lapl[edge_pt0] - lapl[edge_pt1]
        elif edge_pt1[0] == edge_pt0[0] - 1:
            grad_vert += lapl[edge_pt0] - lapl[edge_pt1]
        else:
            assert (False)

    grad_hori *= 0.5
    grad_vert *= 0.5
    edgel['grad'] = np.array([grad_hori, grad_vert], dtype=np.float32)
    grad_mag = np.sqrt(grad_hori * grad_hori + grad_vert * grad_vert)
    edgel['grad_mag'] = grad_mag
    edgel['visited'] = False

    return edgel

#-----------------------------------------------------------------------------------#
def get_neighbors(irow, icol, end_pts_hori, end_pts_vert, edgels_matx, rows, cols):

    # clockwise
    end_pt_up = (irow, (icol, icol + 1))
    end_pt_rt = ((irow, irow + 1), icol + 1)
    end_pt_dw = (irow + 1, (icol, icol + 1))
    end_pt_lt = ((irow, irow + 1), icol)
    assert (end_pt_up in end_pts_hori)
    assert (end_pt_rt in end_pts_vert)
    assert (end_pt_dw in end_pts_hori)
    assert (end_pt_lt in end_pts_vert)

    line_neighbors = {}
    end_pts = {}

    if irow > 0:
        edgels_neighbor = edgels_matx[irow - 1][icol]
        if len(edgels_neighbor) > 0:
            assert (len(edgels_neighbor) == 1)
            line_neighbors['up'] = edgels_neighbor[0]['end_pts']
            end_pts['up'] = np.array([irow, end_pts_hori[end_pt_up]], dtype=np.float32)

    if icol < cols - 1:
        edgels_neighbor = edgels_matx[irow][icol + 1]
        if len(edgels_neighbor) > 0:
            assert (len(edgels_neighbor) == 1)
            line_neighbors['rt'] = edgels_neighbor[0]['end_pts']
            end_pts['rt'] = np.array([end_pts_vert[end_pt_rt], icol + 1], dtype=np.float32)

    if irow < rows - 1:
        edgels_neighbor = edgels_matx[irow + 1][icol]
        if len(edgels_neighbor) > 0:
            assert (len(edgels_neighbor) == 1)
            line_neighbors['dw'] = edgels_neighbor[0]['end_pts']
            end_pts['dw'] = np.array([irow + 1, end_pts_hori[end_pt_dw]], dtype=np.float32)

    if icol > 0:
        edgels_neighbor = edgels_matx[irow][icol - 1]
        if len(edgels_neighbor) > 0:
            assert (len(edgels_neighbor) == 1)
            line_neighbors['lt'] = edgels_neighbor[0]['end_pts']
            end_pts['lt'] = np.array([end_pts_vert[end_pt_lt], icol], dtype=np.float32)

    return line_neighbors, end_pts

#-----------------------------------------------------------------------------------#
def build_edgels(lapl, end_pts_hori, end_pts_vert):

    grad_mag_max = 0.0

    rows = lapl.shape[0]
    cols = lapl.shape[1]

    edgels_matx = list()
    for irow in range(rows):
        b = list()
        for icol in range(cols):
            b.append(list())
        edgels_matx.append(b)

    quad_4_pts = list()

    # phase 1: build edgels from 2 end pts.
    for irow in range(rows - 1):
        for icol in range(cols - 1):

            quad_end_pts = list()
            zero_cross_edge = list()

            # clockwise
            e0 = (irow, (icol, icol + 1))
            if e0 in end_pts_hori:
                quad_end_pts.append((irow, end_pts_hori[e0]))
                zero_cross_edge.append(((irow, icol), (irow, icol + 1)))

            e1 = ((irow, irow + 1), icol + 1)
            if e1 in end_pts_vert:
                quad_end_pts.append((end_pts_vert[e1], icol + 1))
                zero_cross_edge.append(((irow, icol + 1), (irow + 1, icol + 1)))

            e2 = (irow + 1, (icol, icol + 1))
            if e2 in end_pts_hori:
                quad_end_pts.append((irow + 1, end_pts_hori[e2]))
                zero_cross_edge.append(((irow + 1, icol), (irow + 1, icol + 1)))

            e3 = ((irow, irow + 1), icol)
            if e3 in end_pts_vert:
                quad_end_pts.append((end_pts_vert[e3], icol))
                zero_cross_edge.append(((irow, icol), (irow + 1, icol)))

            if len(quad_end_pts) == 2:
                edgel = make_edgel(zero_cross_edge[0], zero_cross_edge[1], lapl, end_pts_hori, end_pts_vert, irow, icol)
                grad_mag = edgel['grad_mag']
                if grad_mag > grad_mag_max:
                    grad_mag_max = grad_mag

                edgels_matx[irow][icol].append(edgel)

            elif len(quad_end_pts) == 4:
                quad_4_pts.append((irow, icol))


    # phase 2, process 4 end pts edgels with 4 linked lines.
    quad_4_pts_remains = list()
    for quad in quad_4_pts:
        irow = quad[0]
        icol = quad[1]
        line_neighbors, end_pts = get_neighbors(irow, icol, end_pts_hori, end_pts_vert, edgels_matx, rows, cols)

        if len(line_neighbors) == 4:
            line_a_0 = end_pts['rt'] - end_pts['up']
            line_a_1 = end_pts['lt'] - end_pts['dw']
            line_a_0_norm = normalize(line_a_0)
            line_a_1_norm = normalize(line_a_1)

            line_b_0 = end_pts['dw'] - end_pts['rt']
            line_b_1 = end_pts['up'] - end_pts['lt']
            line_b_0_norm = normalize(line_b_0)
            line_b_1_norm = normalize(line_b_1)

            line_up = line_neighbors['up'][1] - line_neighbors['up'][0]
            line_rt = line_neighbors['rt'][1] - line_neighbors['rt'][0]
            line_dw = line_neighbors['dw'][1] - line_neighbors['dw'][0]
            line_lt = line_neighbors['lt'][1] - line_neighbors['lt'][0]

            line_up_norm = normalize(line_up)
            line_rt_norm = normalize(line_rt)
            line_dw_norm = normalize(line_dw)
            line_lt_norm = normalize(line_lt)

            test_a = abs(np.dot(line_a_0_norm, line_up_norm))
            test_a += abs(np.dot(line_a_0_norm, line_rt_norm))
            test_a += abs(np.dot(line_a_1_norm, line_dw_norm))
            test_a += abs(np.dot(line_a_1_norm, line_lt_norm))

            test_b = abs(np.dot(line_b_0_norm, line_rt_norm))
            test_b += abs(np.dot(line_b_0_norm, line_dw_norm))
            test_b += abs(np.dot(line_b_1_norm, line_lt_norm))
            test_b += abs(np.dot(line_b_1_norm, line_up_norm))

            if test_a > test_b:
                # choose a
                edge0 = ((irow, icol), (irow, icol + 1))
                edge1 = ((irow, icol + 1), (irow + 1, icol + 1))
                edgel_0 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)
                edgels_matx[irow][icol].append(edgel_0)

                edge0 = ((irow + 1, icol + 1), (irow + 1, icol))
                edge1 = ((irow + 1, icol), (irow, icol))
                edgel_1 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)
                edgels_matx[irow][icol].append(edgel_1)

            else:
                # choose b, add 2 edgels
                edge0 = ((irow, icol + 1), (irow + 1, icol + 1))
                edge1 = ((irow + 1, icol + 1), (irow + 1, icol))
                edgel_0 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)
                edgels_matx[irow][icol].append(edgel_0)

                edge0 = ((irow + 1, icol), (irow, icol))
                edge1 = ((irow, icol), (irow, icol + 1))
                edgel_1 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)
                edgels_matx[irow][icol].append(edgel_1)
        else:
            quad_4_pts_remains.append(quad)

    # phase 3, process 4 end pts edgels with 3 linked lines.
    for quad in quad_4_pts_remains:
        irow = quad[0]
        icol = quad[1]
        line_neighbors, end_pts = get_neighbors(irow, icol, end_pts_hori, end_pts_vert, edgels_matx, rows, cols)

        if len(line_neighbors) == 3:
            print((irow, icol))

    return edgels_matx, grad_mag_max

#-----------------------------------------------------------------------------------#
def process_image2(image_u8):
    image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)
    image_height = image_grayscale.shape[0]
    image_width = image_grayscale.shape[1]

    sigma = 1.5
    image_blur_u8 = cv2.GaussianBlur(image_grayscale, (5, 5), sigma)
    image_blur_f = image_blur_u8.astype(np.float32) / 255.0

    small_width = image_width // 16
    small_image_f = imutils.resize(image_blur_f, width=small_width)

    # build laplacian pyramid.
    lapl = laplacian(small_image_f)

    end_pts_hori, end_pts_vert = build_end_pts(lapl)
    edgels_matx, grad_mag_max = build_edgels(lapl, end_pts_hori, end_pts_vert)
    # edgel_keys = edgels_dict.keys()

    # build linked chain from edgels
    # num_edgels = len(edgel_keys)

    chains = list()
    # i_visited = 0
    # for e_key in edgel_keys:
    #     edgel = edgels_dict[e_key]
    #     # skip already visited edgel
    #     if edgel['visited']:
    #         continue
    #
    #     # use 2 list so they can be easily linked together
    #     this_chain = link_edgel(edgels_dict, e_key, lapl.shape)
    #     chains.append(this_chain)
    #
    #     # update iteration count
    #     i_visited += 1

    # print(len(chains))

    # dbg_lapl = dbg.debug_laplacian(lapl) * 255.0
    # cv2.imshow('lapl', dbg_lapl)
    dbg_image = dbg.debug_edgels(lapl, edgels_matx, chains, grad_mag_max) * 255.0

    result_image = imutils.resize(dbg_image, width=image_width)
    return result_image.astype(np.uint8)
