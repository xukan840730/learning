# For the sake of this exercise, humans have two hidden states: happy (H) and sad (S),
# which the robot will need to deduce from five actions:
# cooking (CO), crying (CR), sleeping (SL), socializing (SO), and watching TV (W).

# From a series of experiments, the robot designers have determined that
# as people move from one task to another, sad people remain sad 80% of the time
# and happy people remain happy 90% of the time
# (the remainder of the time they switch from sad to happy or happy to sad).
# Initially, 60% of people are happy. That is, P(x0 = H) = 0.6.

# The designers have also determined that for happy and sad people, the activity breakdown is as follows:
# P(CO | S) = 10%, P(CO | H) = 30%
# P(CR | S) = 20%, O(CR | H) = 0%
# P(SL | S) = 40%, P(SL | H) = 30%
# P(SO | S) = 0%, P(SO | H) = 30%
# P(W | S) = 30%, P(W | H) = 10%

import numpy as np

# index 0 is sad, 1 is happy

# theta = p(xt | xt-1), transition probablity table
theta = np.array([[0.8, 0.2],
                  [0.1, 0.9]])
# phi = p(yt | xt), observation probablity table
phi = np.array([[0.1, 0.2, 0.4, 0, 0.3],
                [0.3, 0, 0.3, .3, 0.1]])

px0 = np.array([0.4, 0.6])

def HMM(px0, theta, phi, y):
    T = len(y)

    nx = theta.shape[0]
    px = np.zeros((T, nx))
    px_y_t_1 = px0 # P(xt| y1:t-1)

    for t in range(0, T):

        if t != 0:
            # prediction, P(xt | y1:t-1)
            for kind1 in range(0, nx):
                t_sum = 0

                for kind2 in range(0, nx):
                    t1 = theta[kind2][kind1]
                    t2 = px[t-1][kind2]
                    t_sum += t1 * t2

                px_y_t_1[kind1] = t_sum

        # bayes updates:
        for kind1 in range(0, nx):
            t1 = phi[kind1][y[t]] * px_y_t_1[kind1]
            t3 = 0
            for kind2 in range(0, nx):
                t3 += phi[kind2][y[t]] * px_y_t_1[kind2]

            px[t][kind1] = t1 / t3

    return px

px = HMM(px0, theta, phi, (3, 3, 0, 4, 2))
#px = HMM(px0, theta, phi, (3,0,))
print px
