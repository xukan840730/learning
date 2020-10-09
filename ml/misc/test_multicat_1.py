from utils import *

from fastai.vision.all import *
path = untar_data(URLs.PASCAL_2007)

df = pd.read_csv(path/'train.csv')
df.head()

# def get_x(r): return r['fname']
# def get_y(r): return r['labels']
# dblock = DataBlock(get_x = get_x, get_y = get_y)
# dsets = dblock.datasets(df)
# dsets.train[0]

def get_x(r): return path/'train'/r['fname']
def get_y(r): return r['labels'].split(' ')
# dblock = DataBlock(get_x = get_x, get_y = get_y)
# dsets = dblock.datasets(df)
# dsets.train[0]

# dblock = DataBlock(blocks=(ImageBlock, MultiCategoryBlock),
#                    get_x = get_x, get_y = get_y)
# dsets = dblock.datasets(df)
# dsets.train[0]
#
# idxs = torch.where(dsets.train[0][1]==1.)[0]
# dsets.train.vocab[idxs]

def splitter(df):
    train = df.index[~df['is_valid']].tolist()
    valid = df.index[df['is_valid']].tolist()
    return train,valid

# dblock = DataBlock(blocks=(ImageBlock, MultiCategoryBlock),
#                    splitter=splitter,
#                    get_x=get_x,
#                    get_y=get_y)
#
# dsets = dblock.datasets(df)
# dsets.train[0]

dblock = DataBlock(blocks=(ImageBlock, MultiCategoryBlock),
                   splitter=splitter,
                   get_x=get_x,
                   get_y=get_y,
                   item_tfms = RandomResizedCrop(128, min_scale=0.35))

# windows doesn't support multiple workers
dls = dblock.dataloaders(df, num_workers=0)

# loss_func is auto picked by MultiCategoryBlock
print(dls.loss_func)

# dls.show_batch(nrows=1, ncols=3)

# learn = cnn_learner(dls, resnet18, metrics=error_rate)
# x,y = dls.train.one_batch()
# learn.model.cuda()
# activs = learn.model(x)
# print(activs.shape)

def binary_cross_entropy(inputs, targets):
    inputs = inputs.sigmoid()
    return -torch.where(targets==1, inputs, 1-inputs).log().mean()

learn = cnn_learner(dls, resnet50, metrics=partial(accuracy_multi, thresh=0.2))
learn.fine_tune(3, base_lr=3e-3, freeze_epochs=4)

learn.metrics = partial(accuracy_multi, thresh=0.1)
learn.validate()

learn.metrics = partial(accuracy_multi, thresh=0.99)
learn.validate()

preds,targs = learn.get_preds()
accuracy_multi(preds, targs, thresh=0.9, sigmoid=False)