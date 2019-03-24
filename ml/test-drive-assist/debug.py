import cv2
import numpy as np
import matplotlib.pyplot as plt


def debug_sobel(image_gray, sobel_hori_f, sobel_vert_f, threshold):
    sobel_hori_b = np.zeros(sobel_hori_f.shape)
    sobel_hori_g = sobel_hori_b.copy()
    sobel_hori_r = sobel_hori_b.copy()

    sobel_vert_b = np.zeros(sobel_vert_f.shape)
    sobel_vert_g = sobel_vert_b.copy()
    sobel_vert_r = sobel_vert_b.copy()

    sobel_mag_b = np.zeros(sobel_hori_f.shape)
    sobel_mag_g = sobel_mag_b.copy()
    sobel_mag_r = sobel_mag_b.copy()

    sobel_mag_f = np.sqrt(sobel_hori_f * sobel_hori_f + sobel_vert_f * sobel_vert_f)
    sobel_mag_max = np.max(sobel_mag_f)
    sobel_mag_norm = sobel_mag_f / sobel_mag_max

    sobel_local_max = np.zeros(sobel_hori_f.shape)
    sobel_local_max_b = np.zeros(sobel_hori_f.shape)
    sobel_local_max_g = sobel_local_max_b.copy()
    sobel_local_max_r = sobel_local_max_b.copy()

    histo_bins_b = np.zeros(sobel_hori_f.shape)
    histo_bins_g = histo_bins_b.copy()
    histo_bins_r = histo_bins_b.copy()

    # horizontal fill content
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            if sobel_hori_f[ix, iy] > threshold:
                sobel_hori_g[ix, iy] = 1.0
            elif sobel_hori_f[ix, iy] < -threshold:
                sobel_hori_b[ix, iy] = 1.0

    # vertical fill content
    for ix in range(sobel_vert_f.shape[0]):
        for iy in range(sobel_vert_f.shape[1]):
            if sobel_vert_f[ix, iy] > threshold:
                sobel_vert_g[ix, iy] = 1.0
            elif sobel_vert_f[ix, iy] < -threshold:
                sobel_vert_b[ix, iy] = 1.0

    # magnitude fill content
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            sobel_mag_r[ix, iy] = sobel_mag_norm[ix, iy]

    # fill local maximum
    for ix in range(1, sobel_hori_f.shape[0] - 1):
        for iy in range(1, sobel_hori_f.shape[1] - 1):
            if sobel_mag_f[ix, iy] > threshold:
                if (sobel_mag_f[ix, iy] >= sobel_mag_f[ix - 1, iy] and sobel_mag_f[ix, iy] >= sobel_mag_f[ix + 1, iy]) or \
                    (sobel_mag_f[ix, iy] >= sobel_mag_f[ix, iy - 1] and sobel_mag_f[ix, iy] >= sobel_mag_f[ix, iy + 1]):
                    sobel_local_max[ix, iy] = 1.0
                    sobel_local_max_r[ix, iy] = 1.0

    # fill histogram bins color
    # for ix in range(sobel_hori_f.shape[0]):
    #     for iy in range(sobel_hori_f.shape[1]):
    #         k = image_histo_bins[ix, iy]
    #         m = k % 4
    #         if m == 0:
    #             histo_bins_g[ix, iy] = 1.0
    #         elif m == 1:
    #             histo_bins_b[ix, iy] = 1.0
    #         elif m == 2:
    #             histo_bins_r[ix, iy] = 1.0
    #         elif m == 3:
    #             histo_bins_r[ix, iy] = 1.0
    #             histo_bins_g[ix, iy] = 1.0
    #         elif m == 4:
    #             histo_bins_r[ix, iy] = 1.0
    #             histo_bins_b[ix, iy] = 1.0
    #         elif m == 5:
    #             histo_bins_g[ix, iy] = 1.0
    #             histo_bins_b[ix, iy] = 1.0
    #         elif m == 6:
    #             histo_bins_g[ix, iy] = 1.0
    #             histo_bins_b[ix, iy] = 1.0
    #             histo_bins_r[ix, iy] = 1.0

    sobel_mag_mod = sobel_mag_f + sobel_local_max
    sobel_mag_mod_max = np.max(sobel_mag_mod)
    sobel_mag_mod_norm = sobel_mag_mod / sobel_mag_mod_max

    sobel_mag_mod_b = np.zeros(sobel_hori_f.shape)
    sobel_mag_mod_g = sobel_mag_b.copy()
    sobel_mag_mod_r = sobel_mag_b.copy()

    # magnitude fill content
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            sobel_mag_mod_r[ix, iy] = sobel_mag_mod_norm[ix, iy]

    sobel_hori_dbg = cv2.merge((sobel_hori_b, sobel_hori_g, sobel_hori_r))
    sobel_vert_dbg = cv2.merge((sobel_vert_b, sobel_vert_g, sobel_vert_r))
    sobel_mag_dbg = cv2.merge((sobel_mag_b, sobel_mag_g, sobel_mag_r))
    sobel_mag_mod_dbg = cv2.merge((sobel_mag_mod_b, sobel_mag_mod_g, sobel_mag_mod_r))
    sobel_local_max_dbg = cv2.merge((sobel_local_max_b, sobel_local_max_g, sobel_local_max_r))
    # image_histo_dbg = cv2.merge((histo_bins_b, histo_bins_g, histo_bins_r))

    image_gray_3_u8 = cv2.cvtColor(image_gray, cv2.COLOR_GRAY2BGR)
    image_gray_3_f = image_gray_3_u8.astype(np.float32) / 255.0

    sobel_hori_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_hori_dbg, 0.5, 0.0)
    sobel_vert_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_vert_dbg, 0.5, 0.0)
    sobel_mag_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_mag_dbg, 1.0, 0.0)
    # image_histo_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, image_histo_dbg, 0.5, 0.0)

    cv2.imshow("sobel_hori_dbg", sobel_hori_dbg2)
    cv2.imshow("sobel_vert_dbg", sobel_vert_dbg2)
    cv2.imshow("sobel_mag_dbg", sobel_mag_dbg)
    cv2.imshow("sobel_local_max", sobel_local_max_dbg)
    cv2.imshow("sobel_mag_mod_dbg", sobel_mag_mod_dbg)
    # cv2.imshow("image_histo_dbg", image_histo_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()

