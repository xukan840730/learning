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

sobel_hori_b = np.zeros(sobel_hori_f.shape)
sobel_hori_g = sobel_hori_b.copy()
sobel_hori_r = sobel_hori_b.copy()

sobel_vert_b = np.zeros(sobel_vert_f.shape)
sobel_vert_g = sobel_vert_b.copy()
sobel_vert_r = sobel_vert_b.copy()

threshold = 0.1

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

# cv2.imshow("image_gray", image_gray)
# cv2.imshow("sobelx", sobelx_f)
# cv2.imshow("sobely", sobely_f)
# cv2.imshow("test_sobely", test_sobely)
cv2.imshow("sobel_hori_dbg", sobel_hori_dbg2)
cv2.imshow("sobel_vert_dbg", sobel_vert_dbg2)
cv2.waitKey(0)
cv2.destroyAllWindows()

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
