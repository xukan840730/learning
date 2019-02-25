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
    assert(len(e0) == 2)
    assert(len(e1) == 2)

    if (e0[0][0] == e0[1][0]):
        assert(abs(e0[0][1] - e0[1][1]) == 1)
    elif (e0[0][1] == e0[1][1]):
        assert(abs(e0[0][0] - e0[1][0]) == 1)
    else:
        assert(False)

    if (e1[0][0] == e1[1][0]):
        assert(abs(e1[0][1] - e1[1][1]) == 1)
    elif (e1[0][1] == e1[1][1]):
        assert(abs(e1[0][0] - e1[1][0]) == 1)
    else:
        assert(False)

    if e0[0] == e1[0] and e0[1] == e1[1]:
        return True

    if e0[0] == e1[1] and e0[1] == e1[0]:
        return True

    return False

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
def edgel_equal(edgel0, edgel1):
    edgel0_edges = edgel0['edge']
    edgel1_edges = edgel1['edge']

    if edge_equal(edgel0_edges[0], edgel1_edges[0]) and edge_equal(edgel0_edges[1], edgel1_edges[1]):
        return True

    if edge_equal(edgel0_edges[0], edgel1_edges[1]) and edge_equal(edgel0_edges[1], edgel1_edges[0]):
        return True

    return False

#-----------------------------------------------------------------------------------#
def find_edgel_in_list(e, list_a):
    for a in list_a:
        if edgel_equal(e, a):
            return True
    return False

