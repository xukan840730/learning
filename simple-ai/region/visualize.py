import numpy as np
import cv2 as cv2

def visualize_image_region(image_region):
    img_dbg = np.zeros(image_region.shape, dtype=float)

    for ix in range(image_region.shape[0]):
        for iy in range(image_region.shape[1]):
            img_dbg[ix, iy] = int((image_region[ix, iy] % 9) / 9 * 255)

    cv2.imshow('img_dbg', img_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()