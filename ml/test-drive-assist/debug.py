import cv2
import numpy as np


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

    image_gray_3_u8 = cv2.cvtColor(image_gray, cv2.COLOR_GRAY2BGR)
    image_gray_3_f = image_gray_3_u8.astype(float) / 255.0

    sobel_hori_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_hori_dbg, 0.5, 0.0)
    sobel_vert_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_vert_dbg, 0.5, 0.0)
    sobel_mag_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_mag_dbg, 1.0, 0.0)

    cv2.imshow("sobel_hori_dbg", sobel_hori_dbg2)
    cv2.imshow("sobel_vert_dbg", sobel_vert_dbg2)
    cv2.imshow("sobel_mag_dbg", sobel_mag_dbg)
    cv2.imshow("sobel_local_max", sobel_local_max_dbg)
    cv2.imshow("sobel_mag_mod_dbg", sobel_mag_mod_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()


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
    image_gray_3_f = image_gray_3_u8.astype(float) / 255.0

    image_dbg = cv2.addWeighted(image_gray_3_f, 1.0, dbg, 0.5, 0.0)

    cv2.imshow("expansion_test", image_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()
