import cv2
import numpy as np
import matplotlib.pyplot as plt
import simple_math as sm
import region as rg
import debug as dbg

# def canny(image):
#     gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
#     blur = cv2.GaussianBlur(gray, (5, 5), 0)
#     # canny = cv2.Canny(blur, 50, 150) ;; original value
#     canny = cv2.Canny(blur, 20, 40)
#     # sobel = cv2.Sobel(blur, cv2.CV_64F, 1, 0, ksize=5)
#     return canny

image = cv2.imread('test_image_small.jpg')
#canny = canny(image)

image_height = image.shape[0]
image_width = image.shape[1]

image_gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
image_blur_u8 = cv2.GaussianBlur(image_gray, (5, 5), 0)
image_blur_f = image_blur_u8.astype(float) / 255.0
# image_b, image_g, image_r = cv2.split(image)

sobel_hori_f = cv2.Sobel(image_blur_f, cv2.CV_64F, 1, 0, ksize=3)
sobel_vert_f = cv2.Sobel(image_blur_f, cv2.CV_64F, 0, 1, ksize=3)
# sobelx_sqr = sobelx_f * sobelx_f
# sobely_sqr = sobely_f * sobely_f
# sobel_grad_f = np.sqrt(sobelx_sqr + sobely_sqr)
# sobel_grad_u8 = sobel_grad_f.astype(np.uint8)

threshold = 0.1

# dbg.debug_sobel(image_gray, sobel_hori_f, sobel_vert_f, threshold)

sobel_grad_f = cv2.merge((sobel_hori_f, sobel_vert_f))
sobel_grad_mag = np.zeros(sobel_hori_f.shape)
for ix in range(sobel_hori_f.shape[0]):
    for iy in range(sobel_hori_f.shape[1]):
        sobel_grad_mag[ix, iy] = np.linalg.norm(sobel_grad_f[ix, iy])

visited_global = np.zeros(image_gray.shape, dtype=bool)
expand_regions = list()

# get expansions from bottom row
# for ix in range(488, 489):
# for ix in range(0, 1):
for ix in range(0, image_width):
    starting_pos = (image_height - 1, ix)
    # starting_pos = (image_height - 170, 225)

    # debug print
    # for idbg in range(0, 10):
    #     dbg_pos = (starting_pos[0] + idbg, starting_pos[1] - 4)
    #     print('(' + str(dbg_pos[0]) + ', ' + str(dbg_pos[1]) + '): ' + str(sobel_grad_f[dbg_pos]))

    # if pixel is in a region, skip to next pixel
    if visited_global[starting_pos]:
        continue;

    # or if pixel has great gradient, skip to next pixel
    grad_mag = sobel_grad_mag[starting_pos]
    if (grad_mag >= threshold):
        continue;

    print('start expansion from: (' + str(starting_pos[0]) + ', ' + str(starting_pos[1]) + ')')

    visited_global[starting_pos] = True

    new_region = np.zeros(visited_global.shape, dtype=bool)
    rg.expand_v2(starting_pos, visited_global, sobel_grad_f, sobel_grad_mag, threshold, new_region, False)
    expand_regions.append(new_region)

    # fill global mask with new mask
    visited_global = np.bitwise_or(visited_global, new_region)

# print(len(expand_regions))

for iregion in expand_regions:
    # for ix in range(iregion.shape[0]):
    #     for iy in range(iregion.shape[1]):
    #         if iy != 230:
    #             iregion[ix, iy] = False
    dbg.debug_expansion(image_gray, iregion, 0, 0, 1.0)
