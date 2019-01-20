import cv2
import numpy as np
import region as rg
import debug as dbg

def process_image(image_u8):
    image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)
    image_height = image_grayscale.shape[0]
    image_width = image_grayscale.shape[1]

    image_blur_u8 = cv2.GaussianBlur(image_grayscale, (5, 5), 0)
    image_blur_f = image_blur_u8.astype(float) / 255.0
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
def process_image2(image_u8):
    image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)
    image_height = image_grayscale.shape[0]
    image_width = image_grayscale.shape[1]

    sigma = 1.5
    image_blur_u8 = cv2.GaussianBlur(image_grayscale, (5, 5), sigma)
    image_blur_f = image_blur_u8.astype(float) / 255.0

    pyramid_l1 = cv2.pyrDown(image_blur_f)
    l1_expanded = cv2.pyrUp(pyramid_l1)
    laplacian = cv2.subtract(image_blur_f, l1_expanded)
    dbg_image = dbg.debug_laplacian(laplacian) * 255.0
    return dbg_image.astype(np.uint8)