def debug_sobel2(sobel_hori_f, sobel_vert_f):
    sobel_mag_b = np.zeros(sobel_hori_f.shape)
    sobel_mag_g = sobel_mag_b.copy()
    sobel_mag_r = sobel_mag_b.copy()

    sobel_mag_f = np.sqrt(sobel_hori_f * sobel_hori_f + sobel_vert_f * sobel_vert_f)
    sobel_mag_max = np.max(sobel_mag_f)
    sobel_mag_norm = sobel_mag_f / sobel_mag_max

    # magnitude fill content
    for ix in range(sobel_hori_f.shape[0]):
        for iy in range(sobel_hori_f.shape[1]):
            sobel_mag_g[ix, iy] = sobel_mag_norm[ix, iy]

    sobel_mag_dbg = cv2.merge((sobel_mag_b, sobel_mag_g, sobel_mag_r))
    return sobel_mag_dbg

def debug_expansion(image_gray, visited_mask, b, g, r):
    channel_b = np.zeros(image_gray.shape)
    channel_g = channel_b.copy()
    channel_r = channel_b.copy()

    count = 0
    for ix in range(image_gray.shape[0]):
        for iy in range(image_gray.shape[1]):
            if visited_mask[ix, iy]:
                channel_b[ix, iy] = b
                channel_g[ix, iy] = g
                channel_r[ix, iy] = r

                if count == 0:
                    print('region first pixel: (' + str(ix) + ', ' + str(iy) + ')')

                count = count + 1

    dbg = cv2.merge((channel_b, channel_g, channel_r))

    image_gray_3_u8 = cv2.cvtColor(image_gray, cv2.COLOR_GRAY2BGR)
    image_gray_3_f = image_gray_3_u8.astype(np.float32) / 255.0

    image_dbg = cv2.addWeighted(image_gray_3_f, 1.0, dbg, 0.5, 0.0)

    cv2.imshow("expansion_test", image_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()

def debug_histogram(image_gray_u8):
    colors_per_bin = 16
    assert(256 % colors_per_bin == 0)
    num_bins = 256 // colors_per_bin

    image_gray_hist = np.zeros((num_bins), dtype=int)
    for ix in range(image_gray_u8.shape[0]):
        k = image_gray_u8[ix] // colors_per_bin
        image_gray_hist[k] += 1

    # print(np.std(image_gray_hist))

    plt.hist(image_gray_u8, num_bins)
    plt.show()

def debug_laplacian(laplacian):
    dbg_b = np.zeros(laplacian.shape)
    dbg_g = dbg_b.copy()
    dbg_r = dbg_b.copy()

    lap_max = np.max(laplacian)
    lap_norm = laplacian / lap_max

    mask1 = lap_norm > 0.0
    dbg_g[mask1] = lap_norm[mask1]

    mask2 = lap_norm <= 0.0
    dbg_r[mask2] = -lap_norm[mask2]

    dbg_image = cv2.merge((dbg_b, dbg_g, dbg_r))
    return dbg_image

def debug_single_chain(single_chain):
    chain = single_chain['chain']

    num_edgels = len(chain)
    num_cluster = num_edgels // 8
    if num_cluster > 4:
        num_cluster = 4
    elif num_cluster < 2:
        num_cluster = 2

    Z = np.zeros((num_edgels, 2))

    idx = 0
    for e in chain:
        mid_pt = e['mid_pt']
        grad = e['grad']
        tangent_0 = e['tang_0']
        tangent_dir = tangent_0[0]
        perp_dist = tangent_0[2]
        theta_rad = np.arctan2(tangent_dir[1], tangent_dir[0])

        # print(mid_pt, grad, tangent_dir, theta_rad, perp_dist)

        Z[idx, :] = (theta_rad, perp_dist * 0.1)
        idx += 1

    # convert to np.float32
    Z = np.float32(Z)

    # define criteria and apply kmeans()
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 10, 1.0)
    ret, label, center = cv2.kmeans(Z, num_cluster, None, criteria, 10, cv2.KMEANS_RANDOM_CENTERS)

    # colors = ('r', 'g', 'b', 'y')
    #
    # # Now separate the data, Note the flatten()
    # for i in range(num_cluster):
    #     A = Z[label.ravel() == i]
    #
    #     # Plot the data
    #     plt.scatter(A[:, 0], A[:, 1], c=colors[i])
    #
    # # plt.scatter(center[:, 0], center[:, 1], s=80, c='y', marker='s')
    # plt.xlabel('Height'), plt.ylabel('Weight')
    # plt.show()

