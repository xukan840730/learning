import math as math
import numpy as np

def MinMax(v, vmin, vmax):
    if v < vmin:
        return vmin
    elif v > vmax:
        return vmax
    else:
        return v

def image_u8_to_f(img_u8):
    img_f = np.zeros(img_u8.shape, dtype=float)
    img_f = img_u8 / 255
    return img_f