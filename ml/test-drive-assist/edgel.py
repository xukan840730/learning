import numpy as np
import common

#-----------------------------------------------------------------------------------#
def normalize(v):
    norm = np.linalg.norm(v)
    if norm == 0:
       return np.zeros(v.shape)
    return v / norm

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
                if common.edge_equal(ne0[0], edge) or common.edge_equal(ne0[1], edge):
                    line_neighbors[co] = n0['end_pts']
                elif common.edge_equal(ne1[0], edge) or common.edge_equal(ne1[1], edge):
                    line_neighbors[co] = n1['end_pts']
                else:
                    assert(False)

    return line_neighbors, end_pts

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
    mid_pt = (end_pt0 + end_pt1) * 0.5
    edgel['mid_pt'] = mid_pt
    edgel['edge'] = edges

    grad_hori = np.float32(0.0)
    grad_vert = np.float32(0.0)

    e0_pt0 = edge0[0]
    e0_pt1 = edge0[1]
    e1_pt0 = edge1[0]
    e1_pt1 = edge1[1]
    if (e0_pt0[0] == e0_pt1[0] and e1_pt0[0] == e1_pt1[0]) or (e0_pt0[1] == e0_pt1[1] and e1_pt0[1] == e1_pt1[1]):
        grad_hori += lapl[(irow, icol + 1)] - lapl[(irow, icol)]
        grad_hori += lapl[(irow + 1, icol + 1)] - lapl[(irow + 1, icol)]
        grad_vert += lapl[(irow + 1, icol)] - lapl[(irow, icol)]
        grad_vert += lapl[(irow + 1, icol + 1)] - lapl[(irow, icol + 1)]

        grad_hori *= np.float32(0.5)
        grad_vert *= np.float32(0.5)
    else:
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

    grad = np.array([grad_vert, grad_hori], dtype=np.float32)
    edgel['grad'] = grad
    edgel['grad_norm'] = normalize(grad)
    grad_mag = np.sqrt(grad_hori * grad_hori + grad_vert * grad_vert)
    edgel['grad_mag'] = grad_mag
    theta_rad = np.arctan2(grad_vert, grad_hori, dtype=np.float32)
    theta_deg = theta_rad * np.float32(180) / np.float32(np.pi)
    theta_rad_abs = theta_rad
    if theta_rad_abs < 0:
        theta_rad_abs += np.float32(np.pi * 2) # line could be represented by 2 directions!
    theta_deg_abs = theta_rad_abs * np.float32(180) / np.float32(np.pi)
    edgel['theta'] = theta_rad
    edgel['theta_deg'] = theta_deg
    edgel['theta_abs'] = theta_rad_abs
    edgel['theta_deg_abs'] = theta_deg_abs

    tangent_dir_0 = normalize(np.array([grad[1], -grad[0]]))

    # center = np.array([lapl.shape[0] / 2, lapl.shape[1] / 2], dtype=np.float32)
    center = np.array([0, 0], dtype=np.float32)

    # -mid_pt == (kOrigin - mid_pt)
    dist_proj_0 = np.dot(tangent_dir_0, center - mid_pt)
    proj_0 = tangent_dir_0 * dist_proj_0
    # tangent_pt_0 = mid_pt + proj_0
    perp_0 = center - mid_pt - proj_0
    dist_perp_0 = np.linalg.norm(perp_0)
    edgel['tang_0'] = (tangent_dir_0, dist_proj_0, dist_perp_0)

    tangent_dir_1 = -tangent_dir_0
    dist_proj_1 = np.dot(tangent_dir_1, center - mid_pt)
    proj_1 = tangent_dir_1 * dist_proj_1
    # tangent_pt_1 = mid_pt + proj_1
    perp_1 = center - mid_pt - proj_1
    dist_perp_1 = np.linalg.norm(perp_1)
    edgel['tang_1'] = (tangent_dir_1, dist_proj_1, dist_perp_1)

    edgel['visited'] = False

    return edgel

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
