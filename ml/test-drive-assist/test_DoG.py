import cv2
import numpy as np

def debug_dog(image_dog):
    dbg_b = np.zeros(image_dog.shape)
    dbg_g = dbg_b.copy()
    dbg_r = dbg_b.copy()

    for ix in range(image_dog.shape[0]):
        for iy in range(image_dog.shape[1]):
            if image_dog[ix, iy] >= 0.0:
                dbg_b[ix, iy] = 1.0
            else:
                dbg_g[ix, iy] = 1.0

    dbg_dog = cv2.merge((dbg_b, dbg_g, dbg_r))

    cv2.imshow('Frame_1', dbg_dog)
    cv2.waitKey(0)

    # do a bit of cleanup
    cv2.destroyAllWindows()

image_u8 = cv2.imread('test_image_tiny.jpg')
image_grayscale = cv2.cvtColor(image_u8, cv2.COLOR_RGB2GRAY)

ksize = (5, 5)
sigma1 = 0.3 * ((ksize[0] - 1) * 0.5 - 1) + 0.8
# sigma2 = sigma1 + 0.5
sigma2 = sigma1 * 2.0
image_blur_1_u8 = cv2.GaussianBlur(image_grayscale, ksize, sigma1, sigma1)
image_blur_2_u8 = cv2.GaussianBlur(image_grayscale, ksize, sigma2, sigma2)

image_blur_1_f = image_blur_1_u8.astype(np.float32) / 255.0
image_blur_2_f = image_blur_2_u8.astype(np.float32) / 255.0

image_dog = image_blur_1_f - image_blur_2_f
debug_dog(image_dog)
