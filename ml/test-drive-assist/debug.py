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