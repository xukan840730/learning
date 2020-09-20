from fastai.vision.all import *
from utils import *

def init_params(size, std=1.0):
    return (torch.randn(size)*std).requires_grad_()

def linear1(xb, weights, bias):
    return xb@weights + bias

def sigmoid(x): return 1/(1+torch.exp(-x))

# def mnist_loss(predictions, targets):
#     return torch.where(targets==1, 1-predictions, predictions).mean()
def mnist_loss(predictions, targets):
    predictions = predictions.sigmoid()
    return torch.where(targets==1, 1-predictions, predictions).mean()

# model is functor here.
def calc_grad(xb, yb, model, weights, bias):
    preds = model(xb, weights, bias)
    loss = mnist_loss(preds, yb)
    loss.backward()

def train_epoch(model, lr, weights, bias):
    # for number of epochs
    for xb,yb in dl:
        calc_grad(xb, yb, model, weights, bias)
        # for p in params:
        #     p.data -= p.grad*lr
        #     p.grad.zero_()
        weights.data -= weights.grad*lr
        weights.grad.zero_()
        bias.data -= bias.grad*lr
        bias.grad.zero_()

def batch_accuracy(xb, yb):
    preds = xb.sigmoid()
    correct = (preds>0.5) == yb
    return correct.float().mean()

def validate_epoch(model, weights, bias, valid_dl):
    accs = [batch_accuracy(model(xb, weights, bias), yb) for xb,yb in valid_dl]
    return round(torch.stack(accs).mean().item(), 4)

path = untar_data(URLs.MNIST_SAMPLE)

threes = (path/'train'/'3').ls().sorted()
sevens = (path/'train'/'7').ls().sorted()

three_tensors = [tensor(Image.open(o)) for o in threes] # list
seven_tensors = [tensor(Image.open(o)) for o in sevens] # list

stacked_threes = torch.stack(three_tensors).float()/255.0
stacked_sevens = torch.stack(seven_tensors).float()/255.0

valid_3_tens = torch.stack([tensor(Image.open(o)) for o in (path/'valid'/'3').ls()]).float()/255
valid_7_tens = torch.stack([tensor(Image.open(o)) for o in (path/'valid'/'7').ls()]).float()/255

train_x = torch.cat([stacked_threes, stacked_sevens]).view(-1, 28*28)
train_y = tensor([1]*len(threes) + [0]*len(sevens)).unsqueeze(1)

valid_x = torch.cat([valid_3_tens, valid_7_tens]).view(-1, 28*28)
valid_y = tensor([1]*len(valid_3_tens) + [0]*len(valid_7_tens)).unsqueeze(1)

dset = list(zip(train_x, train_y))
valid_dset = list(zip(valid_x,valid_y))

weights = init_params((28*28, 1))
bias = init_params(1)

lr = 1.
dl = DataLoader(dset, batch_size=256)
valid_dl = DataLoader(dset, batch_size=256)

for i in range(500):
    train_epoch(linear1, lr, weights, bias)
    acc = validate_epoch(linear1, weights, bias, valid_dl)
    print(acc, end=' ')