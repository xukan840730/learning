
import math
import random
import numpy as np
import matplotlib.pyplot as plt

bptt_truncate = 5
min_clip_value = -10
max_clip_value = 10

# read samples file:
def read_data():

    file1 = open('gyro-samples-reduced.txt', 'r')

    group_data = list()
    groups = list()

    while True:

        # Get next line from file
        line = file1.readline()

        # if line is empty
        # end of file is reached
        if not line:
            break

        texts = line.split()
        if len(texts) == 0:

            if len(group_data) > 0:
                groups.append(group_data)
                group_data = list()

            continue

        # print(texts)
        # print("Line{}: {}".format(count, line.strip()))
        text_x = texts[2]
        text_z = texts[3]
        text_motor_0 = texts[4]
        text_motor_1 = texts[5]

        gyro_x_str = text_x.split(sep=':')[1].split(',')[0]
        gyro_x_f = float(gyro_x_str)

        gyro_z_str = text_z.split(sep=':')[1].split(',')[0]
        gyro_z_f = float(gyro_z_str)

        motor_1_str = text_motor_1.split(sep=':')[1]
        motor_1_f = float(motor_1_str)

        # data_sample = np.array([gyro_x_f, gyro_z_f, motor_1_f])
        # data_sample = np.array([gyro_x_f, motor_1_f])
        data_sample = np.array([gyro_x_f])
        group_data.append(data_sample)

    return groups

def generate_test_data(data_groups):
    # num_samples_per_data = 32
    # num_samples_per_data = 8
    num_samples_per_data = 6

    input = list()
    output = list()

    for data_group in data_groups:
        num_item_this_group = len(data_group) - num_samples_per_data
        for i in range(0, num_item_this_group, 1):
            num_elem = data_group[i].shape[0]
            single_data = np.zeros(num_samples_per_data * num_elem)
            for j in range(num_samples_per_data):
                single_data[j*num_elem:(j+1)*num_elem] = data_group[i + j]
            input.append(single_data)

            output.append(data_group[i + num_samples_per_data][0])

    data_x = np.zeros([len(input), input[0].size])

    i = 0
    for l in input:
        data_x[i, :] = l
        i += 1

    data_y = np.zeros([len(output), 1])

    i = 0
    for l in output:
        data_y[i, 0] = l
        i += 1

    return data_x, data_y

def generate_test_data2():

    data_groups = read_data()

    data_x, data_y = generate_test_data(data_groups)
    data_x = np.expand_dims(data_x, axis=2)
    data_y = np.expand_dims(data_y, axis=1)

    num_data = data_x.shape[0]

    # X = data_x[0:num_data - num_val]
    # Y = data_y[0:num_data - num_val]

    X = data_x
    Y = data_y

    num_small = 400
    num_val = 20

    small_mask = np.random.choice(num_data, num_small)
    X_small = X[small_mask]
    Y_small = Y[small_mask]

    val_mask = np.random.choice(num_data, num_val)
    X_val = X[val_mask]
    Y_val = Y[val_mask]

    data_dict = {}
    data_dict['X'] = X_small
    data_dict['Y'] = Y_small
    data_dict['X_val'] = X_val
    data_dict['Y_val'] = Y_val

    return data_dict

data_dict = generate_test_data2()

def plot_single_data(x, y):
    t = np.linspace(0.0, 1.0, x.shape[0])
    plt.scatter(t, x)
    plt.plot(t, x, 'r')
    step = 1.0 / x.shape[0]
    ty = 1.0 + step
    ty_array = np.zeros(y.shape)
    ty_array[0] = ty
    plt.scatter(ty_array, y)
    plt.show()

# if True:
#     for i in range(5):
#         rand_idx = random.randint(0, X_small.shape[0])
#         plot_single_data(X_small[rand_idx], Y_small[rand_idx])

# end of data generation.

def sigmoid(x):
    return 1 / (1 + np.exp(-x))

