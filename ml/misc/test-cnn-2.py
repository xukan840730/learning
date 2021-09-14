from fastai.vision.all import *
from utils import *

matplotlib.rc('image', cmap='Greys')

path = untar_data(URLs.MNIST)

Path.BASE_PATH = path

# Categorize_Long: return tensor with dtype=long.
class Categorize_Long(DisplayedTransform):
    "Reversible transform of category string to `vocab` id"
    loss_func,order,store_attrs=CrossEntropyLossFlat(),1,'vocab,add_na'
    def __init__(self, vocab=None, sort=True, add_na=False):
        store_attr(self, self.store_attrs+',sort')
        self.vocab = None if vocab is None else CategoryMap(vocab, sort=sort, add_na=add_na)

    def setups(self, dsets):
        if self.vocab is None and dsets is not None: self.vocab = CategoryMap(dsets, sort=self.sort, add_na=self.add_na)
        self.c = len(self.vocab)

    def encodes(self, o):
        res = TensorCategory(self.vocab.o2i[o])
        res = res.long()
        return res
    def decodes(self, o): return Category      (self.vocab    [o])

# def get_dls(bs=64):
#     return DataBlock(
#         blocks=(ImageBlock(cls=PILImageBW), CategoryBlock),
#         get_items=get_image_files,
#         splitter=GrandparentSplitter('training', 'testing'),
#         get_y=parent_label,
#         batch_tfms=Normalize()
#     ).dataloaders(path, bs=bs, num_workers=0)

# dls = get_dls()


# convert DataBlock to transforms and Datasets
tfms = [[PILImageBW.create], [parent_label, Categorize_Long]]
# files = get_image_files(path, folders = ['training', 'testing'])
files = get_image_files(path)
splits = GrandparentSplitter(train_name='training', valid_name='testing')(files)

dsets = Datasets(files, tfms, splits=splits, verbose=True)

after_batch = [IntToFloatTensor, Normalize]

dls = dsets.dataloaders(
    after_item=[ToTensor],
    after_batch=after_batch,
    dl_type=None, num_workers=0)

# check dls.loaders[1].dataset

# xb, yb = first(dls.valid)
# print(xb.shape)
# print('yb_shape:', yb.shape, ' yb_dtype:', yb.dtype)
#
# xb,yb = to_cpu(xb),to_cpu(yb)

def conv(ni, nf, ks=3, act=True):
    res = nn.Conv2d(ni, nf, stride=2, kernel_size=ks, padding=ks//2)
    if act: res = nn.Sequential(res, nn.ReLU())
    return res

# simple_cnn = sequential(
#     conv(1 ,4),            #14x14
#     conv(4 ,8),            #7x7
#     conv(8 ,16),           #4x4
#     conv(16,32),           #2x2
#     conv(32,2, act=False), #1x1
#     Flatten(),
# )
#
# print('simple_cnn shape: ', simple_cnn(xb).shape)
#
#
# learn = Learner(dls, simple_cnn, loss_func=F.cross_entropy, metrics=accuracy)
# learn.summary()
#
# learn.fit_one_cycle(2, 0.01)

def simple_cnn2():
    return sequential(
        conv(1 , 8, ks=5),
        conv(8 , 16),
        conv(16, 32),
        conv(32, 64),
        conv(64, 10, act=False),
        Flatten()
    )

def fit(epochs=1):
    learn = Learner(dls, simple_cnn2(), loss_func=F.cross_entropy,
                    metrics=accuracy, cbs=ActivationStats(with_hist=True))
    learn.fit(epochs, 0.06)
    return learn

learn = fit()