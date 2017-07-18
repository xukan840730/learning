import numpy as np
import numpy.linalg as linalg
import matplotlib.pyplot as plt

# linear regression by normal equation
def lin_reg(xa, ya):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    t1 = xm.transpose() * xm
    t2 = linalg.inv(t1)
    t3 = xm.transpose()

    theta = t2 * t3 * ym
    return theta

# weighted linear regression
def weighted_lin_reg(xa, ya, tau):
    # wi = exp(-(x - xi)^2 / 2tau^2)
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    # raw x doesn't have the first column of ones
    raw_x = xm[:, 1:]

    thetaM = np.empty([nn, mm])

    # should be for each sample
    for i in range(0, mm):
        tt = -np.square(raw_x[i] - raw_x) / (2*np.square(tau))
        wi = np.exp(tt)
        Wi = np.diagflat(wi)

        t1 = xm.transpose() * Wi * xm
        t2 = linalg.inv(t1)
        t3 = xm.transpose()

        thetaI = t2 * t3 * Wi * ym
        thetaM[:, i] = thetaI.flatten()

    return thetaM

def main():
    raw_data = np.loadtxt('quasar_train.csv', delimiter=',')
    lambdas = raw_data[0, :]
    train_qso = raw_data[1:, :]

    train_data0 = train_qso[0, :]

    # pre process data
    t1 = np.ones((lambdas.shape[0], 1))
    x = lambdas.reshape(lambdas.size, 1)
    X = np.append(t1, x, axis=1)
    Y = train_data0.reshape(train_data0.size, 1)

    # linear regression:
    theta0 = lin_reg(X, Y)
    est_y_lin_reg = X * theta0

    # weighted linear regression:
    tau = 5.0
    theta_weighted = weighted_lin_reg(X, Y, tau)
    est_y_weighted = np.empty(Y.shape)

    for i in range(0, lambdas.size):
        est_y_weighted[i] = np.dot(X[i, :], theta_weighted[:, i])

    # plotting
    plt.plot(lambdas, train_data0, '+k', label='Raw Data')
    plt.plot(lambdas, est_y_lin_reg, 'r', label='Regression Line')
    plt.plot(lambdas, est_y_weighted, 'b', label='Weighted Regression')

    plt.legend()
    plt.show()

# test module
main()