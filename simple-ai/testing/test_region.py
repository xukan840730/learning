import cv2 as cv2
import region.region_basic as rb
import utils.util as util

def test1():
    img_u8 = cv2.imread('data\single\photo-road.jpg', cv2.IMREAD_COLOR)
    img_f = util.image_u8_to_f(img_u8)
    # pixel_similarity = rb.pixel_similarity(img_f)
    rb.block_phase_1(img_f)

