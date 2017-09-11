import numpy as np
import matplotlib.pyplot as plt

"""
lamb = 2
x = np.linspace(0, 10, 1001)
#print x
fx = lamb * np.exp(-lamb*x)
integral = np.sum(fx * 0.01)
print 'integral:', integral

plt.plot(x, fx)
plt.show()
"""

x = np.linspace(0.3, 0.5, 20)
fx = (np.power(x, 3) - 2*np.power(x, 2) + 0.5)/((1-x)*x)

plt.plot(x, fx)
plt.show()