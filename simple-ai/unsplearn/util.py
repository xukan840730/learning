import numpy as np
# from sklearn.decomposition import PCA

# getImageMaskCenterOfMass: get imageMask center of mass
def getImageMaskCenterOfMass(shapeMask):
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

    return (cx + 0.5, cy + 0.5)

def testPCA(shapeMask, num_comp):
    num_rows = shapeMask.shape[0]
    num_cols = shapeMask.shape[1]

    num_set_bits = np.count_nonzero(shapeMask)
    X = np.empty([num_set_bits, 2])

    index = 0
    for irow in range(num_rows):
        for icol in range(num_cols):
            if shapeMask[irow, icol]:
                X[index, :] = (irow, icol)
                index = index + 1

    pca = PCA(n_components=num_comp)
    pca.fit(X)
    print(pca.components_)
    # print(pca.explained_variance_)
