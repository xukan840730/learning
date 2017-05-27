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
    for i in range(0, max_iters):
        margins = np.multiply(ym, xm * theta)
        ll[i] = (1.0/mm) * np.sum(np.log(1 + np.exp(-margins)))
        probs = 1.0 / (1 + np.exp(margins))
        grad = -(1.0/mm) * (xm.transpose() * np.multiply(probs, ym))
        H = (1.0/mm) * (xm.transpose() * np.diag(np.array(np.multiply(probs, (1-probs))).flatten()) * xm)
        theta = theta - linalg.inv(H) * grad

    return theta, ll

# map -1 to 0 and use sigmoid func to maximize likelihood
def test_func1(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    ym01 = np.where(ym > 0, ym, 0)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    alpha = 0.05

    for j in range(0, max_iters):
        for i in range(0, mm):
            margins = np.dot(xm[i], theta)
            #print margins
            hypo = 1.0 / (1 + np.exp(-margins))
            t1 = (ym01[i] - hypo)
            t2 = t1 * xm[i]
            theta = theta + alpha * t2.transpose()

    return theta

def test_func1_batch(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    ym01 = np.where(ym > 0, ym, 0)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    alpha = 0.2
    hypo = np.zeros((mm, 1))
    t1 = np.zeros((mm, 1))
    t2 = np.zeros((mm, nn))
    t3 = np.zeros((1, nn))

    for j in range(0, max_iters):
        margins = xm * theta
        hypo = 1.0 / (1.0 + np.exp(-margins))
        t1 = ym01 - hypo
        t2 = np.multiply(xm, t1)
        t3 = np.sum(t2, axis=0)
        theta = theta + alpha * t3.transpose()

    #print hypo
    return theta

def test_func2_batch(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    alpha = 0.2
    hypo = np.zeros((mm, 1))
    t1 = np.zeros((mm, 1))
    t2 = np.zeros((mm, nn))
    t3 = np.zeros((1, nn))

    for j in range(0, max_iters):
        margins = xm * theta
        hypo = 2.0 / (1.0 + np.exp(-margins)) - 1.0
        t1 = ym - hypo
        t2 = np.multiply(xm, t1)
        t3 = np.sum(t2, axis=0)
        theta = theta + alpha * t3.transpose()

    return theta

# grad = 0.5 * sum((y-(2*h-1)).*x)
# H = -1 * sum((h*(1-h))xj*xk^t)
def test_func2_newton(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    ## newton's method to minimize function.
    for i in range(0, max_iters):
        margins = xm * theta
        hypo = 1.0 / (1 + np.exp(-margins))
        ll = ym - (hypo * 2 - 1)
        grad1 = 0.5 * (xm.transpose() * ll)
        H = -1.0 * (xm.transpose() * np.diag(np.array(np.multiply(hypo, (1-hypo))).flatten()) * xm)
        theta = theta - linalg.inv(H) * grad1

    return theta


# minimize (y - h(x))^2
def test_func_ls(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    ym01 = np.where(ym > 0, ym, 0)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.ones((nn, 1))

    # partial der tj = -2 * (y - g(theta'*x))*g(theta'*x)*(1-g(theta'x))*xj

    alpha = 0.05

    for j in range(0, max_iters):
        for i in range(0, mm):
            margins = xm[i] * theta
            hypo = 1.0 / (1.0 + np.exp(-margins))
            t1 = -2.0*(ym01[i] - hypo)*hypo*(1-hypo)
            t2 = t1 * xm[i]
            theta = theta - alpha * t2.transpose()

    return theta

def print_and_compare(X, Y, theta, l):
    t2 = X * theta
    hypoY = 1 / (1 + np.exp(-t2))
    compY = np.append(np.reshape(Y, (Y.size, 1)), hypoY, axis=1)
    ym01 = np.where(Y > 0, Y, 0)
    error = np.absolute(ym01 - hypoY)
    error = np.where(error > 0.5, 1, 0)
    num_error = np.count_nonzero(error)
    #print compY
    print l, ': ', num_error, ', ', theta

def plot_theta(x1, theta, color, l):
    theta0 = theta[0, 0]
    theta1 = theta[1, 0]
    theta2 = theta[2, 0]
    xls2 = -(theta0 / theta2) - (theta1 / theta2) * x1
    plt.plot(x1, xls2, color, label=l)

def main():
    x = np.loadtxt('logistic_x.txt')
    y = np.loadtxt('logistic_y.txt')

    t1 = np.ones((x.shape[0], 1))
    X = np.append(t1, x, axis=1)
    Y = y.reshape((y.size, 1))

    # theta = np.matrix([[-2.6205, 0.7604, 1.1719]]), # result from anser
    thetaN, ll = newton_opt(X, Y, 100)
    theta1 = test_func1(X, Y, 100)
    theta1B = test_func1_batch(X, Y, 100)
    thetaLS = test_func_ls(X, Y, 100)
    theta2B = test_func2_batch(X, Y, 100)
    theta2N = test_func2_newton(X, Y, 100)

    print_and_compare(X, Y, thetaN, 'newton')
    print_and_compare(X, Y, theta1, 'map 0-1')
    print_and_compare(X, Y, theta1B, 'map 0-1 batch')
    print_and_compare(X, Y, thetaLS, 'least square')
    print_and_compare(X, Y, theta2B, 'orig batch')
    print_and_compare(X, Y, theta2N, 'orig newton')

    mask0 = np.array(Y).flatten() <= 0
    mask1 = np.array(Y).flatten() > 0

    plt.plot(X[mask0, 1], X[mask0, 2], 'rx')
    plt.plot(X[mask1, 1], X[mask1, 2], 'bo')

    xmin1 = np.min(X[:, 1])
    xmax1 = np.max(X[:, 1])

    x1 = np.linspace(xmin1, xmax1)

    plot_theta(x1, thetaN, 'r', 'newton')
    plot_theta(x1, theta1, 'g', 'map 0-1')
    plot_theta(x1, theta1B, 'b', 'map 0-1 batch')
    plot_theta(x1, thetaLS, 'y', 'least square')
    plot_theta(x1, theta2B, 'c', 'orig batch')
    plot_theta(x1, theta2N, 'm', 'orig newton')

    plt.legend()
    plt.show()

main()