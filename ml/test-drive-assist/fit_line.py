import cv2
import numpy as np

#-----------------------------------------------------------------------------------#
def clamp_scale(in0, in1, out0, out1, f):
    if f < in0:
        return out0
    elif f > in1:
        return out1
    else:
        assert(in0 != in1)
        return (out1 - out0) / (in1 - in0) * (f - in0) + out0

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

        Z = np.zeros((num_elems, 2))
        mid_pts = np.zeros((num_elems, 2))
        grads = np.zeros((num_elems, 2))
        seg_grad_mag_max = 0.0

        idx = 0
        for i_elem in range(num_elems):
            e = chain[seg_start + i_elem]

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
        new_line['num_pts'] = l_pts.shape[0]
        new_line['grad_mag_max'] = seg_grad_mag_max
        f_perp_dist_average = f_perp_dist_sum / num_elems
        new_line['perp_dist_avg'] = f_perp_dist_average

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
    if not 'lines' in c:
        return

    fit_lines = c['lines']
    for fl in fit_lines:
        line_dir = fl['line_dir']
        num_pts = fl['num_pts']
        grad_mag = fl['grad_mag_max']

        line_theta = np.arctan2(line_dir[1], line_dir[0], dtype=np.float32)
        c_theta = rate_theta_rad(line_theta) * 1.5

        c_grad_mag = (grad_mag_max_global - grad_mag) / grad_mag_max_global
        c_num_pts = clamp_scale(16, 48, 1.5, 0.0, num_pts)

        perp_dist = fl['perp_dist_avg']
        c_perp_dist = clamp_scale(0.2, 1.0, 0.0, 1.0, perp_dist)

        c_final = c_theta + c_grad_mag + c_num_pts + c_perp_dist

        fl['cost_theta'] = c_theta
        fl['cost_grad_mag'] = c_grad_mag
        fl['cost_num_pts'] = c_num_pts
        fl['cost_perp_dist'] = c_perp_dist
        fl['cost_final'] = c_final

# -----------------------------------------------------------------------------------#
def sort_fit_lines(chains):
    lines = list()

    for c in chains:
        if not 'lines' in c:
            continue
        chain_idx = c['chain_index']
        fit_lines = c['lines']

        l_idx = 0
        for fl in fit_lines:
            lines.append((chain_idx, l_idx, fl['cost_final'], fl['perp_dist_avg']))
            l_idx += 1

    lines.sort(key=lambda s: s[2])

    return lines