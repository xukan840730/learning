
import matplotlib.pyplot as plt
from cs231n.classifiers.fc_net import *
# from cs231n.data_utils import get_CIFAR10_data
from cs231n.gradient_check import eval_numerical_gradient, eval_numerical_gradient_array
from cs231n.solver import Solver

X_train_2 = np.array([[1, 1],
                      [-1, -1],
                      [-1, 1],
                      [1, -1],
                      [0, 2],
                      [2, 0],
                      [0, -2],
                      [-2, 0],
                      [0, 0],
                      [-2, 2],
                      [2, -2]])

# Y_train_2 = np.array([[1, 0],
#                       [1, 0],
#                       [0, 1],
#                       [0, 1]])

Y_train_2 = np.array([1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0])

X_val_2 = X_train_2.copy()
Y_val_2 = Y_train_2.copy()

small_data_2 = {}
small_data_2['X_train'] = X_train_2
small_data_2['y_train'] = Y_train_2
small_data_2['X_val'] = X_val_2
small_data_2['y_val'] = Y_val_2

# weight_scale = 1e-2
weight_scale = 0.1
# learning_rate = 1e-2 # was 1e-4
learning_rate = 0.1

model = FullyConnectedNet([2, 2], input_dim=2, num_classes=2,
          weight_scale=weight_scale, dtype=np.float64)

solver = Solver(model, small_data_2,
            print_every=10, num_epochs=1000, batch_size=8,
            update_rule='sgd',
            optim_config={
              'learning_rate': learning_rate,
            }
     )
solver.train()

# for k, v in model.params.items():
#     print(model.params[k])

test_data_x = np.linspace(-1.5, 1.5, 20)
test_data_y = np.linspace(-1.5, 1.5, 20)
test_data = np.zeros([test_data_x.size * test_data_y.size, 2])

for ix in range(test_data_x.size):
    for iy in range(test_data_y.size):
        test_data[ix * test_data_y.size + iy] = np.array([test_data_x[ix], test_data_y[iy]])

test_score = model.loss(test_data)
test_category = np.argmax(test_score, axis=1)

neg_data = test_data[test_category == 0]
pos_data = test_data[test_category == 1]
# print(neg_data.shape)
# print(pos_data.shape)
neg_data_x = np.zeros(neg_data.shape[0])
neg_data_y = np.zeros(neg_data.shape[0])
for i in range(neg_data.shape[0]):
    neg_data_x[i] = neg_data[i, 0]
    neg_data_y[i] = neg_data[i, 1]

pos_data_x = np.zeros(pos_data.shape[0])
pos_data_y = np.zeros(pos_data.shape[0])
for i in range(pos_data.shape[0]):
    pos_data_x[i] = pos_data[i, 0]
    pos_data_y[i] = pos_data[i, 1]

plt.scatter(neg_data_x, neg_data_y, c='red')
plt.scatter(pos_data_x, pos_data_y, c='blue')

# plt.plot(solver.loss_history, 'o')
# plt.title('Training loss history')
# plt.xlabel('Iteration')
# plt.ylabel('Training loss')
plt.show()