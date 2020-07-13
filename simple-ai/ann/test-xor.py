
import matplotlib.pyplot as plt
from cs231n.classifiers.fc_net import *
from cs231n.data_utils import get_CIFAR10_data
from cs231n.gradient_check import eval_numerical_gradient, eval_numerical_gradient_array
from cs231n.solver import Solver

model = 0
solver = 0

if False:
    # TODO: Use a three-layer Net to overfit 50 training examples.

    # Load the (preprocessed) CIFAR10 data.

    data = get_CIFAR10_data()
    for k, v in data.items():
      print('%s: ' % k, v.shape)

    # TODO: Use a three-layer Net to overfit 50 training examples.

    num_train = 50
    small_data = {
        'X_train': data['X_train'][:num_train],
        'y_train': data['y_train'][:num_train],
        'X_val': data['X_val'][:num_train],
        'y_val': data['y_val'][:num_train],
    }

    weight_scale = 1e-2
    learning_rate = 1e-2  # was 1e-4
    model = FullyConnectedNet([100, 100],
                              weight_scale=weight_scale, dtype=np.float64)
    solver = Solver(model, small_data,
                    print_every=10, num_epochs=20, batch_size=25,
                    update_rule='sgd',
                    optim_config={
                        'learning_rate': learning_rate,
                    }
                    )
    solver.train()

else:

    X_train_2 = np.array([[1, 1],
                          [-1, -1],
                          [-1, 1],
                          [1, -1]])

    # Y_train_2 = np.array([[1, 0],
    #                       [1, 0],
    #                       [0, 1],
    #                       [0, 1]])

    Y_train_2 = np.array([1, 1, 0, 0])

    X_val_2 = X_train_2.copy()
    Y_val_2 = Y_train_2.copy()

    small_data_2 = {}
    small_data_2['X_train'] = X_train_2
    small_data_2['y_train'] = Y_train_2
    small_data_2['X_val'] = X_val_2
    small_data_2['y_val'] = Y_val_2

    # weight_scale = 1e-2
    weight_scale = 1.0
    # learning_rate = 1e-2 # was 1e-4
    learning_rate = 1.0

    model = FullyConnectedNet([2, 2], input_dim=2, num_classes=2,
              weight_scale=weight_scale, dtype=np.float64)

    solver = Solver(model, small_data_2,
                print_every=10, num_epochs=1000, batch_size=4,
                update_rule='sgd',
                optim_config={
                  'learning_rate': learning_rate,
                }
         )
    solver.train()

# for k, v in model.params.items():
#     print(model.params[k])

plt.plot(solver.loss_history, 'o')
plt.title('Training loss history')
plt.xlabel('Iteration')
plt.ylabel('Training loss')
plt.show()