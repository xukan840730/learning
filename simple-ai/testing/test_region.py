import cv2 as cv2
import region.region_basic as rb
import region.visualize as rv
import utils.util as util

def test1():
    img_u8 = cv2.imread('data\single\photo-road.jpg', cv2.IMREAD_COLOR)
    img_f = util.image_u8_to_f(img_u8)

    size_small = (img_f.shape[1] // 8, img_f.shape[0] // 8)
    img_small_f = cv2.resize(img_f, dsize=size_small)

    cv2.imshow('img_small', img_small_f)

    # pixel_similarity = rb.pixel_similarity(img_small_f)
    image_regions = rb.block_phase_1(img_small_f)
    rv.visualize_image_region(image_regions)

