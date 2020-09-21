from mlxtend.data import loadlocal_mnist
from fastai.vision.all import *

train_raw_x, train_raw_y = loadlocal_mnist(images_path='train-images.idx3-ubyte', labels_path='train-labels.idx1-ubyte')
train_tensor_x = tensor(train_raw_x).view(-1, 28*28).float() / 255
train_tensor_y = tensor(train_raw_y).unsqueeze(1)

print(train_tensor_x.shape, train_tensor_y.shape)

valid_raw_x, valid_raw_y = loadlocal_mnist(images_path='t10k-images.idx3-ubyte', labels_path='t10k-labels.idx1-ubyte')
valid_tensor_x = tensor(valid_raw_x).view(-1, 28*28).float() / 255
valid_tensor_y = tensor(valid_raw_y).unsqueeze(1)

print(valid_tensor_x.shape, valid_tensor_y.shape)

def valid_tensor(t):
    if (t!=t).any():
        return False
    else:
        return True

def mnist_loss(predictions, targets):
    predictions = predictions.sigmoid()
    targets_y = torch.zeros(predictions.shape)
    for i in range(predictions.shape[0]):
        targets_y[i][targets[i].item()] = 1.0
    return torch.where(targets==1, 1-predictions, predictions).mean()

def mnist_loss_2(predictions, targets):
    # return torch.where(targets==1, 1-predictions, predictions).mean()
    return (predictions - targets).square().mean().sqrt()

def mnist_loss_3(predictions, targets):
    predictions = predictions.sigmoid()

    # to prevent log(0) happens
    predictions = torch.clamp(predictions, 0.000001, 0.999999)
    targets_y = torch.zeros(predictions.shape)
    for i in range(predictions.shape[0]):
        targets_y[i][targets[i].item()] = 1.0

    # use cross-entropy

    # pred0 = predictions[0]
    # targets0 = targets_y[0]
    # print(pred0.shape, targets0.shape)
    #
    # tt = -(targets0 * torch.log(pred0) + (1-targets0)*torch.log(1-pred0))
    # loss0 = tt.sum()

    part1 = targets_y * predictions.log()
    assert(valid_tensor(part1))
    part2 = (1-targets_y) * (1-predictions).log()
    if not valid_tensor(part2):
        print("aaa")
    tt1 = -(part1 + part2)
    assert(valid_tensor(tt1))
    tt2 = tt1.sum(dim=1)
    assert(valid_tensor(tt2))
    loss = tt2.mean()
    assert(valid_tensor(loss))
    return loss

    # test_x = predictions[0:2]
    # test_y = targets_y[0:2]
    #
    # part1 = test_y * torch.log(test_x)
    # part2 = (1 - test_y) * torch.log(1-test_x)
    #
    # tt1 = -(part1 + part2)
    # tt2 = torch.sum(tt1, dim=1)
    # loss = tt2.mean()
    # return loss

def calc_grad(xb, yb, model):
    preds = model(xb)
    loss = mnist_loss(preds, yb)
    loss.backward()

def train_epoch(model, opt, dl):
    for xb, yb in dl:
        calc_grad(xb, yb, model)
        opt.step()
        opt.zero_grad()

def batch_accuracy(xb, yb):
    preds = xb.sigmoid()
    preds_x = torch.argmax(preds, dim=1).unsqueeze(1)
    assert(preds_x.shape == yb.shape)
    correct = preds_x == yb
    return correct.float().mean()

def batch_accuracy_2(xb, yb):
    preds_x = xb.round()
    correct = preds_x == yb
    return correct.float().mean()

def validate_epoch(model, dl):
    accs = [batch_accuracy(model(xb), yb) for xb,yb in dl]
    return round(torch.stack(accs).mean().item(), 4)

def train_model(model, opt, epochs, dl, valid_dl):
    for i in range(epochs):
        train_epoch(model, opt, dl)
        print(validate_epoch(model, valid_dl), end=' ')

dset = list(zip(train_tensor_x, train_tensor_y))
valid_dset = list(zip(valid_tensor_x, valid_tensor_y))

dl = DataLoader(dset, batch_size=256)
valid_dl = DataLoader(valid_dset, batch_size=256)

choice = 1

if choice == 0:
    lr = 1.0
    linear_model = nn.Linear(28 * 28, 10)
    opt = SGD(linear_model.parameters(), lr)

    # calc_grad(train_tensor_x[0:2], train_tensor_y[0:2], linear_model)
    # train_epoch(linear_model, opt, dl)
    train_model(linear_model, opt, 40, dl, valid_dl)

else:
    dls = DataLoaders(dl, valid_dl)
    simple_net = nn.Sequential(
        nn.Linear(28 * 28, 30),
        nn.ReLU(),
        # nn.Linear(30, 30),
        # nn.ReLU(),
        nn.Linear(30, 10),
    )
    learn = Learner(dls, simple_net, opt_func=SGD,
                    loss_func=mnist_loss_3, metrics=batch_accuracy)

    lr = 0.1
    learn.fit(24, lr=lr)
