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

def smooth_qso(lambdas, qso, tau):
    smoothed = np.empty(qso.shape)

    num_samples = qso.shape[0]
    num_features = qso.shape[1]

    for ii in range(0, num_samples):
        ytrain = qso[ii, :]
        ytrain = ytrain.reshape(ytrain.size, 1)
        ysmoothed, theta = local_weighted_linreg(lambdas, ytrain, tau)
        smoothed[ii, :] = ysmoothed.transpose()
        print("sample:", ii, " is done!")

    return smoothed

def ker(t):
    tt = 1 - t
    idx = tt < 0.0
    tt[idx] = 0.0
    return tt

def estimate_left_qso(lambdas, smoothed_qso):
    rt_idx = lambdas >= 1300
    lt_idx = lambdas < 1200
    qso_right = smoothed_qso[:, rt_idx]
    qso_left = smoothed_qso[:, lt_idx]

    mm = smoothed_qso.shape[0]

    k = 3

    est_qso_left = np.empty(qso_left.shape)

    # estimate left qso
    for jj in range(0, mm):
        qso_diff = qso_right - qso_right[jj, :]
        metric_d = np.sum(qso_diff * qso_diff, axis=1)
        h = np.max(metric_d)

        sorted_idx = np.argsort(metric_d)
        #sorted_right = qso_right[sorted_idx]
        sorted_left = qso_left[sorted_idx]
        #neighb_right = sorted_right[0:k, :]
        neighb_left = sorted_left[0:k, :]
        sorted_diff = metric_d[sorted_idx]
        neighb_diff = sorted_diff[0:k]

        ker_diff = ker(neighb_diff / h)
        denom = np.sum(ker_diff)

        t2 = np.empty(neighb_left.shape)
        for qq in range(0, ker_diff.size):
            t2[qq, :] = neighb_left[qq, :] * ker_diff[qq]

        t3 = np.sum(t2, axis=0)
        f_left_hat = t3 / denom
        #assert f_left_hat.shape[1] == est_qso_left.shape[1]
        est_qso_left[jj, :] = f_left_hat

    return qso_left, est_qso_left

def calc_error(lambdas, qso_left, est_qso_left):
    # calculate error
    qso_error = qso_left - est_qso_left
    t2 = qso_error * qso_error
    metric_error = np.sum(t2, axis=1)
    print("average error:", np.average(metric_error))

def plot_est_left(lambdas, smoothed_qso, est_qso_left, figure_idx, plot_idx):
    plt.figure(figure_idx)
    plt.plot(lambdas, smoothed_qso[plot_idx, :], '+k')
    plt.plot(lambdas[0:est_qso_left.shape[1]], est_qso_left[plot_idx, :].flatten(), 'r')
    print("plot-done")

def main():
    raw_data_train = np.loadtxt('quasar_train.csv', delimiter=',')
    lambdas_train = raw_data_train[0, :]
    train_qso = raw_data_train[1:, :]

    raw_data_test = np.loadtxt('quasar_test.csv', delimiter=',')
    lambdas_test = raw_data_test[0, :]
    test_qso = raw_data_test[1:, :]

    #train_data0 = train_qso[0, :]
    #problem1(lambdas, train_data0)

    tau = 5
    smoothed_train = smooth_qso(lambdas_train, train_qso, tau)
    smoothed_test = smooth_qso(lambdas_test, test_qso, tau)

    qso_train_left, est_train_left = estimate_left_qso(lambdas_train, smoothed_train)
    qso_test_left, est_test_left = estimate_left_qso(lambdas_test, smoothed_test)
    calc_error(lambdas_train, qso_train_left, est_train_left)
    calc_error(lambdas_test, qso_test_left, est_test_left)

    # 1st figure is sample 0
    plot_est_left(lambdas_test, smoothed_test, est_test_left, 1, 0)
    # 2st figure is sample 5
    plot_est_left(lambdas_test, smoothed_test, est_test_left, 2, 5)

    #plt.legend()
    plt.show()

# test module
main()