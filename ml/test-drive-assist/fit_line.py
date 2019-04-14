import cv2
import numpy as np
import matplotlib.pyplot as plt
import common

#-----------------------------------------------------------------------------------#
def clamp_scale(in0, in1, out0, out1, f):
    if f < in0:
        return out0
    elif f > in1:
        return out1
    else:
        assert(in0 != in1)
        return (out1 - out0) / (in1 - in0) * (f - in0) + out0

def calc_lines_dist(l0, l1):
    l0_a = l0[0]
    l0_b = l0[1]
    l1_a = l1[0]
    l1_b = l1[1]

    d0_a = 0
    d0_b = 0
    d1_a = 0
    d1_b = 0

    if True:
        l0_norm = common.normalize(l0_b - l0_a)
        d0 = np.dot(l0_b - l0_a, l0_norm)

        l0_a_to_l1_a = l1_a - l0_a
        d0_a = np.dot(l0_a_to_l1_a, l0_norm)
        if d0_a >= 0 and d0_a <= d0:
            return 0.0 # overlapped

        l0_a_to_l1_b = l1_b - l0_a
        d0_b = np.dot(l0_a_to_l1_b, l0_norm)
        if d0_b >= 0 and d0_b <= d0:
            return 0.0 # overlapped

    if True:
        l1_norm = common.normalize(l1_b - l1_a)
        d1 = np.dot(l1_b - l1_a, l1_norm)

        l1_a_to_l0_a = l0_a - l1_a
        d1_a = np.dot(l1_a_to_l0_a, l1_norm)
        if d1_a >= 0 and d1_a <= d1:
            return 0.0 # overlapped

        l1_a_to_l0_b = l0_b - l1_a
        d1_b = np.dot(l1_a_to_l0_b, l1_norm)
        if d1_b >= 0 and d1_b <= d1:
            return 0.0 # overlapped

    return min(abs(d0_a), abs(d0_b), abs(d1_a), abs(d1_b))

#-----------------------------------------------------------------------------------#
def fit_edgels_to_line(edgels):
    num_elems = len(edgels)
    Z = np.zeros((num_elems, 2))
    mid_pts = np.zeros((num_elems, 2))
    grads = np.zeros((num_elems, 2))
    seg_grad_mag_max = 0.0

    idx = 0
    for i_elem in range(num_elems):
        e = edgels[i_elem]

        tangent_0 = e['tang_0']
        tangent_dir = tangent_0[0]
        # proj_dist = tangent_0[1]
        perp_dist = tangent_0[2]
        theta_rad = np.arctan2(tangent_dir[1], tangent_dir[0])

        # print(mid_pt, grad, tangent_dir, theta_rad, perp_dist)

        Z[idx, :] = (theta_rad, perp_dist * 0.1)
        mid_pts[idx, :] = e['mid_pt']
        grads[idx, :] = e['grad']
        if e['grad_mag'] > seg_grad_mag_max:
            seg_grad_mag_max = e['grad_mag']
        idx += 1

    l_pts = mid_pts
    line_fit = cv2.fitLine(l_pts, cv2.DIST_L2, 0, 0.01, 0.01)
    line_dir = np.zeros(2, dtype=np.float32)
    line_dir[0] = line_fit[0]
    line_dir[1] = line_fit[1]
    line_pt = np.zeros(2, dtype=np.float32)
    line_pt[0] = line_fit[2]
    line_pt[1] = line_fit[3]

    f_min = np.float32(10000.0)
    f_max = np.float32(-10000.0)
    f_perp_dist_sum = 0.0
    for i_pts in range(l_pts.shape[0]):
        a = l_pts[i_pts, :] - line_pt
        v = np.dot(a, line_dir)
        if v > f_max:
            f_max = v
        if v < f_min:
            f_min = v
        b = a - line_dir * v
        perp_dist = np.linalg.norm(b)
        f_perp_dist_sum += perp_dist

    line_end_pt_0 = line_pt + line_dir * f_min
    line_end_pt_1 = line_pt + line_dir * f_max

    new_line = {}
    new_line['line_dir'] = line_dir
    new_line['line_pt'] = line_pt
    new_line['end_pts'] = (line_end_pt_0, line_end_pt_1)
    new_line['num_edgels'] = num_elems
    new_line['grad_mag_max'] = seg_grad_mag_max
    f_perp_dist_average = f_perp_dist_sum / num_elems
    new_line['perp_dist_avg'] = f_perp_dist_average

    # perp_dist
    new_line['dist_p'] = np.linalg.norm(-line_pt - (line_dir * np.dot(line_dir, -line_pt)))

    line_theta = np.arctan2(line_dir[1], line_dir[0], dtype=np.float32)
    new_line['theta'] = line_theta

    return new_line