#-----------------------------------------------------------------------------------#
def link_edgel(edgels_matx, edgel, shape):
    chain_a = list()
    chain_b = list()

    quad_idx = edgel['quad_idx']

    first_edgel = edgel
    first_edgel['visited'] = True
    chain_a.append(first_edgel)
    chain_b.append(first_edgel)
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
        if next_idx0[0] >= 0 and next_idx0[0] < shape[0] and next_idx0[1] >= 0 and next_idx0[1] < shape[1]:
            next_edgel_list = edgels_matx[next_idx0[0]][next_idx0[1]]
            if len(next_edgel_list) > 0:
                # find next edgel
                next_edgel = None
                if len(next_edgel_list) == 1:
                    next_edgel = next_edgel_list[0]
                elif len(next_edgel_list) == 2:
                    for ne in next_edgel_list:
                        ne_edges = ne['edge']
                        assert (len(ne_edges) == 2)
                        for nee in ne_edges:
                            if edge_equal(nee, e0):
                                next_edgel = ne
                                break
                else:
                    assert(False)

                if next_edgel and next_edgel['visited'] == False:
                    # assert(not find_edgel_in_list(next_edgel, chain_a) and not find_edgel_in_list(next_edgel, chain_b))
                    next_edgel['visited'] = True
                    grad_mag = next_edgel['grad_mag']
                    if grad_mag > grad_mag_max:
                        grad_mag_max = grad_mag
                    new_frontiers.append(next_edgel)
                    chain_a.append(next_edgel)

        if iter_count > 0:
            e1 = edges[1]
            next_idx1 = get_adj_quad(edgel_idx, e1)
            if next_idx1[0] >= 0 and next_idx1[0] < shape[0] and next_idx1[1] >= 0 and next_idx1[1] < shape[1]:
                next_edgel_list = edgels_matx[next_idx1[0]][next_idx1[1]]
                if len(next_edgel_list) > 0:
                    # find next edgel
                    next_edgel = None
                    if len(next_edgel_list) == 1:
                        next_edgel = next_edgel_list[0]
                    elif len(next_edgel_list) == 2:
                        for ne in next_edgel_list:
                            ne_edges = ne['edge']
                            assert(len(ne_edges) == 2)
                            for nee in ne_edges:
                                if edge_equal(nee, e1):
                                    next_edgel = ne
                                    break
                    else:
                        assert (False)

                    if next_edgel and next_edgel['visited'] == False:
                        # assert(not find_edgel_in_list(next_edgel, chain_a) and not find_edgel_in_list(next_edgel, chain_b))
                        next_edgel['visited'] = True
                        grad_mag = next_edgel['grad_mag']
                        if grad_mag > grad_mag_max:
                            grad_mag_max = grad_mag
                        new_frontiers.append(next_edgel)
                        chain_a.append(next_edgel)

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
            if next_idx0[0] >= 0 and next_idx0[0] < shape[0] and next_idx0[1] >= 0 and next_idx0[1] < shape[1]:
                next_edgel_list = edgels_matx[next_idx0[0]][next_idx0[1]]
                if len(next_edgel_list) > 0:
                    # find next edgel
                    next_edgel = None
                    if len(next_edgel_list) == 1:
                        next_edgel = next_edgel_list[0]
                    elif len(next_edgel_list) == 2:
                        for ne in next_edgel_list:
                            ne_edges = ne['edge']
                            assert(len(ne_edges) == 2)
                            for nee in ne_edges:
                                if edge_equal(nee, e1):
                                    next_edgel = ne
                                    break
                    else:
                        assert (False)

                    if next_edgel and next_edgel['visited'] == False:
                        # assert(not find_edgel_in_list(next_edgel, chain_a) and not find_edgel_in_list(next_edgel, chain_b))
                        next_edgel['visited'] = True
                        grad_mag = next_edgel['grad_mag']
                        if grad_mag > grad_mag_max:
                            grad_mag_max = grad_mag
                        new_frontiers.append(next_edgel)
                        chain_b.append(next_edgel)

        e1 = edges[1]
        next_idx1 = get_adj_quad(edgel_idx, e1)
        if next_idx1[0] >= 0 and next_idx1[0] < shape[0] and next_idx1[1] >= 0 and next_idx1[1] < shape[1]:
            next_edgel_list = edgels_matx[next_idx1[0]][next_idx1[1]]
            if len(next_edgel_list) > 0:
                # find next edgel
                next_edgel = None
                if len(next_edgel_list) == 1:
                    next_edgel = next_edgel_list[0]
                elif len(next_edgel_list) == 2:
                    for ne in next_edgel_list:
                        ne_edges = ne['edge']
                        assert (len(ne_edges) == 2)
                        for nee in ne_edges:
                            if edge_equal(nee, e1):
                                next_edgel = ne
                                break
                else:
                    assert (False)

                if next_edgel and next_edgel['visited'] == False:
                    # assert(not find_edgel_in_list(next_edgel, chain_a) and not find_edgel_in_list(next_edgel, chain_b))
                    next_edgel['visited'] = True
                    grad_mag = next_edgel['grad_mag']
                    if grad_mag > grad_mag_max:
                        grad_mag_max = grad_mag
                    new_frontiers.append(next_edgel)
                    chain_b.append(next_edgel)

        frontiers_b = new_frontiers

    final_chain = chain_a.copy()
    final_chain.reverse()
    final_chain.pop(len(final_chain) - 1)
    final_chain.extend(chain_b)
    final_chain.reverse()

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

    grad_hori = np.float32(0.0)
    grad_vert = np.float32(0.0)
    for e in edges:
        edge_pt0 = e[0]
        edge_pt1 = e[1]
        if edge_pt1[1] == edge_pt0[1] + 1:
            assert (edge_pt0[0] == edge_pt1[0])
            grad_hori += np.float32(lapl[edge_pt1] - lapl[edge_pt0])
        elif edge_pt1[0] == edge_pt0[0] + 1:
            assert(edge_pt0[1] == edge_pt1[1])
            grad_vert += np.float32(lapl[edge_pt1] - lapl[edge_pt0])
        elif edge_pt1[1] == edge_pt0[1] - 1:
            assert (edge_pt0[0] == edge_pt1[0])
            grad_hori += np.float32(lapl[edge_pt0] - lapl[edge_pt1])
        elif edge_pt1[0] == edge_pt0[0] - 1:
            assert (edge_pt0[1] == edge_pt1[1])
            grad_vert += np.float32(lapl[edge_pt0] - lapl[edge_pt1])
        else:
            assert (False)

    grad_hori *= np.float32(0.5)
    grad_vert *= np.float32(0.5)
    edgel['grad'] = np.array([grad_vert, grad_hori], dtype=np.float32)
    grad_mag = np.sqrt(grad_hori * grad_hori + grad_vert * grad_vert)
    edgel['grad_mag'] = grad_mag
    theta_rad = np.arctan2(grad_hori, grad_vert, dtype=np.float32)
    theta_deg = theta_rad * np.float32(180) / np.float32(np.pi)
    edgel['theta'] = theta_rad
    edgel['theta_deg'] = theta_deg
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
    end_pts['up'] = np.array([irow, end_pts_hori[end_pt_up]], dtype=np.float32)
    end_pts['rt'] = np.array([end_pts_vert[end_pt_rt], icol + 1], dtype=np.float32)
    end_pts['dw'] = np.array([irow + 1, end_pts_hori[end_pt_dw]], dtype=np.float32)
    end_pts['lt'] = np.array([end_pts_vert[end_pt_lt], icol], dtype=np.float32)

    n_quads = [(irow - 1, icol),
             (irow, icol + 1),
             (irow + 1, icol),
             (irow, icol -1)]

    edges = [((irow, icol), (irow, icol + 1)),
             ((irow, icol + 1), (irow + 1, icol + 1)),
             ((irow + 1, icol + 1), (irow + 1, icol)),
             ((irow + 1, icol), (irow, icol))]

    coords = ['up', 'rt', 'dw', 'lt']

    for idx in range(len(n_quads)):
        n_q = n_quads[idx]
        co = coords[idx]
        nrow = n_q[0]
        ncol = n_q[1]

        if nrow >= 0 and nrow < rows and ncol >= 0 and ncol < cols:
            edgels_neighbor = edgels_matx[nrow][ncol]

            if len(edgels_neighbor) == 1:
                line_neighbors[co] = edgels_neighbor[0]['end_pts']

            elif len(edgels_neighbor) == 2:
                edge = edges[idx]
                n0 = edgels_neighbor[0]
                n1 = edgels_neighbor[1]
                ne0 = n0['edge']
                ne1 = n1['edge']
                if edge_equal(ne0[0], edge) or edge_equal(ne0[1], edge):
                    line_neighbors[co] = n0['end_pts']
                elif edge_equal(ne1[0], edge) or edge_equal(ne1[1], edge):
                    line_neighbors[co] = n1['end_pts']
                else:
                    assert(False)

    return line_neighbors, end_pts

