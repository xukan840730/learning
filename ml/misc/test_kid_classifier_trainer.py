#from utils import *
from fastbook import *
# from fastai.vision.widgets import *

path = Path('sorted_cropped')

kids_pics = DataBlock(
        blocks=(ImageBlock, CategoryBlock),
        get_items=get_image_files,
        splitter=RandomSplitter(valid_pct=0.2, seed=42),
        get_y=parent_label,
        item_tfms=Resize(128))

# kids_pics = kids_pics.new(
#     item_tfms=RandomResizedCrop(224, min_scale=0.5),
#     batch_tfms=aug_transforms())

kids_pics = kids_pics.new(
    item_tfms=Resize(512, ResizeMethod.Pad, pad_mode='zeros')
)

#
dls = kids_pics.dataloaders(path, num_workers=0, bs=16)
dls.train.show_batch(max_n=4, nrows=1)

learner = cnn_learner(dls, resnet18, metrics=error_rate)
learner.fine_tune(40)

interp = ClassificationInterpretation.from_learner(learner)
interp.plot_confusion_matrix()

res = learner.predict('images/hudson.jpg')
print(res)