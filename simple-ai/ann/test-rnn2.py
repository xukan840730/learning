import math
import numpy as np
import matplotlib.pyplot as plt

def generate_data():
    sin_wave = np.array([math.sin(x) for x in np.arange(200)])
    # plt.plot(sin_wave[:50])
    # plt.show()

    X = []
    Y = []

    seq_len = 50
    num_records = len(sin_wave) - seq_len

    for i in range(num_records - 50):
        X.append(sin_wave[i:i + seq_len])
        Y.append(sin_wave[i + seq_len])

    X = np.array(X)
    X = np.expand_dims(X, axis=2)

    Y = np.array(Y)
    Y = np.expand_dims(Y, axis=1)

    print(X.shape, Y.shape)

    X_val = []
    Y_val = []

    for i in range(num_records - 50, num_records):
        X_val.append(sin_wave[i:i + seq_len])
        Y_val.append(sin_wave[i + seq_len])

    X_val = np.array(X_val)
    X_val = np.expand_dims(X_val, axis=2)

    Y_val = np.array(Y_val)
    Y_val = np.expand_dims(Y_val, axis=1)

    print(X_val.shape, Y_val.shape)

    return X, Y, X_val, Y_val

X, Y, X_val, Y_val = generate_data()

learning_rate = 0.0001
nepoch = 25
T = 50                   # length of sequence
hidden_dim = 100
output_dim = 1

bptt_truncate = 5
min_clip_value = -10
max_clip_value = 10

def sigmoid(x):
    return 1 / (1 + np.exp(-x))

class RNN2(object):
    def __init__(self):
        self.U = np.random.uniform(0, 1, (hidden_dim, T))
        self.W = np.random.uniform(0, 1, (hidden_dim, hidden_dim))
        self.V = np.random.uniform(0, 1, (output_dim, hidden_dim))

    def forward_pass(self, x):
        prev_s = np.zeros((hidden_dim,
                           1))  # here, prev-s is the value of the previous activation of hidden layer; which is initialized as all zeroes
        for t in range(T):
            new_input = np.zeros(x.shape)  # we then do a forward pass for every timestep in the sequence
            new_input[t] = x[t]  # for this, we define a single input for that timestep
            mulu = np.dot(self.U, new_input)
            mulw = np.dot(self.W, prev_s)
            add = mulw + mulu
            s = sigmoid(add)
            mulv = np.dot(self.V, s)
            prev_s = s

        return mulv

    def predict(self, x):
        prev_s = np.zeros((hidden_dim, 1))
        # Forward pass
        for t in range(T):
            mulu = np.dot(model.U, x)
            mulw = np.dot(model.W, prev_s)
            add = mulw + mulu
            s = sigmoid(add)
            mulv = np.dot(model.V, s)
            prev_s = s

        return mulv

    def train(self, x, y):
        layers = []
        prev_s = np.zeros((hidden_dim, 1))
        dU = np.zeros(self.U.shape)
        dV = np.zeros(self.V.shape)
        dW = np.zeros(self.W.shape)

        dU_t = np.zeros(self.U.shape)
        dV_t = np.zeros(self.V.shape)
        dW_t = np.zeros(self.W.shape)

        dU_i = np.zeros(self.U.shape)
        dW_i = np.zeros(self.W.shape)

        # forward pass
        for t in range(T):
            new_input = np.zeros(x.shape)
            new_input[t] = x[t]
            mulu = np.dot(self.U, new_input)
            mulw = np.dot(self.W, prev_s)
            add = mulw + mulu
            s = sigmoid(add)
            mulv = np.dot(self.V, s)
            layers.append({'s': s, 'prev_s': prev_s})
            prev_s = s

        # derivative of pred
        dmulv = (mulv - y)

        # backward pass
        for t in range(T):
            dV_t = np.dot(dmulv, np.transpose(layers[t]['s']))
            dsv = np.dot(np.transpose(self.V), dmulv)

            ds = dsv
            dadd = add * (1 - add) * ds

            dmulw = dadd * np.ones_like(mulw)

            dprev_s = np.dot(np.transpose(self.W), dmulw)

            for i in range(t - 1, max(-1, t - bptt_truncate - 1), -1):
                ds = dsv + dprev_s
                dadd = add * (1 - add) * ds

                dmulw = dadd * np.ones_like(mulw)
                dmulu = dadd * np.ones_like(mulu)

                dW_i = np.dot(self.W, layers[t]['prev_s'])
                dprev_s = np.dot(np.transpose(self.W), dmulw)

                new_input = np.zeros(x.shape)
                new_input[t] = x[t]
                dU_i = np.dot(self.U, new_input)
                dx = np.dot(np.transpose(self.U), dmulu)

                dU_t += dU_i
                dW_t += dW_i

            dV += dV_t
            dU += dU_t
            dW += dW_t

            if dU.max() > max_clip_value:
                dU[dU > max_clip_value] = max_clip_value
            if dV.max() > max_clip_value:
                dV[dV > max_clip_value] = max_clip_value
            if dW.max() > max_clip_value:
                dW[dW > max_clip_value] = max_clip_value

            if dU.min() < min_clip_value:
                dU[dU < min_clip_value] = min_clip_value
            if dV.min() < min_clip_value:
                dV[dV < min_clip_value] = min_clip_value
            if dW.min() < min_clip_value:
                dW[dW < min_clip_value] = min_clip_value

        # update
        self.U -= learning_rate * dU
        self.V -= learning_rate * dV
        self.W -= learning_rate * dW


model = RNN2()

for epoch in range(nepoch):
    # check loss on train
    loss = 0.0

    # do a forward pass to get prediction
    for i in range(Y.shape[0]):
        x, y = X[i], Y[i]  # get input, output values of each record
        mulv = model.forward_pass(x)

        # calculate error
        loss_per_record = (y - mulv) ** 2 / 2
        loss += loss_per_record
    loss = loss / float(y.shape[0])

    # check loss on val
    val_loss = 0.0
    for i in range(Y_val.shape[0]):
        x, y = X_val[i], Y_val[i]
        mulv = model.forward_pass(x)
        loss_per_record = (y - mulv) ** 2 / 2
        val_loss += loss_per_record
    val_loss = val_loss / float(y.shape[0])

    print('Epoch: ', epoch + 1, ', Loss: ', loss, ', Val Loss: ', val_loss)

    # train model
    for i in range(Y.shape[0]):
        x, y = X[i], Y[i]
        model.train(x, y)

preds = []
for i in range(Y.shape[0]):
    x = X[i]
    mulv = model.predict(x)
    preds.append(mulv)

preds = np.array(preds)

plt.plot(preds[:, 0, 0], 'g')
plt.plot(Y[:, 0], 'r')
plt.show()

preds = []
for i in range(Y_val.shape[0]):
    x = X_val[i]
    mulv = model.predict(x)
    preds.append(mulv)

preds = np.array(preds)

plt.plot(preds[:, 0, 0], 'g')
plt.plot(Y_val[:, 0], 'r')
plt.show()