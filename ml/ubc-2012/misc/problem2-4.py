import numpy as np
import matplotlib.pyplot as plt
import scipy.ndimage as ndimage

img = plt.imread('miku.png')

plt.figure()

plt.subplot(2, 2, 1)
plt.imshow(img)

plt.subplot(2, 2, 2)
plt.imshow(ndimage.rotate(img, 90))

plt.subplot(2, 2, 3)
nored = img.copy()
#print(nored[:,:,0].shape)
nored[:,:,0] = np.zeros(nored[:,:,0].shape)
plt.imshow(nored)

plt.subplot(2, 2, 4)
onlyred = img.copy()
onlyred[:,:,1:3] = np.zeros(onlyred[:,:,1:3].shape)
plt.imshow(onlyred)

plt.show()
