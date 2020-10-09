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

learn = cnn_learner(dls, resnet34, metrics=error_rate)
lr_min, lr_steep = learn.lr_find()
learn.fine_tune(2, base_lr=lr_steep * 2)