def debug_chains(chains, threshold, shape):

    if len(chains) > 0:

        # for c in chains:
        #     chain_grad_mag = c['grad_mag_max']
        #     if chain_grad_mag > threshold:
        #         debug_single_chain(c)

        plt.xlim(0, shape[1])
        plt.ylim(shape[0], 0)

        cidx = 0
        for c in chains:
            chain_grad_mag = c['grad_mag_max']
            if chain_grad_mag > threshold:
                chain = c['chain']

                mid_pts = np.zeros((len(chain), 2))

                midx = 0
                for e in chain:
                    mid_pts[midx] = e['mid_pt']

                    # if cidx == 0:
                    #     print(mid_pts[midx], e['grad'], e['theta_deg'], e['theta_deg_abs'])

                    midx += 1

                # plt.scatter(mid_pts[:, 1], mid_pts[:, 0])
                cidx += 1

        # plt.show()

def debug_edgels(lapl, chains, threshold):
    shape = lapl.shape
    dbg_b = np.zeros(shape)
    dbg_g = dbg_b.copy()
    dbg_r = dbg_b.copy()

    # debug_chains(chains, threshold, shape)
    dbg_idx1 = 23
    dbg_idx2 = 1

    for c in chains:
        chain_grad_mag = c['grad_mag_max']
        if chain_grad_mag > threshold or dbg_idx1 != -1:
            chain = c['chain']
            chain_idx = c['chain_index']

            # if True:
            if chain_idx != dbg_idx1 and dbg_idx1 != -1:
                continue

            # if not c['is_loop']:
            for ie in range(len(chain)):
                edgel = chain[ie]
                e_key = edgel['quad_idx']
                # edgel = edgels_dict[e_key]
                # dbg_g[edgel_idx] = edgel_grad_mag / grad_mag_max

                if dbg_idx2 != -1 and dbg_idx2 < len(c['segments']):
                    if ie < c['segments'][dbg_idx2] or ie >= c['segments'][dbg_idx2 + 1]:
                        continue

                if dbg_idx1 != -1:
                    dbg_r[e_key] = 0.3
                elif chain_idx % 6 == 0:
                    dbg_b[e_key] = 0.3
                    dbg_g[e_key] = 0.3
                elif chain_idx % 6 == 1:
                    dbg_g[e_key] = 0.3
                elif chain_idx % 6 == 2:
                    dbg_r[e_key] = 0.3
                elif chain_idx % 6 == 3:
                    dbg_r[e_key] = 0.3
                    dbg_g[e_key] = 0.3
                elif chain_idx % 6 == 4:
                    dbg_g[e_key] = 0.3
                    dbg_b[e_key] = 0.3
                elif chain_idx % 6 == 5:
                    dbg_b[e_key] = 0.3
                    dbg_r[e_key] = 0.3

    dbg_image = cv2.merge((dbg_b, dbg_g, dbg_r))

    color_index = 0
    for c in chains:
        continue
        chain_grad_mag = c['grad_mag_max']
        if chain_grad_mag > threshold:
            chain = c['chain']
            chain_idx = c['chain_index']

            if 'lines' in c:
                # if True:
                if chain_idx != dbg_idx1 and dbg_idx1 != -1:
                    continue

                fit_lines = c['lines']

                for i in range(len(fit_lines)):
                    if i != dbg_idx2 and dbg_idx2 != -1:
                        continue

                    l = fit_lines[i]
                    seg_grad_mag_max = l['grad_mag_max']
                    if seg_grad_mag_max < threshold:
                        continue

                    # num_pts = l['num_pts']
                    # if num_pts < 16:
                    #     continue

                    end_pts = l['end_pts']
                    pt0 = end_pts[0]
                    pt1 = end_pts[1]

                    color = (0.0, 0.8, 0.0)
                    if dbg_idx1 != -1:
                        color = (0, 0.8, 0.0)
                    elif color_index % 6 == 0:
                        color = (0, 0.5, 0.8)
                    elif color_index % 6 == 1:
                        color = (0, 0, 0.8)
                    elif color_index % 6 == 2:
                        color = (0.8, 0, 0.0)
                    elif color_index % 6 == 3:
                        color = (0.8, 0.8, 0.0)
                    elif color_index % 6 == 4:
                        color = (0.0, 0.8, 0.8)
                    elif color_index % 6 == 5:
                        color = (0.8, 0.0, 0.8)

                    cv2.line(dbg_image, (pt0[1], pt0[0]), (pt1[1], pt1[0]), color=color)

                    color_index += 1

    return dbg_image

