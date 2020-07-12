import numpy as np
import cv2 as cv2

def visualize_image_region(image_region):
    channel_b = np.zeros(image_region.shape, dtype=float)
    channel_g = np.zeros(image_region.shape, dtype=float)
    channel_r = np.zeros(image_region.shape, dtype=float)

    for ix in range(image_region.shape[0]):
        for iy in range(image_region.shape[1]):
            idx = image_region[ix, iy] % 5
            if idx == 0:
                channel_b[ix, iy] = 0.0
                channel_g[ix, iy] = 0.0
                channel_r[ix, iy] = 0.0
            elif idx == 1:
                channel_b[ix, iy] = 1.0
            elif idx == 2:
                channel_g[ix, iy] = 1.0
            elif idx == 3:
                channel_r[ix, iy] = 1.0
            elif idx == 4:
                channel_b[ix, iy] = 1.0
                channel_g[ix, iy] = 1.0
            elif idx == 5:
                channel_b[ix, iy] = 1.0
                channel_r[ix, iy] = 1.0
            elif idx == 6:
                channel_g[ix, iy] = 1.0
                channel_r[ix, iy] = 1.0
            elif idx == 7:
                channel_b[ix, iy] = 1.0
                channel_g[ix, iy] = 1.0
                channel_r[ix, iy] = 1.0
            elif idx == 8:
                channel_b[ix, iy] = 0.0
                channel_g[ix, iy] = 0.0
                channel_r[ix, iy] = 0.0
            else:
                assert(False)


    img_dbg = cv2.merge((channel_b, channel_g, channel_r))

    cv2.imshow('img_dbg', img_dbg)
    cv2.waitKey(0)
    cv2.destroyAllWindows()