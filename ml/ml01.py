import numpy as np
import matplotlib.pyplot as plt

def log_regression(X, Y, max_iters):
    mm = X.shape[0]
    nn = X.shape[1]
    print mm, nn

def main():
    x = np.loadtxt('logistic_x.txt')
    Y = np.loadtxt('logistic_y.txt')

    #Y = np.reshape(Y, (Y.size, 1))

    #test1()

    t1 = np.ones((x.shape[0], 1))
    X = np.append(t1, x, axis=1)
    #print X
    #print X.shape

    log_regression(X, Y, 20)

    theta = np.array([[-2.6205, 0.7604, 1.1719]])
    #print theta
    #print theta.shape

    t2 = np.dot(X, theta.transpose())
    hypoY = 1 / (1 + np.exp(-t2))

    #print Y.shape
    #print t2.shape

    compY = np.append(np.reshape(Y, (Y.size, 1)), hypoY, axis=1)
    #print compY

    #print hypoY
    #thetaT = theta.transpose()
    #print thetaT
    #print thetaT.shape
    #xM = np.asmatrix(X)
    #thetaM = np.asmatrix(theta)

    mask0 = Y <= 0
    mask1 = Y > 0

    plt.plot(X[mask0, 1], X[mask0, 2], 'rx')
    plt.plot(X[mask1, 1], X[mask1, 2], 'bo')

    xmin1 = np.min(X[:, 1])
    xmax1 = np.max(X[:, 1])
    x1 = np.linspace(xmin1, xmax1)
    x2 = -(theta[0, 0] / theta[0, 2]) - (theta[0, 1] / theta[0, 2]) * x1

    plt.plot(x1, x2)

    plt.show()


main()