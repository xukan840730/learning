import cv2 as cv2
import numpy as np
import region.region_basic as rb
import region.visualize as rv
import utils.util as util


def test1():
    img_u8 = cv2.imread('data\single\photo-road.jpg', cv2.IMREAD_COLOR)
    img_f = util.image_u8_to_f(img_u8)

    size_small = (img_f.shape[1] // 4, img_f.shape[0] // 4)
    image_small_f = cv2.resize(img_f, dsize=size_small)

    # cv2.imshow('img_small', img_small_f)

    block_size = np.zeros([2], dtype=int)
    block_size_x = 5
    block_size_y = 5
    block_size[0] = block_size_x
    block_size[1] = block_size_y

    # pixel_similarity = rb.pixel_similarity(img_small_f)
    image_region_id = rb.block_phase_1(image_small_f, block_size)
    # rv.visualize_image_region(image_regions)

    rb.block_merge_single_step(image_small_f, image_region_id, block_size)