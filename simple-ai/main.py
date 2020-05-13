import numpy as np
import matplotlib.pyplot as plt
import shapes.pre_defined as spd
import shapes.visualize as pvis
import unsplearn.util as unslutil
import unsplearn.polar_coord as unslpolar

print("Hello World!")

# pvis.visualize(spd.g_horiLine)
# pvis.visualize(spd.g_vertLine)
# pvis.visualize(spd.g_triangle)

# unslutil.test_pca(spd.g_triangle.getShapeMask(), 2)

imCom = unslutil.getImageMaskCenterOfMass(spd.g_triangle.getShapeMask())
imagePolarMask = unslpolar.imageMaskToPolarCoords2(spd.g_triangle.getShapeMask(), imCom)
# print(imagePolarMask)

imagePolarHist = unslpolar.imagePolarHistogram(imagePolarMask)
print(imagePolarHist)

imagePolarMean = np.mean(imagePolarHist)
imagePolarMedian = np.median(imagePolarHist)
