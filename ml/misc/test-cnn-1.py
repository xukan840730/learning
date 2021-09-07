from fastai.vision.all import *
from utils import *

matplotlib.rc('image', cmap='Greys')

top_edge = tensor([[-1,-1,-1],
                   [ 0, 0, 0],
                   [ 1, 1, 1]]).float()

path = untar_data(URLs.MNIST_SAMPLE)

Path.BASE_PATH = path

im3 = Image.open(path/'train'/'3'/'12.png')
# ax = show_image(im3)
# plt.show()

im3_t = tensor(im3)

# df = pd.DataFrame(im3_t[:10,:20])
# df.style.set_properties(**{'font-size':'6pt'}).background_gradient('Greys')

# print((im3_t[4:7,6:9] * top_edge).sum())

# def apply_kernel(row, col, kernel):
#     return (im3_t[row-1:row+2, col-1:col+2] * kernel).sum()

# print(apply_kernel(5, 7, top_edge))

# rng = range(1, 27)
# top_edge3 = tensor([[apply_kernel(i,j,top_edge) for j in rng] for i in rng])

# mnist = DataBlock((ImageBlock(cls=PILImageBW), CategoryBlock),
#                   get_items=get_image_files,
#                   splitter=GrandparentSplitter(),
#                   get_y=parent_label)
#
# print(mnist.type_tfms[0])
# print(mnist.type_tfms[0].map(type))
# print(mnist.default_item_tfms.map(type))
# print(mnist.type_tfms[1])

# dls = mnist.dataloaders(path, num_workers=0, verbose=True)

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


# convert DataBlock to transforms and Datasets
tfms = [[PILImageBW.create], [parent_label, Categorize_Long]]
files = get_image_files(path, folders = ['train', 'valid'])
splits = GrandparentSplitter()(files)

dsets = Datasets(files, tfms, splits=splits)

dls = dsets.dataloaders(
    after_item=[ToTensor],
    after_batch=[IntToFloatTensor],
    dl_type=None, num_workers=0, verbose=True)

# check dls.loaders[1].dataset

xb, yb = first(dls.valid)
print(xb.shape)
print('yb_shape:', yb.shape, ' yb_dtype:', yb.dtype)

xb,yb = to_cpu(xb),to_cpu(yb)

def conv(ni, nf, ks=3, act=True):
    res = nn.Conv2d(ni, nf, stride=2, kernel_size=ks, padding=ks//2)
    if act: res = nn.Sequential(res, nn.ReLU())
    return res

simple_cnn = sequential(
    conv(1 ,4),            #14x14
    conv(4 ,8),            #7x7
    conv(8 ,16),           #4x4
    conv(16,32),           #2x2
    conv(32,2, act=False), #1x1
    Flatten(),
)

print('simple_cnn shape: ', simple_cnn(xb).shape)


learn = Learner(dls, simple_cnn, loss_func=F.cross_entropy, metrics=accuracy)
learn.summary()

learn.fit_one_cycle(2, 0.01)