#-----------------------------------------------------------------------------------#
def merge_lines(l0, l1):
    # line 0
    l0_dir = l0['line_dir']
    l0_pt = l0['line_pt']
    l0_theta = np.arctan2(l0_dir[1], l0_dir[0], dtype=np.float32)
    l0_dist_p = np.linalg.norm(-l0_pt - (l0_dir * np.dot(l0_dir, -l0_pt)))
    l0_grad_mag_max = l0['grad_mag_max']
    weight0 = l0_grad_mag_max

    # line 1
    l1_dir = l1['line_dir']
    l1_pt = l1['line_pt']
    l1_theta = np.arctan2(l1_dir[1], l1_dir[0], dtype=np.float32)
    l1_dist_p = np.linalg.norm(-l1_pt - (l1_dir * np.dot(l1_dir, -l1_pt)))
    l1_grad_mag_max = l1['grad_mag_max']
    weight1 = l1_grad_mag_max

    w = weight0 / (weight0 + weight1)

    new_theta = l0_theta + (l1_theta - l0_theta) * w
    new_dist_p = l0_dist_p + (l1_dist_p - l0_dist_p) * w
    new_line_dir = np.zeros(l0_dir.shape)
    new_line_dir[0] = np.cos(new_theta)
    new_line_dir[1] = np.sin(new_theta)

    new_line_pt = np.zeros(l0_pt.shape)
    new_line_pt[0] = -new_line_dir[1]
    new_line_pt[1] = new_line_dir[0]
    new_line_pt *= new_dist_p

    if new_line_pt[0] < 0 or new_line_pt[1] < 0:
        new_line_pt *= -1

    f_min = np.float32(10000.0)
    f_max = np.float32(-10000.0)
    f_perp_dist_sum = 0.0

    l0_end_pt_0 = l0['end_pts'][0]
    l0_end_pt_1 = l0['end_pts'][1]
    l1_end_pt_0 = l1['end_pts'][0]
    l1_end_pt_1 = l1['end_pts'][1]

    l_pts = list()
    l_pts.append(l0_end_pt_0)
    l_pts.append(l0_end_pt_1)
    l_pts.append(l1_end_pt_0)
    l_pts.append(l1_end_pt_1)

    for l_pt in l_pts:
        a = l_pt - new_line_pt
        v = np.dot(a, new_line_dir)
        if v > f_max:
            f_max = v
        if v < f_min:
            f_min = v
        b = a - new_line_dir * v
        perp_dist = np.linalg.norm(b)
        f_perp_dist_sum += perp_dist

    line_end_pt_0 = new_line_pt + new_line_dir * f_min
    line_end_pt_1 = new_line_pt + new_line_dir * f_max

    new_line = {}
    new_line['line_dir'] = new_line_dir
    new_line['line_pt'] = new_line_pt
    new_line['end_pts'] = (line_end_pt_0, line_end_pt_1)
    new_line['grad_mag_max'] = max(l0_grad_mag_max, l1_grad_mag_max)

    l0_num_edgels = l0['num_edgels']
    l1_num_edgels = l1['num_edgels']
    new_num_edgels = l0_num_edgels + l1_num_edgels
    new_line['num_edgels'] = new_num_edgels

    f_perp_dist_average = f_perp_dist_sum / new_num_edgels
    new_line['perp_dist_avg'] = f_perp_dist_average

    # perp_dist
    new_line['dist_p'] = new_dist_p
    new_line['theta'] = new_theta

    return new_line

#-----------------------------------------------------------------------------------#
def chain_fit_lines(c):

    segments = c['segments']
    num_segs = len(segments) - 1
    if num_segs == 0:
        return

    chain = c['chain']

    fit_lines = list()

    for i_seg in range(num_segs):
        seg_start = segments[i_seg]
        seg_end = segments[i_seg + 1]

        num_elems = seg_end - seg_start
        assert(num_elems > 0)

        edgel_list = list()
        for i_elem in range(num_elems):
            e = chain[seg_start + i_elem]
            edgel_list.append(e)

        new_line = fit_edgels_to_line(edgel_list)
        new_line['line_idx'] = len(fit_lines)
        fit_lines.append(new_line)

    c['lines'] = fit_lines

