import numpy as np
import numpy.linalg as linalg
from math import sqrt, exp, pi
import matplotlib.pyplot as plt

def problem2_1():
    A = np.matrix([[-2, 2, -3],
                   [2, 1, -6],
                   [-1, -2, 0]])

    print(linalg.eig(A))

problem2_1()


def problem2_2():
    def guassianPdf(mu, sigma, x):
        return 1.0 / (sigma * sqrt(2 * pi)) * exp(-(x - mu) ** 2 / (2.0 * sigma ** 2))

    def plotGuassian(mu, sigma, samples, figureIndex):
        #plt.figure(figureIndex)
        plt.plot(samples, [guassianPdf(mu, sigma, x) for x in samples], lw=2)

    samples = np.arange(-5.0, 5.0, 0.05)
    plotGuassian(0, 1, samples, 1)
    plotGuassian(2, 0.5, samples, 2)
    plotGuassian(-2, 2, samples, 3)

    plt.grid()
    plt.show()

#problem2_2()

def problem2_3():
    def guassianPdf2d(mu, div, siginv, x):
        t2 = np.transpose(x - mu)
        t4 = x - mu
        t5 = np.dot(t2, siginv)
        t6 = np.dot(t5, t4)
        t7 = -0.5 * t6
        d = 1.0 / div * np.exp(t7)
        return float(d)

    sigma = np.matrix([[1, 0],
                       [0, 1]])
    mu = np.array([0.0, 0.0])

    div = sqrt(linalg.det(2.0 * pi * sigma))
    siginv = linalg.inv(sigma)

    x = np.arange(-5.0, 5.0, 0.05)
    y = np.arange(-5.0, 5.0, 0.05)
    X, Y = np.meshgrid(x, y)
    Z = np.zeros(X.shape)
    nx, ny = X.shape
    for ix in range(0, nx):
        for iy in range(0, ny):
            Z[ix, iy] = guassianPdf2d(mu, div, siginv, np.array([X[ix, iy], Y[ix, iy]]))

    from mpl_toolkits.mplot3d import Axes3D

    fig = plt.figure(3)
    ax = Axes3D(fig)
    ax.plot_surface(X, Y, Z)
    #plt.contour(X, Y, Z)
    plt.show()

problem2_3()