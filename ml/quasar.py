import numpy as np
import numpy.linalg as linalg
import matplotlib.pyplot as plt

# linear regression by normal equation
def lin_reg(xa, ya):
    xm = np.matrix(xa)
    ym = np.matrix(ya)

    t1 = xm.transpose() * xm
    t2 = xm.transpose() * ym

    theta = linalg.solve(t1, t2)
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
    estY = np.empty([mm, 1])

    # should be for each sample
    for i in range(0, mm):
        tt = -np.square(raw_x[i] - raw_x) / (2*np.square(tau))
        wi = np.exp(tt)
        Wi = np.diagflat(wi)

        t1 = xm.transpose() * Wi * xm
        t2 = xm.transpose() * Wi * ym

        thetaI = linalg.solve(t1, t2)
        thetaM[:, i] = thetaI.flatten()
        estY[i] = np.dot(xm) #unfinished

    return thetaM

def problem1(lambdas, y):
    # pre process data
    t1 = np.ones((lambdas.shape[0], 1))
    x = lambdas.reshape(lambdas.size, 1)
    X = np.append(t1, x, axis=1)
    Y = y.reshape(y.size, 1)

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
    plt.plot(lambdas, y, '+k', label='Raw Data')
    plt.plot(lambdas, est_y_lin_reg, 'r', label='Regression Line')
    plt.plot(lambdas, est_y_weighted, 'b', label='Weighted Regression')

    plt.legend()
    plt.show()

def problem2(lambdas, qso):
    smoothed = qso
    tau = 5

    mm = qso.shape[0]
    nn = qso.shape[1]
    t1 = np.ones((nn, 1))
    x = lambdas.reshape(nn, 1)
    X = np.append(t1, x, axis=1)

    for ii in range(0, mm):
        ytrain = qso[ii, :]
        ytrain = ytrain.reshape(ytrain.size, 1)
        theta = weighted_lin_reg(X, ytrain, tau)

        #ysmoothed = np.empty(ytrain.shape)
        #for jj in range(0, nn):
        #    ysmoothed[jj] = np.dot(X[jj, :], theta[:, jj])

        #smoothed[ii, :] = ysmoothed.transpose()
        print("done:", ii)

def main():
    raw_data = np.loadtxt('quasar_train.csv', delimiter=',')
    lambdas = raw_data[0, :]
    train_qso = raw_data[1:, :]

    train_data0 = train_qso[0, :]
    #problem1(lambdas, train_data0)

    problem2(lambdas, train_qso)

# test module
main()