class RNN2(object):
    def __init__(self, hidden_dim, output_dim, T, lr):
        self.hidden_dim = hidden_dim
        self.output_dim = output_dim
        self.T = T
        self.learning_rate = lr
        self.U = np.random.uniform(0, 1, (self.hidden_dim, self.T))
        self.W = np.random.uniform(0, 1, (self.hidden_dim, self.hidden_dim))
        self.V = np.random.uniform(0, 1, (self.output_dim, self.hidden_dim))

    def forward_pass(self, x):
        prev_s = np.zeros((self.hidden_dim,
                           1))  # here, prev-s is the value of the previous activation of hidden layer; which is initialized as all zeroes
        for t in range(self.T):
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
        prev_s = np.zeros((self.hidden_dim, 1))
        # Forward pass
        for t in range(self.T):
            mulu = np.dot(self.U, x)
            mulw = np.dot(self.W, prev_s)
            add = mulw + mulu
            s = sigmoid(add)
            mulv = np.dot(self.V, s)
            prev_s = s

        return mulv

    def train(self, x, y):
        layers = []
        prev_s = np.zeros((self.hidden_dim, 1))
        dU = np.zeros(self.U.shape)
        dV = np.zeros(self.V.shape)
        dW = np.zeros(self.W.shape)

        dU_t = np.zeros(self.U.shape)
        # dV_t = np.zeros(self.V.shape)
        dW_t = np.zeros(self.W.shape)

        # dU_i = np.zeros(self.U.shape)
        # dW_i = np.zeros(self.W.shape)

        # forward pass
        for t in range(self.T):
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
        for t in range(self.T):
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
        self.U -= self.learning_rate * dU
        self.V -= self.learning_rate * dV
        self.W -= self.learning_rate * dW

def main(dict_d):

    X = dict_d['X']
    Y = dict_d['Y']
    X_val = dict_d['X_val']
    Y_val = dict_d['Y_val']

    # learning_rate = 0.0001
    learning_rate = 0.0001
    nepoch = 100

    assert(X.shape[1] == X_val.shape[1])
    T = X.shape[1]  # length of sequence

    # hidden_dim = 100
    hidden_dim = 128 * 2
    output_dim = 1

    model = RNN2(hidden_dim=hidden_dim, output_dim=output_dim, T=T, lr=learning_rate)

    best_model_U = None
    best_model_V = None
    best_model_W = None
    best_loss = None

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
        loss = loss / float(Y.shape[0])

        # check loss on val
        val_loss = 0.0
        for i in range(Y_val.shape[0]):
            x, y = X_val[i], Y_val[i]
            mulv = model.forward_pass(x)
            loss_per_record = (y - mulv) ** 2 / 2
            val_loss += loss_per_record
        val_loss = val_loss / float(Y_val.shape[0])

        print('Epoch: ', epoch + 1, ', Loss: ', loss, ', Val Loss ', val_loss)

        if best_loss == None or val_loss < best_loss:
            best_loss = val_loss
            best_model_U = model.U.copy()
            best_model_V = model.V.copy()
            best_model_W = model.W.copy()

        # train model
        for i in range(Y.shape[0]):
            x, y = X[i], Y[i]
            model.train(x, y)

    model.U = best_model_U
    model.V = best_model_V
    model.W = best_model_W

    # preds = []
    # for i in range(Y.shape[0]):
    #     x = X[i]
    #     mulv = model.predict(x)
    #     preds.append(mulv)
    #
    # preds = np.array(preds)
    #
    # plt.plot(preds[:, 0, 0], 'g')
    # plt.plot(Y[:, 0], 'r')
    # plt.show()

    print(Y_val.shape)

    preds = []
    for i in range(Y_val.shape[0]):
        x, y = X_val[i], Y_val[i]
        mulv = model.predict(x)
        print("mulv: ", mulv, ", y: ", y)
        preds.append(mulv)

    preds = np.array(preds)

    plt.plot(preds[:, 0, 0], 'g')
    plt.plot(Y_val[:, 0], 'r')
    plt.show()


# main(X, Y, X_val, Y_val)
main(data_dict)