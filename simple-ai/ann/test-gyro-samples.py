
import numpy as np
import matplotlib.pyplot as plt
from cs231n.classifiers.fc_net import *
# from cs231n.data_utils import get_CIFAR10_data
from cs231n.gradient_check import eval_numerical_gradient, eval_numerical_gradient_array
from cs231n.solver import Solver

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
        data_sample = np.array([gyro_x_f, motor_1_f])
        group_data.append(data_sample)

    return groups

def generate_test_data(data_groups):
    num_samples_per_data = 8

    input = list()
    output = list()

    for data_group in data_groups:
        num_iter = len(data_group) - num_samples_per_data
        for i in range(0, num_iter, num_samples_per_data):
            num_elem =  data_group[i].shape[0]
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

data_groups = read_data()

data_x, data_y = generate_test_data(data_groups)

X_val_2 = data_x.copy()
Y_val_2 = data_y.copy()

small_data_2 = {}
small_data_2['X_train'] = data_x
small_data_2['y_train'] = data_y
small_data_2['X_val'] = X_val_2
small_data_2['y_val'] = Y_val_2

# weight_scale = 1e-2
weight_scale = 0.1
# learning_rate = 1e-2 # was 1e-4
learning_rate = 0.001

model = FullyConnectedNetSqrErr([640, 640, 640, 640, 640, 8], input_dim=16, num_classes=1,
          weight_scale=weight_scale, dtype=np.float64)

solver = Solver(model, small_data_2,
            print_every=100, num_epochs=1000, batch_size=100,
            update_rule='sgd',
            optim_config={
              'learning_rate': learning_rate,
            },
            use_acc_2=True,
            lr_decay=0.9999,
     )
solver.train()

# print(solver.best_params)

test_data_x_256 = data_x[256:258]
test_data_y_256 = model.loss(test_data_x_256)
val_data_y_256 = data_y[256:258]
print(test_data_y_256 - val_data_y_256)

plt.plot(solver.loss_history, 'o')
plt.title('Training loss history')
plt.xlabel('Iteration')
plt.ylabel('Training loss')
plt.show()