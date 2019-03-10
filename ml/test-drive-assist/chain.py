import common
import numpy as np

#-----------------------------------------------------------------------------------#
def get_adj_quad(curr_quad_idx, edge):
    irow = curr_quad_idx[0]
    icol = curr_quad_idx[1]

    test_edge0 = ((irow, icol), (irow, icol + 1))
    if common.edge_equal(test_edge0, edge):
        return (irow - 1, icol)

    test_edge1 = ((irow, icol + 1), (irow + 1, icol + 1))
    if common.edge_equal(test_edge1, edge):
        return (irow, icol + 1)

    test_edge2 = ((irow + 1, icol), (irow + 1, icol + 1))
    if common.edge_equal(test_edge2, edge):
        return (irow + 1, icol)

    test_edge3 = ((irow, icol), (irow + 1, icol))
    if common.edge_equal(test_edge3, edge):
        return (irow, icol - 1)

    assert(False)

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
                            if common.edge_equal(nee, e0):
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
                                if common.edge_equal(nee, e1):
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
                                if common.edge_equal(nee, e1):
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
                            if common.edge_equal(nee, e1):
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

    segments = list()

    seg_end = 0
    segments.append(seg_end) # begin with element 0

    elem_dix = 0
    last_grad = final_chain[0]['grad']

    for e in final_chain:
        if elem_dix != 0:
            curr_grad = e['grad']
            if np.dot(last_grad, curr_grad) < 0.0:
                segments.append(elem_dix)

            last_grad = curr_grad

        elem_dix += 1

    segments.append(len(final_chain))

    result = {}
    result['chain'] = final_chain
    result['grad_mag_max'] = grad_mag_max
    result['segments'] = segments

    return result
