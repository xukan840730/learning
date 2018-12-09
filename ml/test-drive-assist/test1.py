import cv2
import numpy as np
import matplotlib.pyplot as plt

def canny(image):
    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    canny = cv2.Canny(blur, 20, 40)
    return canny

image = cv2.imread('test_image_small.jpg')
lane_image = np.copy(image)
canny = canny(lane_image)

canny_copy = np.copy(canny)

cv2.imshow("result", canny_copy)
cv2.waitKey(0)
cv2.destroyAllWindows()

# scaledown test: when scaled down to (60, 80), road can still be recognized
# while True:
#     shape0, shape1 = canny_copy.shape
#     new_shape = (shape0 // 2, shape1 // 2)
#
#     if new_shape[0] < 10 or new_shape[1] < 10:
#         break
#
#     # cv2.resize(canny_copy, new_shape, new_canny, 0, 0, cv2.INTER_AREA)
#     new_canny = cv2.resize(canny_copy, (0,0), fx=0.5, fy=0.5)
#
#     title = "result_%d" % shape0
#     print(new_shape)
#
#     cv2.imshow(title, new_canny)
#     cv2.waitKey(0)
#     cv2.destroyAllWindows()
#
#     canny_copy = new_canny


# cv2.imshow("result", canny)
# cv2.waitKey(0)
