from utils import *

from fastai.vision.all import *
path = untar_data(URLs.PETS)

Path.BASE_PATH = path
print((path/"images").ls())

# fname = (path/"images").ls()[0]
# p = re.findall(r'(.+)_\d+.jpg$', fname.name)
# print(p)

pets = DataBlock(blocks = (ImageBlock, CategoryBlock),
                 get_items=get_image_files,
                 splitter=RandomSplitter(seed=42),
                 get_y=using_attr(RegexLabeller(r'(.+)_\d+.jpg$'), 'name'),
                 item_tfms=Resize(460),
                 batch_tfms=aug_transforms(size=224, min_scale=0.75))

# windows doesn't support multiple workers
dls = pets.dataloaders(path/"images", num_workers=0, bs=32)
# dls.show_batch(nrows=1, ncols=3)

acts = torch.randn((6, 2)) * 2
print(acts)
targ = tensor([0,1,0,1,1,0])
loss_func = nn.CrossEntropyLoss()
print(loss_func(acts, targ))
print(F.cross_entropy(acts, targ))

learn = cnn_learner(dls, resnet34, metrics=error_rate)
# learn = Learner(dls, resnet34, opt_func=SGD, loss_func=loss_func)

# cnn_learner auto freeze the early layers
learn.fit_one_cycle(3, 3e-3)
learn.unfreeze()

# learning rate finder
lr_min, lr_steep = learn.lr_find()
# learn.fit_one_cycle(6, lr_max=1e-5)

# discriminative learning rates
learn.fit_one_cycle(6, lr_max=slice(1e-6, 1e-4))
