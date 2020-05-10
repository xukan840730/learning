import numpy as np
import matplotlib.pyplot as plt
import unsplearn.util as unsplutil

def visualize(primShape):
    shapeMask = primShape.getShapeMask()
    img_float = np.zeros(shapeMask.shape)

    for irow in range(shapeMask.shape[0]):
        for icol in range(shapeMask.shape[1]):
            if shapeMask[irow, icol]:
                img_float[irow, icol] = 0.8

    # draw center of mass
    (cx, cy) = unsplutil.getImageMaskCenterOfMass(shapeMask)
    ix = int(round(cx))
    iy = int(round(cy))
    if ix >= 0 and ix < shapeMask.shape[0] and iy >= 0 and iy < shapeMask.shape[1]:
        img_float[ix, iy] = 1.0

    plt.imshow(img_float)
    plt.show()