#-----------------------------------------------------------------------------------#
def make_edgel_quad(end_pts, line_neighbors, irow, icol, lapl, end_pts_hori, end_pts_vert):
    line_a_0 = end_pts['rt'] - end_pts['up']
    line_a_1 = end_pts['lt'] - end_pts['dw']
    line_a_0_norm = normalize(line_a_0)
    line_a_1_norm = normalize(line_a_1)

    line_b_0 = end_pts['dw'] - end_pts['rt']
    line_b_1 = end_pts['up'] - end_pts['lt']
    line_b_0_norm = normalize(line_b_0)
    line_b_1_norm = normalize(line_b_1)

    line_up = line_neighbors['up'][1] - line_neighbors['up'][0]
    if line_up[0] > 0:
        line_up = -line_up

    line_rt = line_neighbors['rt'][1] - line_neighbors['rt'][0]
    if line_rt[1] < 0:
        line_rt = -line_rt

    line_dw = line_neighbors['dw'][1] - line_neighbors['dw'][0]
    if line_dw[0] < 0:
        line_dw = -line_dw

    line_lt = line_neighbors['lt'][1] - line_neighbors['lt'][0]
    if line_lt[1] > 0:
        line_lt = -line_lt

    line_up_norm = normalize(line_up)
    line_rt_norm = normalize(line_rt)
    line_dw_norm = normalize(line_dw)
    line_lt_norm = normalize(line_lt)

    test_a = np.dot(line_a_0_norm, line_up_norm)
    test_a += np.dot(-line_a_0_norm, line_rt_norm)
    test_a += np.dot(line_a_1_norm, line_dw_norm)
    test_a += np.dot(-line_a_1_norm, line_lt_norm)

    test_b = np.dot(line_b_0_norm, line_rt_norm)
    test_b += np.dot(-line_b_0_norm, line_dw_norm)
    test_b += np.dot(line_b_1_norm, line_lt_norm)
    test_b += np.dot(-line_b_1_norm, line_up_norm)

    edgel_0 = None
    edgel_1 = None

    if test_a < test_b:
        # choose a
        edge0 = ((irow, icol), (irow, icol + 1))
        edge1 = ((irow, icol + 1), (irow + 1, icol + 1))
        edgel_0 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)

        edge0 = ((irow + 1, icol + 1), (irow + 1, icol))
        edge1 = ((irow + 1, icol), (irow, icol))
        edgel_1 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)

    else:
        # choose b, add 2 edgels
        edge0 = ((irow, icol + 1), (irow + 1, icol + 1))
        edge1 = ((irow + 1, icol + 1), (irow + 1, icol))
        edgel_0 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)

        edge0 = ((irow + 1, icol), (irow, icol))
        edge1 = ((irow, icol), (irow, icol + 1))
        edgel_1 = make_edgel(edge0, edge1, lapl, end_pts_hori, end_pts_vert, irow, icol)

    return edgel_0, edgel_1


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
            edgel_0, edgel_1 = make_edgel_quad(end_pts, line_neighbors, irow, icol, lapl, end_pts_hori, end_pts_vert)
            edgels_matx[irow][icol].append(edgel_0)
            edgels_matx[irow][icol].append(edgel_1)
        else:
            quad_4_pts_remains.append(quad)

    # for quad in quad_4_pts_remains:
    #     irow = quad[0]
    #     icol = quad[1]
    #     line_neighbors, end_pts = get_neighbors(irow, icol, end_pts_hori, end_pts_vert, edgels_matx, rows, cols)
    #     print((irow, icol))
    #     print(len(line_neighbors))
    #     print(len(end_pts))

    # phase 3, process 4 end pts edgels with 3 linked lines.
    for iter in range(10):
        quad_4_pts_new = list()

        for quad in quad_4_pts_remains:
            irow = quad[0]
            icol = quad[1]
            # print((irow, icol))
            line_neighbors, end_pts = get_neighbors(irow, icol, end_pts_hori, end_pts_vert, edgels_matx, rows, cols)

            if len(line_neighbors) >= 3:

                if 'up' in line_neighbors:
                    pass
                else:
                    pt_old = end_pts['up']
                    pt_new = pt_old.copy()
                    pt_new[0] -= 1
                    line_neighbors['up'] = (pt_old, pt_new)

                if 'rt' in line_neighbors:
                    pass
                else:
                    pt_old = end_pts['rt']
                    pt_new = pt_old.copy()
                    pt_new[1] += 1
                    line_neighbors['rt'] = (pt_old, pt_new)

                if 'dw' in line_neighbors:
                    pass
                else:
                    pt_old = end_pts['dw']
                    pt_new = pt_old.copy()
                    pt_new[0] += 1
                    line_neighbors['dw'] = (pt_old, pt_new)

                if 'lt' in line_neighbors:
                    pass
                else:
                    pt_old = end_pts['lt']
                    pt_new = pt_old.copy()
                    pt_new[1] -= 1
                    line_neighbors['lt'] = (pt_old, pt_new)

                edgel_0, edgel_1 = make_edgel_quad(end_pts, line_neighbors, irow, icol, lapl, end_pts_hori, end_pts_vert)
                edgels_matx[irow][icol].append(edgel_0)
                edgels_matx[irow][icol].append(edgel_1)
            else:
                quad_4_pts_new.append((irow, icol))

        if len(quad_4_pts_new) == 0:
            break

        # nothing processed
        if len(quad_4_pts_new) == len(quad_4_pts_remains):
            # print('missing quad:')
            # for quad in quad_4_pts_new:
            #     print(quad)
            break

        quad_4_pts_remains = quad_4_pts_new.copy()

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

    # build linked chain from edgels

    chains = list()
    i_visited = 0
    for irow in range(lapl.shape[0]):
        for icol in range(lapl.shape[1]):
            edgel_list = edgels_matx[irow][icol]
            if len(edgel_list) == 0:
                continue;

            for edgel in edgel_list:
                # skip already visited edgel
                if edgel['visited']:
                    continue

                # print((irow, icol))

                # use 2 list so they can be easily linked together
                new_chain = link_edgel(edgels_matx, edgel, lapl.shape)
                # print(new_chain)
                chains.append(new_chain)

            # update iteration count
            i_visited += 1

    # print(len(chains))

    # dbg_lapl = dbg.debug_laplacian(lapl) * 255.0
    # cv2.imshow('lapl', dbg_lapl)
    dbg_image = dbg.debug_edgels(lapl, edgels_matx, chains, grad_mag_max) * 255.0

    result_image = imutils.resize(dbg_image, width=image_width)
    return result_image.astype(np.uint8)
