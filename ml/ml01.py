import numpy as np
import numpy.linalg as linalg
import matplotlib.pyplot as plt

def newton_opt(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    ll = np.zeros((max_iters, 1))

    ## newton's method to minimize function.
    for i in range(1, max_iters):
        margins = np.multiply(ym, xm * theta)
        ll[i] = (1.0/mm) * np.sum(np.log(1 + np.exp(-margins)))
        probs = 1.0 / (1 + np.exp(margins))
        grad = -(1.0/mm) * (xm.transpose() * np.multiply(probs, ym))
        H = (1.0/mm) * (xm.transpose() * np.diag(np.array(np.multiply(probs, (1-probs))).flatten()) * xm)
        theta = theta - linalg.inv(H) * grad

    return theta, ll

def test_func(xa, ya):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    ym01 = np.where(ym > 0, ym, 0)
    #print ym01



def main():
    x = np.loadtxt('logistic_x.txt')
    y = np.loadtxt('logistic_y.txt')

    t1 = np.ones((x.shape[0], 1))
    X = np.append(t1, x, axis=1)
    Y = y.reshape((y.size, 1))

    # theta = np.matrix([[-2.6205, 0.7604, 1.1719]]), # result from anser
    theta, ll = newton_opt(X, Y, 20)

    test_func(X, Y)

    t2 = np.dot(X, theta)
    hypoY = 1 / (1 + np.exp(-t2))

    compY = np.append(np.reshape(Y, (Y.size, 1)), hypoY, axis=1)

    #print compY

    mask0 = np.array(Y).flatten() <= 0
    mask1 = np.array(Y).flatten() > 0

    plt.plot(X[mask0, 1], X[mask0, 2], 'rx')
    plt.plot(X[mask1, 1], X[mask1, 2], 'bo')

    xmin1 = np.min(X[:, 1])
    xmax1 = np.max(X[:, 1])

    x1 = np.linspace(xmin1, xmax1)

    theta0 = theta[0, 0]
    theta1 = theta[1, 0]
    theta2 = theta[2, 0]
    x2 = -(theta0 / theta2) - (theta1 / theta2) * x1

    plt.plot(x1, x2)

    plt.show()

main()