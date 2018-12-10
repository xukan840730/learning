import cv2
import numpy as np
import matplotlib.pyplot as plt

def canny(image):
    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    # canny = cv2.Canny(blur, 50, 150) ;; original value
    canny = cv2.Canny(blur, 20, 40)
    return canny

image = cv2.imread('test_image_small.jpg')
lane_image = np.copy(image)
canny = canny(lane_image)

canny_copy = np.copy(canny)

# cv2.imshow("result", canny_copy)
# cv2.waitKey(0)
# cv2.destroyAllWindows()

# scaledown test: when scaled down to (60, 80), road can still be recognized
# tiny_canny = cv2.resize(canny_copy, (0,0), fx=0.125, fy=0.125, interpolation=cv2.INTER_AREA)
tiny_canny = cv2.resize(canny_copy, (0,0), fx=0.125, fy=0.125)


title = "result_%d" % tiny_canny.shape[0]
# cv2.imshow(title, tiny_canny)
# cv2.waitKey(0)
# cv2.destroyAllWindows()

print(np.count_nonzero(tiny_canny))
print(np.count_nonzero(tiny_canny > 64))
print(np.count_nonzero(tiny_canny > 80))
print(np.count_nonzero(tiny_canny > 96))
print(np.count_nonzero(tiny_canny > 128))
print(np.count_nonzero(tiny_canny > 240))

tiny_canny2 = tiny_canny.copy()
tiny_canny2[tiny_canny2 <= 128] = 0
cv2.imshow(title, tiny_canny2)
cv2.waitKey(0)
cv2.destroyAllWindows()

# now we have a much simpler image to do line/curve fitting.
