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
def merge_nearby_similar_pixels(image_f, region_id_local, region_id_new, first_idx, block_size, offset):
    curr_indices = []
    curr_indices.append(first_idx)

    threshold = 1.0

    image_size_x = image_f.shape[0]
    image_size_y = image_f.shape[1]

    # collect nearby pixels
    while len(curr_indices) > 0:
        new_indices = []

        # first iteration:
        for ic in curr_indices:
            # up
            idx_up = ic + np.array([-1, 0])
            # down
            idx_down = ic + np.array([1, 0])
            # left
            idx_left = ic + np.array([0, -1])
            # right
            idx_right = ic + np.array([0, 1])

            new_pixels = [idx_up, idx_down, idx_left, idx_right]

            for new_p in new_pixels:
                if in_bound(new_p, block_size):
                    if region_id_local[new_p[0], new_p[1]] == -1:

                        image_ic = offset + ic
                        image_ip = offset + new_p

                        if image_ip[0] < image_size_x and image_ip[1] < image_size_y:

                            diff = dist_rgb_l1(image_f[image_ic[0], image_ic[1]], image_f[image_ip[0], image_ip[1]])
                            if diff < threshold:
                                region_id_local[new_p[0], new_p[1]] = region_id_new
                                new_indices.append(new_p)

        curr_indices = new_indices


def block_phase_1_internal(image_f, block_i, block_size, num_blocks, image_region_id_global):
    block_size_x = block_size[0]
    block_size_y = block_size[1]

    i_block_x = block_i[0]
    i_block_y = block_i[1]

    i_block_global = i_block_x * num_blocks[1] + i_block_y

    _region_id_local = np.zeros([block_size_x, block_size_y], dtype=int)
    _region_id_local[:] = -1

    offset = np.zeros([2], dtype=int)
    offset[0] = i_block_x * block_size_x
    offset[1] = i_block_y * block_size_y

    block_size = np.zeros([2], dtype=int)
    block_size[0] = block_size_x
    block_size[1] = block_size_y

    # each block begins with unique region-id
    region_id_new = 0
    while True:
        t = np.argwhere(_region_id_local == -1)
        if t.size == 0:
            break

        # expansion from first element.
        first_idx = t[0, :]
        _region_id_local[first_idx[0], first_idx[1]] = region_id_new

        # merge nearby similar pixels into region
        merge_nearby_similar_pixels(image_f, _region_id_local, region_id_new, first_idx, block_size, offset)

        # increase region-id
        region_id_new += 1

    # if region_id_new >= 2:
    #     print(region_id_new)

    _region_id_local += i_block_global * block_size_x * block_size_y

    # copy result to image_region_id_global
    for ix in range(block_size_x):
        for iy in range(block_size_y):
            image_region_id_global[offset[0] + ix, offset[1] + iy] = _region_id_local[ix, iy]


### block phase
def block_phase_1(image_f):

    image_shape = np.zeros([2], dtype=int)
    image_shape[0] = image_f.shape[0]
    image_shape[1] = image_f.shape[1]

    block_size = np.zeros([2], dtype=int)
    block_size_x = 3
    block_size_y = 3
    block_size[0] = block_size_x
    block_size[1] = block_size_y

    image_shape_pad = np.zeros([2], dtype=int)
    image_shape_pad[0] = (image_f.shape[0] + block_size_x - 1) // block_size_x * block_size_x
    image_shape_pad[1] = (image_f.shape[1] + block_size_y - 1) // block_size_y * block_size_y

    # calucate number of blocks
    num_blocks = np.zeros([2], dtype=int)
    num_blocks_x = int(math.ceil(image_shape[0] / block_size_x))
    num_blocks_y = int(math.ceil(image_shape[1] / block_size_y))
    num_blocks[0] = num_blocks_x
    num_blocks[1] = num_blocks_y

    num_blocks_total = num_blocks_x * num_blocks_y

    image_region_id_global = np.zeros(image_shape_pad, dtype=int)
    image_region_id_global[:] = -1

    for i_block_x in range(num_blocks_x):
        for i_block_y in range(num_blocks_y):
            block_i = np.zeros([2], dtype=int)
            block_i[0] = i_block_x
            block_i[1] = i_block_y
            block_phase_1_internal(image_f, block_i, block_size, num_blocks, image_region_id_global)

    return image_region_id_global