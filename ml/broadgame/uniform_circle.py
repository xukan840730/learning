import numpy as np
import matplotlib.pyplot as plt
import math as math

num_samples = 1000

lmin = 0.5
lmax = 1.0

phi = np.sqrt(np.random.rand(num_samples)) * (lmax - lmin) + lmin
theta = np.random.rand(num_samples) * math.pi * 2

x = np.cos(theta) * phi
y = np.sin(theta) * phi

fig, ax = plt.subplots()

plt.plot(x, y, 'rx')
# plt.axis([-1, 1, -1, 1])
ax.set_aspect(1.0)
plt.show()
