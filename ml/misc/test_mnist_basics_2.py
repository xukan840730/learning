from fastai.vision.all import *
from utils import *

def init_params(size, std=1.0):
    return (torch.randn(size)*std).requires_grad_()

class BasicOptim:
    def __init__(self, params, lr):
        self.params = list(params)
        self.lr = lr

    def step(self, *args, **kwargs):
        for p in self.params:
            p.data -= p.grad.data * self.lr

    def zero_grad(self, *args, **kwargs):
        for p in self.params:
            p.grad = None

def mnist_loss(predictions, targets):
    predictions = predictions.sigmoid()
    return torch.where(targets==1, 1-predictions, predictions).mean()

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
    correct = (preds>0.5) == yb
    return correct.float().mean()

def validate_epoch(model, dl):
    accs = [batch_accuracy(model(xb), yb) for xb,yb in dl]
    return round(torch.stack(accs).mean().item(), 4)

def train_model(model, opt, epochs, dl, valid_dl):
    for i in range(epochs):
        train_epoch(model, opt, dl)
        print(validate_epoch(model, valid_dl), end=' ')

path = untar_data(URLs.MNIST_SAMPLE)

print("untar_data done!")

threes = (path/'train'/'3').ls().sorted()
sevens = (path/'train'/'7').ls().sorted()

three_tensors = [tensor(Image.open(o)) for o in threes] # list
seven_tensors = [tensor(Image.open(o)) for o in sevens] # list

print("train set loaded!")

stacked_threes = torch.stack(three_tensors).float()/255.0
stacked_sevens = torch.stack(seven_tensors).float()/255.0

valid_3_tens = torch.stack([tensor(Image.open(o)) for o in (path/'valid'/'3').ls()]).float()/255
valid_7_tens = torch.stack([tensor(Image.open(o)) for o in (path/'valid'/'7').ls()]).float()/255

print("valid set loaded!")

train_x = torch.cat([stacked_threes, stacked_sevens]).view(-1, 28*28)
train_y = tensor([1]*len(threes) + [0]*len(sevens)).unsqueeze(1)

valid_x = torch.cat([valid_3_tens, valid_7_tens]).view(-1, 28*28)
valid_y = tensor([1]*len(valid_3_tens) + [0]*len(valid_7_tens)).unsqueeze(1)

dset = list(zip(train_x, train_y))
valid_dset = list(zip(valid_x,valid_y))

lr = 1.
dl = DataLoader(dset, batch_size=256)
valid_dl = DataLoader(dset, batch_size=256)

choice = 2

if choice == 0:
    linear_model = nn.Linear(28 * 28, 1)
    opt = BasicOptim(linear_model.parameters(), lr)
    train_model(linear_model, opt, 20, dl, valid_dl)
elif choice == 1:
    linear_model = nn.Linear(28 * 28, 1)
    opt = SGD(linear_model.parameters(), lr)
    train_model(linear_model, opt, 20, dl, valid_dl)
else:
    dls = DataLoaders(dl, valid_dl)
    simple_net = nn.Sequential(
        nn.Linear(28*28, 20),
        nn.ReLU(),
        nn.Linear(20, 1)
    )
    learn = Learner(dls, simple_net, opt_func=SGD,
                    loss_func=mnist_loss, metrics=batch_accuracy)

    lr = 0.1
    learn.fit(100, lr=lr)
