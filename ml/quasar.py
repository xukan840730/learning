import numpy as np
import numpy.linalg as linalg
import matplotlib.pyplot as plt

# linear regression by normal equation
def linreg(xa, ya):
    # pre process data, set first column to ones
    t1 = np.ones((xa.shape[0], 1))
    x = xa.reshape(xa.size, 1)
    X = np.append(t1, x, axis=1)

    xm = np.matrix(X)
    ym = np.matrix(ya)

    t1 = xm.transpose() * xm
    t2 = xm.transpose() * ym

    theta = linalg.solve(t1, t2)
    est_y = X * theta
    return est_y, theta

# weighted linear regression
def local_weighted_linreg(xa, ya, tau):
    # pre process data, set first column to ones
    t1 = np.ones((xa.shape[0], 1))
    x = xa.reshape(xa.size, 1)
    X = np.append(t1, x, axis=1)

    # wi = exp(-(x - xi)^2 / 2tau^2)
    xm = np.matrix(X)
    ym = np.matrix(ya)

    mm = xm.shape[0]
    nn = xm.shape[1]

    assert mm == ym.shape[0]

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
        estY[i] = np.dot(xm[i, :], thetaI)

    return estY, thetaM

def problem1(lambdas, y):
    Y = y.reshape(y.size, 1)

    # linear regression:
    est_y_lr, theta_lr = linreg(lambdas, Y)

    # weighted linear regression:
    tau = 5.0
    est_y_weighted, theta_weighted = local_weighted_linreg(lambdas, Y, tau)

    # plotting
    plt.plot(lambdas, y, '+k', label='Raw Data')
    plt.plot(lambdas, est_y_lr, 'r', label='Regression Line')
    plt.plot(lambdas, est_y_weighted, 'b', label='Weighted Regression')

def problem2(lambdas, qso):
    smoothed = qso
    tau = 5

    num_samples = qso.shape[0]
    num_features = qso.shape[1]

    for ii in range(0, num_samples):
        ytrain = qso[ii, :]
        ytrain = ytrain.reshape(ytrain.size, 1)
        ysmoothed, theta = local_weighted_linreg(lambdas, ytrain, tau)
        smoothed[ii, :] = ysmoothed.transpose()
        print("sample:", ii, " is done!")

    # plotting
    for ii in range(0, 10):
        l = str(ii)
        plt.plot(lambdas, smoothed[ii, :], label=l)

def main():
    raw_data = np.loadtxt('quasar_train.csv', delimiter=',')
    lambdas = raw_data[0, :]
    train_qso = raw_data[1:, :]

    train_data0 = train_qso[0, :]
    problem1(lambdas, train_data0)
    problem2(lambdas, train_qso)

    plt.legend()
    plt.show()

# test module
main()