#-----------------------------------------------------------------------------------#
def rate_theta_rad(theta_rad):
    if theta_rad < 0:
        theta_rad += np.pi

    cost = 0.0
    if theta_rad >= 0 and theta_rad < np.pi * 5 / 12:
        cost = clamp_scale(0, np.pi * 5 / 12, 1, 0, theta_rad)
    elif theta_rad >= np.pi * 5 / 12 and theta_rad < np.pi / 2:
        cost = clamp_scale(np.pi * 5 / 12, np.pi / 2, 0, 1, theta_rad)
    elif theta_rad >= np.pi / 2 and theta_rad < np.pi * 7 / 12:
        cost = clamp_scale(np.pi / 2, np.pi * 7 / 12, 1, 0, theta_rad)
    elif theta_rad >= (np.pi * 7 / 12) and theta_rad < np.pi:
        cost = clamp_scale(np.pi * 7 / 12, np.pi, 0, 1, theta_rad)
    elif theta_rad >= np.pi and theta_rad < np.pi * 17 / 12:
        cost = clamp_scale(np.pi, np.pi * 17 / 12, 1, 0, theta_rad)
    elif theta_rad >= np.pi * 17 / 12 and theta_rad < np.pi * 3 / 2:
        cost = clamp_scale(np.pi * 17 / 12, np.pi * 3 / 2, 0, 1, theta_rad)
    elif theta_rad >= np.pi * 3 / 2 and theta_rad < np.pi * 19 / 12:
        cost = clamp_scale(np.pi * 3 / 2, np.pi * 19 / 12, 1, 0, theta_rad)
    else:
        cost = clamp_scale(np.pi * 19 / 12, np.pi * 2, 0, 1, theta_rad)

    return cost


#-----------------------------------------------------------------------------------#
def rate_lines(c, grad_mag_max_global):
    fl = c['line_info']

    line_dir = fl['line_dir']
    num_edgels = fl['num_edgels']
    grad_mag = fl['grad_mag_max']

    c_theta = rate_theta_rad(fl['theta']) * 1.5

    c_grad_mag = (grad_mag_max_global - grad_mag) / grad_mag_max_global
    c_num_pts = clamp_scale(16, 48, 1.5, 0.0, num_edgels)

    perp_dist = fl['perp_dist_avg']
    c_perp_dist = clamp_scale(0.2, 1.0, 0.0, 1.0, perp_dist)

    c_final = c_theta + c_grad_mag + c_num_pts + c_perp_dist

    fl['cost_theta'] = c_theta
    fl['cost_grad_mag'] = c_grad_mag
    fl['cost_num_pts'] = c_num_pts
    fl['cost_perp_dist'] = c_perp_dist
    fl['cost_final'] = c_final

# -----------------------------------------------------------------------------------#
def sort_fit_lines(chains, threshold1, grad_mag_max):
    threshold_num_pts = 5

    # dbg_lines = np.zeros((len(lines), 2))
    #
    # l_idx = 0
    # for l in lines:
    #     theta_rad = l[4]
    #     dist_p = l[5]
    #     dbg_lines[l_idx, :] = (theta_rad, dist_p)
    #     l_idx += 1
    #
    # plt.scatter(dbg_lines[:, 0], dbg_lines[:, 1])
    # plt.show()

    # merge lines
    merged_lines = list()
    for c in chains:
        if not 'lines' in c:
            continue
        chain_idx = c['chain_index']
        fit_lines = c['lines']

        for fl in fit_lines:
            if fl['grad_mag_max'] < threshold1:
                continue

            if fl['num_edgels'] < threshold_num_pts:
                continue

            fl_theta = fl['theta']
            fl_dist_p = fl['dist_p']
            fl_end_pts = fl['end_pts']

            merged = False
            for ml in merged_lines:
                ml_info = ml['line_info']
                ml_theta = ml_info['theta']
                ml_dist_p = ml_info['dist_p']

                is_close = False
                threshold_theta = 10.0 * np.pi / 180
                dist_p_threshold = 8.0
                if abs(ml_theta - fl_theta) < threshold_theta:
                    if abs(ml_dist_p - fl_dist_p) < dist_p_threshold:
                        dist = calc_lines_dist(ml_info['end_pts'], fl_end_pts)
                        if dist < 20:
                            is_close = True

                if is_close:
                    merged_l = merge_lines(ml_info, fl)
                    assert(merged_l)
                    ml['lines'].append((chain_idx, fl['line_idx']))
                    ml['line_info'] = merged_l

                    merged = True
                    break

            # create a new one if not existed
            if not merged:
                new_merged = {}
                new_merged_lines = list()
                new_merged_lines.append((chain_idx, fl['line_idx']))
                new_merged['lines'] = new_merged_lines
                new_merged['line_info'] = fl

                merged_lines.append(new_merged)

    # rating
    for ml in merged_lines:
        rate_lines(ml, grad_mag_max)

    # sorting
    sorted_lines = list()
    for ml in merged_lines:
        fl = ml['line_info']
        sorted_lines.append((ml['lines'], fl['cost_final'], fl['perp_dist_avg'], fl['theta'], fl['dist_p'], fl['end_pts']))

    sorted_lines.sort(key=lambda s: s[1])

    sorted_lines_res = list()
    for sl in sorted_lines:
        new_res = {}
        new_res['lines'] = sl[0] # 'lines'
        new_res['cost_final'] = sl[1] # 'cost_final'
        new_res['end_pts'] = sl[5]  # 'end_pts'
        sorted_lines_res.append(new_res)

    return sorted_lines_res