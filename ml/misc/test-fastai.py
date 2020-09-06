#from utils import *
from fastbook import *
# from fastai.vision.widgets import *

path = Path('bears')

if False:
    key = '739b5a311768410e9fdb6c430b1c0ca5'

    result = search_images_bing(key, 'grizzly bear')
    ims = result.attrgot('content_url')

    dest = 'images/grizzly.jpg'
    download_url(ims[0], dest)

    im = Image.open(dest)
    im.to_thumb(128, 128)

    bear_types = 'grizzly','black','teddy'

    print("download begin")

    # if not path.exists():
    #     path.mkdir()
    for o in bear_types:
        dest = (path/o)
        dest.mkdir(exist_ok=True)
        results = search_images_bing(key, f'{o} bear')
        print(results)
        download_images(dest, urls=results.attrgot('content_url'))

    print("download done")

if True:
    # fns = get_image_files(path)
    # print(fns)

    # failed = verify_images(fns)
    # failed.map(Path.unlink)

    bears = DataBlock(
        blocks=(ImageBlock, CategoryBlock),
        get_items=get_image_files,
        splitter=RandomSplitter(valid_pct=0.2, seed=42),
        get_y=parent_label,
        item_tfms=Resize(128))

    # dls = bears.dataloaders(path)

    bears = bears.new(
        item_tfms=RandomResizedCrop(224, min_scale=0.5),
        batch_tfms=aug_transforms())
    dls = bears.dataloaders(path, num_workers=0, bs=16)
    dls.train.show_batch(max_n=4, nrows=1)

    learn = cnn_learner(dls, resnet18, metrics=error_rate)
    learn.fine_tune(4)

    interp = ClassificationInterpretation.from_learner(learn)
    interp.plot_confusion_matrix()

    res = learn.predict('images/grizzly.jpg')
    print(res)