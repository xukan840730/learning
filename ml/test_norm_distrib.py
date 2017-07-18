import numpy as np
import matplotlib.pyplot as plt

def plot_normal_dist(mu, sigma, c):
    s = np.random.normal(mu, sigma, 1000)

    count, bins, ignored = plt.hist(s, 30, normed=True)
    #t = 1 / (sigma * np.sqrt(2 * np.pi)) * np.exp(- (bins - mu) ** 2 / (2 * sigma ** 2))
    #plt.plot(t, linewidth=2, color=c)

    plt.plot(bins, 1 / (sigma * np.sqrt(2 * np.pi)) *
            np.exp(- (bins - mu) ** 2 / (2 * sigma ** 2)),
            linewidth = 2, color = 'r')

def p_normal(mean, stdd, x):
    p = 1 / (stdd * np.sqrt(2 * np.pi)) * np.exp(- (x - mean) ** 2 / (2 * stdd ** 2))
    return p

#plot_normal_dist(0, 0.1, 'r')
#plot_normal_dist(0.1, 0.1, 'b')

x = np.linspace(-1.5, 2.5, 100) # samples
num_x = x.size

px_given_y0 = p_normal(0, 0.5, x) # P(x|y=0) distribution
px_given_y1 = p_normal(1, 0.5, x) # P(x|y=1) distribution

plt.plot(x, px_given_y0, label='P(x|y=0)')
plt.plot(x, px_given_y1, label='P(x|y=1)')

py1 = 0.5 # P(y=1)
py0 = 1 - py1 # P(y=0)

# P(x) = P(y=0)*P(x|y=0) + P(y=1)*P(x|y=1)
px = py0 * px_given_y0 + py1 * px_given_y1

py1_given_x = py1 * px_given_y1 / px

plt.plot(x, py1_given_x, label='P(y=1|x)')

# the other way of calculating P(y=1|x) is:
# P(y=1|x) = P(y=1)*P(x|y=1)/P(x)
# P(y=0|x) = P(y=0)*P(x|y=0)/P(x)
# we get P(y=1|x)/P(y=0|x) = [P(y=1)*P(x|y=1)] / [P(y=0)*P(x|y=0)], and right side is known.
# also P(y=1|x) + P(y=0|x) = 1, we can get both P(y=1|x) and P(y=0|x)
cons = (py1 * px_given_y1) / (py0 * px_given_y0)
py0_given_x_2 = 1 / (1 + cons)
py1_given_x_2 = 1 - py0_given_x_2

plt.plot(x, py1_given_x_2, 'x', label='P(y=1|x) second method')

plt.legend()
plt.show()