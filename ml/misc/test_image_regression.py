from utils import *

from fastai.vision.all import *
path = untar_data(URLs.BIWI_HEAD_POSE)

img_files = get_image_files(path)
def img2pose(x): return Path(f'{str(x)[:-7]}pose.txt')
print(img2pose(img_files[0]))

cal = np.genfromtxt(path/'01'/'rgb.cal', skip_footer=6)
def get_ctr(f):
    ctr = np.genfromtxt(img2pose(f), skip_header=3)
    c1 = ctr[0] * cal[0][0]/ctr[2] + cal[0][2]
    c2 = ctr[1] * cal[1][1]/ctr[2] + cal[1][2]
    return tensor([c1,c2])

print(get_ctr(img_files[0]))

biwi = DataBlock(
    blocks=(ImageBlock, PointBlock),
    get_items=get_image_files,
    get_y=get_ctr,
    splitter=FuncSplitter(lambda o: o.parent.name=='13'),
    batch_tfms=[*aug_transforms(size=(240,320)),
                Normalize.from_stats(*imagenet_stats)]
)

dls = biwi.dataloaders(path, num_workers=0, bs=16)
print(dls.loss_func)
# dls.show_batch(max_n=9, figsize=(8,6))

learn = cnn_learner(dls, resnet18, y_range=(-1,1))
# learn = cnn_learner(dls, resnet18, y_range=(-1,1)).to_fp16() # CUDA error: out of memory

#lr_min, lr_steep = learn.lr_find()
lr = 1e-2
# lr = lr_steep * 10
learn.fine_tune(3, lr)