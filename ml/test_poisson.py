import numpy as np
import scipy.misc as misc
import matplotlib.pyplot as plt

# 56 person in line.
# caller depart according to a Poisson process with a rate of lambda = 2 per minute
# what's the probability you will have to wait for more than 30 minutes?

# fYk(y) = (lambda^k * y^(k-1) * e^(-lambda*y)) / fact(k-1)

def p_poisson(n, p, k):
    lamb = n * p
    return np.exp(-lamb) * np.power(lamb, k) / misc.factorial(k)

#print p[25: 30]

#print p
#plt.plot(y, p)
#plt.show()

print p_poisson(250, 0.02, 5)