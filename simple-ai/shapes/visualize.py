import numpy as np
import matplotlib.pyplot as plt

def visualize(primShape):
    shapeMask = primShape.getShapeMask()
    img_float = np.zeros(shapeMask.shape)

    for irow in range(shapeMask.shape[0]):
        for icol in range(shapeMask.shape[1]):
            if shapeMask[irow, icol]:
                img_float[irow, icol] = 1.0

    plt.imshow(img_float)
    plt.show()