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

# hidden markov model:
# lambda = { init-prob, trans-prob, emission-prob }
def HmmFwdBwd(prior_init_prob, prior_trans_prob, prior_emis_prob, action_seq):

    T = len(action_seq)

    if T == 0:
        return 0

    num_states = prior_emis_prob.shape[0]
    num_observ = prior_emis_prob.shape[1]

    action_seq_idx = []
    for act_name in action_seq:
        action_seq_idx.append(action_to_idx[act_name])

    # forward probability alpha_t(i) = P(O_1:t, qt = Si | lambda)
    fwd_probs = np.zeros((num_states, T))
    # backward probablity beta_t(i) = P(O_t+1:T, qt = Si | lambda)
    bwd_probs = np.zeros((num_states, T))

    # fwd_probs(i, 0) = init_prob(i) * emis_prob(i, O0)
    act_idx_0 = action_seq_idx[0]
    fwd_probs[:, 0] = prior_init_prob * prior_emis_prob[:, act_idx_0]

    # calculate fwd_probs(i, t), 1 <= t <= T - 1
    for t in range(1, T):
        act_idx = action_seq_idx[t]

        for istate in range(num_states):
            t1 = np.dot(fwd_probs[:, t-1], prior_trans_prob[:, istate])
            fwd_probs[istate][t] = t1 * prior_emis_prob[istate, act_idx]

    # bwd_probs(i, T-1) = 1.0
    bwd_probs[:, T-1] = 1.0

    # calculate bwd_probs(i, t),
    for t in range(T-2, -1, -1):
        t_plus_1 = t + 1
        act_idx_plus_1 = action_seq_idx[t_plus_1]
        for istate in range(num_states):
            ss = 0.0
            for jstate in range(num_states):
                s1 = prior_trans_prob[istate][jstate]
                s2 = prior_emis_prob[jstate][act_idx_plus_1]
                s3 = bwd_probs[jstate][t_plus_1]
                ss += s1 * s2 * s3
            bwd_probs[istate, t] = ss

    return fwd_probs, bwd_probs

def HmmTrain(prior_init_prob, prior_trans_prob, prior_emis_prob, train_seq):
    T = len(train_seq)
    num_states = prior_emis_prob.shape[0]
    num_actions = prior_emis_prob.shape[1]

    init_prob = prior_init_prob.copy()
    trans_prob = prior_trans_prob.copy()
    emis_prob = prior_emis_prob.copy()

    fwd_probs, bwd_probs = HmmFwdBwd(init_prob, trans_prob, emis_prob, train_seq)

    prob_train_seq = np.sum(fwd_probs[:, T-1])

    # sigma(i, j, t): P(qt = Si, qt+1 = Sj | O, lambda)
    sigma = np.zeros((num_states, num_states, T-1))
    for istate in range(num_states):
        for jstate in range(num_states):
            for t in range(T-1):
                act_idx = action_to_idx[train_seq[t+1]]
                t1 = emis_prob[jstate, act_idx]
                sigma[istate, jstate, t] = fwd_probs[istate, t] * trans_prob[istate, jstate] * t1 * bwd_probs[jstate, t+1] / prob_train_seq
    print("sigma:")
    print(sigma)

    # gamma(i, t) = P(qt = Si | O, lambda)
    gamma = np.zeros((num_states, T-1))
    for t in range(T-1):
        for istate in range(num_states):
            gamma[istate, t] = np.sum(sigma[istate, :, t])
    print("gamma:")
    print(gamma)

    new_init_prob = np.zeros(init_prob.shape)
    new_init_prob = gamma[:, 0]
    print("new-init-prob:")
    print(new_init_prob)

    new_trans_prob = np.zeros(trans_prob.shape)
    for istate in range(num_states):
        for jstate in range(num_states):
            new_trans_prob[istate, jstate] = np.sum(sigma[istate, jstate, :]) / np.sum(gamma[istate, :])
    print("new-trans-prob:")
    print(new_trans_prob)

    new_emis_prob = np.zeros(emis_prob.shape)
    for jstate in range(num_states):
        for kaction in range(num_actions):
            p = 0.0
            for t in range(T-1):
                act_idx = action_to_idx[train_seq[t]]
                if act_idx == kaction:
                    p += gamma[jstate, t]
            new_emis_prob[jstate, kaction] = p / np.sum(gamma[jstate, :])
    print(new_emis_prob)

num_states = len(hidden_states)
num_actions = len(actions)

HmmTrain(prior_init_prob, prior_trans_prob, prior_emis_prob, ("CO", "WTV", "CR", "CO", "SO"))


# for i0 in range(num_actions):
#     total_prob = 0
#     for i1 in range(num_actions):
#         for i2 in range(num_actions):
#             for i3 in range(num_actions):
#                 action_seq = (actions[i0], actions[i1], actions[i2], actions[i3])
#                 #print(action_seq)
#                 fwd_probs, bwd_probs = Hmm(prior_init_prob,
#                                            prior_trans_prob,
#                                            prior_emis_prob,
#                                            action_seq)
#                 ss = np.dot(bwd_probs[:, 0], prior_init_prob)
#                 total_prob += ss
#     print(total_prob)
