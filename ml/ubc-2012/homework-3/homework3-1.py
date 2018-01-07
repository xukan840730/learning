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

# theta = p(xt | xt-1), transition probablity table
theta = np.array([[0.8, 0.2],
                  [0.1, 0.9]])
# phi = p(yt | xt), observation probablity table
phi = np.array([[0.1, 0.2, 0.4, 0, 0.3],
                [0.3, 0, 0.3, 0.3, 0.1]])

action_to_idx = {}
for idx, action in enumerate(actions):
    action_to_idx[action] = idx

px0 = np.array([0.4, 0.6])

def HMM(px0, trans_prob, obser_prob, actions):
    T = len(actions)

    nx = trans_prob.shape[0]
    ny = obser_prob.shape[1]
    px = np.zeros((nx, T))
    px_y_t_1 = px0.copy() # P(xt| y1:t-1)

    max_prob = np.zeros((nx, T))
    max_path = np.zeros((nx, T), dtype=int)

    for t in range(0, T):
        action_name = actions[t]
        act_idx = action_to_idx[action_name]
        assert (act_idx >= 0 and act_idx < ny)

        # prediction, P(xt | y1:t-1) = sum over xt-1 [ P(xt | xt-1) * P(xt-1 | y1:t-1) ]
        if t != 0:
            for kind1 in range(0, nx):
                px_y_t_1[kind1] = np.dot(trans_prob[:, kind1], px[:, t-1])

        # bayes updates: P(xt | y1:t) = [P(yt | xt) * P(xt | y1:t-1)] / [sum over xt P(yt | xt) * P(xt | y1:t-1)]
        for kind1 in range(0, nx):
            t1 = obser_prob[kind1][act_idx] * px_y_t_1[kind1]
            t4 = np.dot(obser_prob[:, act_idx], px_y_t_1)
            px[kind1][t] = t1 / t4

        # calculate the max probability and path
        if t == 0:
            for ix in range(0, nx):
                max_prob[ix][t] = obser_prob[ix][act_idx] * px0[ix]
                max_path[ix][t] = ix
        else:
            for ix in range(0, nx):
                new_max_prob = np.zeros((nx))
                for ipath in range(0, nx): # iterate previous max_path
                    t1 = obser_prob[ix][act_idx]
                    t2 = trans_prob[ipath][ix]
                    t3 = max_prob[ipath][t-1]
                    new_max_prob[ipath] = t1 * t2 * t3
                max_prob[ix][t] = np.max(new_max_prob)
                max_path[ix][t] = np.argmax(new_max_prob)

    return px, max_prob, max_path

#px, max_prob, max_path = HMM(px0, theta, phi, ("SO", "SO", "CO", "WTV", "SL"))
px, max_prob, max_path = HMM(px0, theta, phi, ("CO", "WTV", "CR", "CO", "SO"))
print(px)
print(max_prob)
print(max_path)

final_path_idx = np.argmax(max_prob[:,max_prob.shape[1]-1])
final_path = max_path[final_path_idx, :]
print(final_path)
final_path_states = []
for i in final_path:
    final_path_states.append(hidden_states[i])
print(final_path_states)