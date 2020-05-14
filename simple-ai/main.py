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
# pvis.visualize(spd.g_square)

# unslutil.test_pca(spd.g_triangle.getShapeMask(), 2)

testShape = spd.g_triangle
# testShape = spd.g_square
imCom = unslutil.getImageMaskCenterOfMass(testShape.getShapeMask())
imagePolarMask = unslpolar.imageMaskToPolarCoords2(testShape.getShapeMask(), imCom)
# print(imagePolarMask)

imagePolarHist = unslpolar.imagePolarHistogram(imagePolarMask)
imageDominantAxis = unslpolar.findDominantAxis(imagePolarHist)
# imagePolarMean = np.mean(imagePolarHist)
# imagePolarMedian = np.median(imagePolarHist)

for i in imageDominantAxis:
    print(i)