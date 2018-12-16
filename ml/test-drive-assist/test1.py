import cv2
import numpy as np
import matplotlib.pyplot as plt
import simple_math as sm

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

def debug_sobel(image_gray, sobel_hori_f, sobel_vert_f, threshold):
    sobel_hori_b = np.zeros(sobel_hori_f.shape)
    sobel_hori_g = sobel_hori_b.copy()
    sobel_hori_r = sobel_hori_b.copy()

    sobel_vert_b = np.zeros(sobel_vert_f.shape)
    sobel_vert_g = sobel_vert_b.copy()
    sobel_vert_r = sobel_vert_b.copy()

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

    sobel_hori_dbg = cv2.merge((sobel_hori_b, sobel_hori_g, sobel_hori_r))
    sobel_vert_dbg = cv2.merge((sobel_vert_b, sobel_vert_g, sobel_vert_r))

    image_gray_3_u8 = cv2.cvtColor(image_gray, cv2.COLOR_GRAY2BGR)
    image_gray_3_f = image_gray_3_u8.astype(float) / 255.0

    sobel_hori_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_hori_dbg, 0.5, 0.0)
    sobel_vert_dbg2 = cv2.addWeighted(image_gray_3_f, 1.0, sobel_vert_dbg, 0.5, 0.0)

    cv2.imshow("sobel_hori_dbg", sobel_hori_dbg2)
    cv2.imshow("sobel_vert_dbg", sobel_vert_dbg2)
    cv2.waitKey(0)
    cv2.destroyAllWindows()

debug_sobel(image_gray, sobel_hori_f, sobel_vert_f, threshold)

sobel_grad_f = cv2.merge((sobel_hori_f, sobel_vert_f))

# test expansion.
# at first, none of pixels is visited.

def expand_one(pos, image_shape, visited_global, sobel_grad_f, threshold, frontiers, visited_new):
    image_height = image_shape[0]
    image_width = image_shape[1]

    def expand_one_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, visited_new):
        if not visited_global[new_pos] and not visited_new[new_pos]:
            grad = sobel_grad_f[new_pos]
            grad_mag = np.sqrt(grad[0] * grad[0] + grad[1] * grad[1])
            if grad_mag < threshold:
                frontiers.append(new_pos)
                visited_new[new_pos] = True

    if pos[0] > 0:
        # test up
        new_pos = (pos[0] - 1, pos[1])
        expand_one_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, visited_new)

    if pos[0] < image_height - 1:
        # test down
        new_pos = (pos[0] + 1, pos[1])
        expand_one_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, visited_new)

    if pos[1] > 0:
        # test left
        new_pos = (pos[0], pos[1] - 1)
        expand_one_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, visited_new)

    if pos[1] < image_width - 1:
        # test right
        new_pos = (pos[0], pos[1] + 1)
        expand_one_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, visited_new)

def expand_version1(pos, image_shape, visited_global, sobel_grad_f, threshold, visited_new):
    frontiers = list()
    expand_one(pos, image_shape, visited_global, sobel_grad_f, threshold, frontiers, visited_new)

    while len(frontiers) > 0:
        new_fronties = list()

        for x in frontiers:
            expand_one(x, image_shape, visited_global, sobel_grad_f, threshold, new_fronties, visited_new)

        frontiers = new_fronties

def debug_expansion(image_gray, visited_mask, b, g, r):
    channel_b = np.zeros(image_gray.shape)
    channel_g = channel_b.copy()
    channel_r = channel_b.copy()

    for ix in range(image_gray.shape[0]):
        for iy in range(image_gray.shape[1]):
            if visited_mask[ix, iy]:
                channel_b[ix, iy] = b
                channel_g[ix, iy] = g
                channel_r[ix, iy] = r

    dbg = cv2.merge((channel_b, channel_g, channel_r))

    image_gray_3_u8 = cv2.cvtColor(image_gray, cv2.COLOR_GRAY2BGR)
    image_gray_3_f = image_gray_3_u8.astype(float) / 255.0

    image_dbg = cv2.addWeighted(image_gray_3_f, 1.0, dbg, 0.5, 0.0)

    cv2.imshow("expansion_test", image_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()

visited_global = np.zeros(image_gray.shape, dtype=bool)

for iter in range(0, 2):
    starting_pos = (image_height - 1, image_width // 2)
    if iter == 1:
        starting_pos = (image_height - 1, image_width - 1)
    visited_global[starting_pos] = True

    frontiers = list()
    visited_new = np.zeros(visited_global.shape, dtype=bool)
    expand_version1(starting_pos, image_gray.shape, visited_global, sobel_grad_f, threshold, visited_new)
    debug_expansion(image_gray, visited_new, 0, 0, 1.0)

    # fill global mask with new mask
    visited_global = np.bitwise_or(visited_global, visited_new)

# scaledown test: when scaled down to (60, 80), road can still be recognized
# tiny_canny = cv2.resize(canny, (0,0), fx=0.125, fy=0.125, interpolation=cv2.INTER_AREA)
# tiny_canny = cv2.resize(canny, (0,0), fx=0.125, fy=0.125)
#
# title = "result_%d" % tiny_canny.shape[0]
# # cv2.imshow(title, tiny_canny)
# # cv2.waitKey(0)
# # cv2.destroyAllWindows()
#
# print(np.count_nonzero(tiny_canny))
# print(np.count_nonzero(tiny_canny > 64))
# print(np.count_nonzero(tiny_canny > 80))
# print(np.count_nonzero(tiny_canny > 96))
# print(np.count_nonzero(tiny_canny > 128))
# print(np.count_nonzero(tiny_canny > 240))
#
# # tiny_canny2 = tiny_canny.copy()
# # tiny_canny2[tiny_canny2 <= 128] = 0
# # cv2.imshow(title, tiny_canny2)
# # cv2.waitKey(0)
# # cv2.destroyAllWindows()
#
# def retrieve_sample_points(image, threshold):
#     num_points = np.count_nonzero(image > threshold)
#
#     # retrieve points coordinates
#     coords = np.zeros((num_points, 2))
#
#     idx = 0
#     for idx_i in range(image.shape[0]):
#         for idx_j in range(image.shape[1]):
#             if image[idx_i, idx_j] > threshold:
#                 coords[idx] = np.array((idx_i, idx_j))
#                 idx = idx + 1
#
#     return coords
#
#
# # now we have a much simpler image to do line/curve fitting.
# threshold = 128
# sample_points = retrieve_sample_points(tiny_canny, threshold)
#
# def generate_line_candidates(scatter_points):
#     lines = ()
#
#     # brute force generate all lines
#     for ii in range(scatter_points.shape[0]):
#         for jj in range(ii, scatter_points.shape[0]):
#             line = ()
#
#     return lines
#
# line_candidates = generate_line_candidates(sample_points)
