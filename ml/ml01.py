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

# minimize (y - h(x))^2
def test_func2(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    ym01 = np.where(ym > 0, ym, 0)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

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

# p = phi^((y+1)/2) * (1-phi)^((1-y)/2)
# E[y|x;theta] = 2*phi - 1
def test_func3(xa, ya, max_iters):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    theta = np.zeros((nn, 1))

    for j in range(0, max_iters):
        for i in range(0, mm):
            margins = np.dot(xm[i], theta)
            #print margins
            hypo = 2.0 / (1 + np.exp(-margins*2.0)) - 1.0
            t1 = (ym[i] - hypo)
            t2 = t1 * xm[i]
            alpha = 0.05
            if np.absolute(t1) > 1.0:
                alpha = 0.01
            theta = theta + alpha * t2.transpose()

    return theta

def print_and_compare(X, Y, theta):
    t2 = X * theta
    hypoY = 1 / (1 + np.exp(-t2))
    compY = np.append(np.reshape(Y, (Y.size, 1)), hypoY, axis=1)
    ym01 = np.where(Y > 0, Y, 0)
    error = np.absolute(ym01 - hypoY)
    error = np.where(error > 0.5, 1, 0)
    num_error = np.count_nonzero(error)
    print compY
    print num_error

def plot_theta(x1, theta, color):
    theta0 = theta[0, 0]
    theta1 = theta[1, 0]
    theta2 = theta[2, 0]
    xn2 = -(theta0 / theta2) - (theta1 / theta2) * x1
    plt.plot(x1, xn2, color)


def main():
    x = np.loadtxt('logistic_x.txt')
    y = np.loadtxt('logistic_y.txt')

    t1 = np.ones((x.shape[0], 1))
    X = np.append(t1, x, axis=1)
    Y = y.reshape((y.size, 1))

    # theta = np.matrix([[-2.6205, 0.7604, 1.1719]]), # result from anser
    thetaN, ll = newton_opt(X, Y, 20)
    theta1 = test_func1(X, Y, 20)
    theta2 = test_func2(X, Y, 20)
    theta3 = test_func3(X, Y, 20)

    print "thetaN: " , thetaN
    print "theta1: " , theta1
    print "theta2: " , theta2
    print "theta3: " , theta3

    #print_and_compare(X, Y, thetaN)
    #print_and_compare(X, Y, thetaLS)

    mask0 = np.array(Y).flatten() <= 0
    mask1 = np.array(Y).flatten() > 0

    plt.plot(X[mask0, 1], X[mask0, 2], 'rx')
    plt.plot(X[mask1, 1], X[mask1, 2], 'bo')

    xmin1 = np.min(X[:, 1])
    xmax1 = np.max(X[:, 1])

    x1 = np.linspace(xmin1, xmax1)

    plot_theta(x1, thetaN, 'b')
    plot_theta(x1, theta1, 'r')
    plot_theta(x1, theta2, 'g')
    plot_theta(x1, theta3, 'y')

    plt.show()

## test module
main()