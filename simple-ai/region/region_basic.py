import math as math
import numpy as np
import numpy.linalg as linalg

# distance function rgb: l2 distance
def dist_rgb_l2(pixel_a, pixel_b):
    diff = pixel_a - pixel_b
    return linalg.norm(diff)

# distance function rgb: l1 distance
def dist_rgb_l1(pixel_a, pixel_b):
    diff = pixel_a - pixel_b
    return abs(diff[0]) + abs(diff[1]) + abs(diff[2])

def in_bound(pixel, image_shape):
    if pixel[0] >= 0 and pixel[0] < image_shape[0] and pixel[1] >= 0 and pixel[1] < image_shape[1]:
        return True
    else:
        return False

def pixel_similarity(img_float):

    image_shape = np.zeros([2], dtype=int)
    image_shape[0] = img_float.shape[0]
    image_shape[1] = img_float.shape[1]

    # 8-connection
    res = np.zeros([image_shape[0], image_shape[1], 8], dtype=float)

    # TODO: shift image on 8 directions to get

    for ix in range(img_float.shape[0]):
        # print(ix)
        for iy in range(img_float.shape[1]):
            neighbors = np.zeros([8, 2], dtype=int)
            neighbors[0] = (ix - 1, iy)
            neighbors[1] = (ix - 1, iy + 1)
            neighbors[2] = (ix, iy + 1)
            neighbors[3] = (ix + 1, iy + 1)
            neighbors[4] = (ix + 1, iy)
            neighbors[5] = (ix + 1, iy - 1)
            neighbors[6] = (ix, iy - 1)
            neighbors[7] = (ix - 1, iy - 1)

            for iz in range(8):
                if in_bound(neighbors[iz], image_shape):
                    neighbor_x = neighbors[iz][0]
                    neighbor_y = neighbors[iz][1]
                    res[ix, iy, iz] = dist_rgb_l1(img_float[ix, iy], img_float[neighbor_x, neighbor_y])

    return res

### block phase
def block_phase_1_internal(image_f, i_block_x, i_block_y, block_size_x, block_size_y):
    _mask = np.zeros([block_size_x, block_size_y], dtype=bool)
    _region_id_local = np.zeros([block_size_x, block_size_y], dtype=int)

    region_id_new = 0
    while True:
        t = _mask == False
        if len(t) == 0:
            break;

        # expansion from first element.
        first_idx = t[0]
        _mask[first_idx] = True
        _region_id_local[first_idx] = region_id_new

        break;


### block phase
def block_phase_1(image_f):

    image_shape = np.zeros([2], dtype=int)
    image_shape[0] = image_f.shape[0]
    image_shape[1] = image_f.shape[1]

    block_size_x = 3
    block_size_y = 3

    # calucate number of blocks
    num_blocks_x = int(math.ceil(image_shape[0] / block_size_x))
    num_blocks_y = int(math.ceil(image_shape[1] / block_size_y))

    num_blocks_total = num_blocks_x * num_blocks_y

    for i_block_x in range(num_blocks_x):
        for i_block_y in range(num_blocks_y):
            block_phase_1_internal(image_f, i_block_x, i_block_y, block_size_x, block_size_y)