def debug_sorted_lines(lapl, sorted_lines):
    shape = lapl.shape
    dbg_b = np.zeros(shape)
    dbg_g = dbg_b.copy()
    dbg_r = dbg_b.copy()

    dbg_image = cv2.merge((dbg_b, dbg_g, dbg_r))
    if len(sorted_lines) == 0:
        return dbg_image

    green = (0, 1.0, 0)
    red = (0, 0, 1.0)
    blue = (1.0, 0, 0)
    yellow = (0, 1.0, 1.0)
    pink = (203.0 / 255, 192.0 / 255, 1.0)
    colors = [green, green, blue, blue, yellow, yellow]
    thickness = [2, 2, 2, 2, 1, 1]

    cost_best = sorted_lines[0]['cost_final']

    irange = min(len(colors), len(sorted_lines))

    for i in range(irange):
        sl = sorted_lines[i]

        cost_final = sl['cost_final']
        if cost_final > cost_best * 2:
            break

        end_pts = sl['end_pts']
        pt0 = end_pts[0]
        pt1 = end_pts[1]
        cv2.line(dbg_image, (pt0[1], pt0[0]), (pt1[1], pt1[0]), color=colors[i], thickness=thickness[i])

    return dbg_image


def proto_histogram(image_gray_u8):
    num_bins = 16
    assert (256 % num_bins == 0)
    colors_per_bin = 256 // num_bins

    # fill histogram content
    image_gray_hist = np.zeros((num_bins), dtype=int)
    for ix in range(image_gray_u8.shape[0]):
        k = image_gray_u8[ix] // colors_per_bin
        image_gray_hist[k] += 1

    threshold = np.std(image_gray_hist) / 2.0 # magic number
    ranges = list()

    curr_bin_start = 0
    while True:
        while curr_bin_start < num_bins and image_gray_hist[curr_bin_start] == 0:
            curr_bin_start += 1

        if curr_bin_start == num_bins:
            break

        curr_bin_end = curr_bin_start
        curr_bin_sum = image_gray_hist[curr_bin_start]
        curr_bin_average = curr_bin_sum

        while True:
            next_bin_idx = curr_bin_end + 1
            if next_bin_idx >= num_bins:
                break

            if abs(image_gray_hist[next_bin_idx] - curr_bin_average) > threshold:
                break

            curr_bin_end = next_bin_idx
            curr_bin_sum += image_gray_hist[curr_bin_end]
            curr_bin_average = curr_bin_sum / (curr_bin_end - curr_bin_start + 1)

        ranges.append((curr_bin_start, curr_bin_end, curr_bin_sum, curr_bin_average))

        # next iteration
        curr_bin_start = curr_bin_end + 1
        if curr_bin_start >= num_bins:
            break

    # prototype, merge single bin to others
    while True:
        found = False

        # test middle one
        for ix in ranges:
            if ix[0] == ix[1]:
                pass

        # test side one

        break

    return ranges


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
