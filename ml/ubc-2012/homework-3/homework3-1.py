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

# theta = p(xt | xt_1), transition probablity table
theta = ((0.8, 0.2), (0.1, 0.9))
# phi = p(yt | xt), observation probablity table
phi = ((0.1, 0.2, 0.4, 0, 0.3),
       (0.3, 0, 0.3, .3, 0.1))

px0 = (0.4, 0.6)

def HMM(px0, theta, phi, y, T):
    print px0
    print theta
    print phi

HMM(px0, theta, phi, 1, 2)
