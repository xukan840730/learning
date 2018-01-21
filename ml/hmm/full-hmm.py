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
# P(WTV | S) = 30%, P(W | H) = 10%

import numpy as np
import math as math

# index 0 is sad, 1 is happy
hidden_states = ("sad", "happy")
actions = ("CO", "CR", "SL", "SO", "WTV")

# prior_trans_prob = P(qt=Si | qt-1=Sj), prior transition probability matrix
prior_trans_prob = np.array([[0.8, 0.2],
                             [0.1, 0.9]])

# prior_emis_prob = P(Ot | qt), prior emission probability matrix
prior_emis_prob = np.array([[0.1, 0.2, 0.4, 0, 0.3],
                            [0.3, 0, 0.3, 0.3, 0.1]])

action_to_idx = {}
for idx, action in enumerate(actions):
    action_to_idx[action] = idx

# prior_init_prob = P(q1 = Si), prior initial probability matrix
prior_init_prob = np.array([0.4, 0.6])

def full_hmm:
    return 0

