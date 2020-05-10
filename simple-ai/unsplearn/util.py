import numpy as np

def center_of_mass(shapeMask):
    num_rows = shapeMask.shape[0]
    num_cols = shapeMask.shape[1]
    m = np.zeros([num_rows, num_cols])

    for x in range(num_rows):
        for y in range(num_cols):
            if shapeMask[x, y]:
                m[x, y] = 1.0
            else:
                m[x, y] = 0.0
    m = m / np.sum(np.sum(m))

    # marginal distributions
    dx = np.sum(m, 1)
    dy = np.sum(m, 0)

    # expected values
    cx = np.sum(dx * np.arange(num_rows))
    cy = np.sum(dy * np.arange(num_cols))

    return (cx, cy)