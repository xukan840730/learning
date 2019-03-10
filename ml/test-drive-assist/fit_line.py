import cv2
import numpy as np

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
        for i_pts in range(l_pts.shape[0]):
            v = np.dot(l_pts[i_pts, :] - line_pt, line_dir)
            if v > f_max:
                f_max = v
            if v < f_min:
                f_min = v

        line_end_pt_0 = line_pt + line_dir * f_min
        line_end_pt_1 = line_pt + line_dir * f_max

        new_line = {}
        new_line['line_dir'] = line_dir
        new_line['line_pt'] = line_pt
        new_line['end_pts'] = (line_end_pt_0, line_end_pt_1)
        new_line['num_pts'] = l_pts.shape[0]
        new_line['grad_mag_max'] = seg_grad_mag_max

        fit_lines.append(new_line)

    c['lines'] = fit_lines

        # convert to np.float32
        # Z = np.float32(Z)

        # define criteria and apply kmeans()
        # criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 10, 1.0)
        # ret, label, center = cv2.kmeans(Z, num_segs, None, criteria, 10, cv2.KMEANS_RANDOM_CENTERS)

    # if num_edgels == 118:
    #     colors = ('r', 'g', 'b', 'y')
    #
    #     for i in chain:
    #         print(i['quad_idx'], i['mid_pt'], i['grad'], i['theta_deg'])
    #
    #     fig = plt.figure()
    #     ax = fig.add_subplot(111, projection='3d')
    #
    #     ax.scatter(Z[:, 0], Z[:, 1], Z[:, 1], c='r', marker='o')
    #     ax.set_xlabel('x axis')
    #     ax.set_ylabel('y axis')
    #     ax.set_zlabel('z axis')
    #
    #     plt.show()
    #
    #     # # Now separate the data, Note the flatten()
    #     # for i in range(num_cluster):
    #     #     A = Z[label.ravel() == i]
    #     #
    #     #     # Plot the data
    #     #     plt.scatter(A[:, 0], A[:, 1], c=colors[i])
    #     #
    #     # # plt.scatter(center[:, 0], center[:, 1], s=80, c='y', marker='s')
    #     # # plt.xlabel('Height'), plt.ylabel('Weight')
    #     # plt.show()
    #
    #     plt.scatter(Z[:, 0], Z[:, 1], c='r')
    #     plt.show()
