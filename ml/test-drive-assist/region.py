import numpy as np

##--------------------------------------------------------------------------------------##
## v1
##--------------------------------------------------------------------------------------##
def expand_v1_internal(pos, visited_global, sobel_grad_f, threshold, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    def expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region):
        vg = visited_global[new_pos]
        vn = new_region[new_pos]
        if not vg and not vn:
            grad = sobel_grad_f[new_pos]
            grad_mag = np.linalg(grad)
            if grad_mag < threshold:
                frontiers.append(new_pos)
                new_region[new_pos] = True
            else:
                new_region[new_pos] = True

    if pos[0] > 0:
        # test up
        new_pos = (pos[0] - 1, pos[1])
        expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[0] < image_height - 1:
        # test down
        new_pos = (pos[0] + 1, pos[1])
        expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[1] > 0:
        # test left
        new_pos = (pos[0], pos[1] - 1)
        expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[1] < image_width - 1:
        # test right
        new_pos = (pos[0], pos[1] + 1)
        expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    # if pos[0] > 0 and pos[1] > 0:
    #     # test up-left
    #     new_pos = (pos[0] - 1, pos[1] - 1)
    #     expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)
    #
    # if pos[0] < image_height - 1 and pos[1] > 0:
    #     # test down-left
    #     new_pos = (pos[0] + 1, pos[1] - 1)
    #     expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)
    #
    # if pos[0] > 0 and pos[1] < image_width - 1:
    #     # test up-right
    #     new_pos = (pos[0] - 1, pos[1] + 1)
    #     expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)
    #
    # if pos[0] < image_height - 1 and pos[1] < image_width - 1:
    #     # test down-right
    #     new_pos = (pos[0] + 1, pos[1] + 1)
    #     expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)


def expand_v1(pos, visited_global, sobel_grad_f, threshold, new_region):
    frontiers = list()
    expand_v1_internal(pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    while len(frontiers) > 0:
        new_fronties = list()

        for x in frontiers:
            expand_v1_internal(x, visited_global, sobel_grad_f, threshold, new_fronties, new_region)

        frontiers = new_fronties

##--------------------------------------------------------------------------------------##
## v2
##--------------------------------------------------------------------------------------##
def expand_v2_internal2(pos_from, pos_to, visited_global, sobel_grad_mag, threshold_grad, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]
    grad_to_mag = sobel_grad_mag[pos_to]

    if grad_to_mag < threshold_grad:
        # keep exploring on that direction
        new_region[pos_to] = True
        frontiers.append((pos_from, pos_to))
    else:
        grad_from_mag = sobel_grad_mag[pos_from]

        if grad_from_mag > grad_to_mag:
            pass
        elif pos_to[0] == 0 or pos_to[0] == image_height - 1 or pos_to[1] == 0 or pos_to[1] == image_width - 1:
            pass
        else:
            direction = (pos_to[0] - pos_from[0], pos_to[1] - pos_from[1])
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            assert(pos_next[0] >= 0 and pos_next[0] < image_height and pos_next[1] >= 0 and pos_next[1] < image_width)
            grad_next_mag = sobel_grad_mag[pos_next]

            if grad_to_mag < grad_next_mag:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_from, pos_to))
                # else:
                #     # pos_to is a local maximum on that direction.
                #     new_region[pos_to] = True

def expand_v2_internal(pos, visited_global, sobel_grad_mag, threshold_grad, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    new_positions = ((pos[0] - 1, pos[1]), (pos[0] + 1, pos[1]),
                     (pos[0], pos[1] - 1), (pos[0], pos[1] + 1))

    for new_pos in new_positions:
        if new_pos[0] >= 0 and new_pos[0] < image_height and new_pos[1] >= 0 and new_pos[1] < image_width:
            if not new_region[new_pos] and not visited_global[new_pos]:
                expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_mag, threshold_grad, frontiers, new_region)

def expand_v2(pos, visited_global, sobel_grad_mag, threshold_grad, new_region, verbose):
    frontiers = list()
    iter_idx = 0
    new_region[pos] = True
    expand_v2_internal(pos, visited_global, sobel_grad_mag, threshold_grad, frontiers, new_region)

    if verbose:
        print("iter: %d begin:" % iter_idx)
        for each_f in frontiers:
            print(each_f)
        print("iter: %d end:" % iter_idx)

    while len(frontiers) > 0:
        iter_idx = iter_idx + 1
        # print('iter: %d' % iter_idx)
        new_fronties = list()

        for each_f in frontiers:
            pos_from, pos_to = each_f
            expand_v2_internal(pos_to, visited_global, sobel_grad_mag, threshold_grad, new_fronties, new_region)

        if verbose:
            print("iter: %d begin:" % iter_idx)
            for each_f in new_fronties:
                print(each_f)
            print("iter: %d end:" % iter_idx)

        frontiers = new_fronties

##--------------------------------------------------------------------------------------##
## v3: with color
##--------------------------------------------------------------------------------------##
def expand_v3_internal2(pos_from, pos_to, from_frontiers2, visited_global, sobel_grad_mag, image_gray_f, threshold_grad, frontiers1, frontiers2, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]
    grad_to_mag = sobel_grad_mag[pos_to]

    if grad_to_mag < threshold_grad:
        # keep exploring on that direction
        new_region[pos_to] = True
        frontiers1.append((pos_from, pos_to))
    else:
        grad_from_mag = sobel_grad_mag[pos_from]

        if pos_to[0] == 0 or pos_to[0] == image_height - 1 or pos_to[1] == 0 or pos_to[1] == image_width - 1:
            pass
        elif grad_from_mag > grad_to_mag:
            # pass
            if not from_frontiers2:
                gray_from = image_gray_f[pos_from]
                gray_to = image_gray_f[pos_to]
                if abs(gray_from - gray_to) < 0.02:
                    new_region[pos_to] = True
                    frontiers2.append((pos_from, pos_to))
        else:
            direction = (pos_to[0] - pos_from[0], pos_to[1] - pos_from[1])
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            assert(pos_next[0] >= 0 and pos_next[0] < image_height and pos_next[1] >= 0 and pos_next[1] < image_width)
            grad_next_mag = sobel_grad_mag[pos_next]

            if grad_to_mag < grad_next_mag:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers1.append((pos_from, pos_to))
                # else:
                #     # pos_to is a local maximum on that direction.
                #     new_region[pos_to] = True

def expand_v3_internal(pos, from_frontiers2, visited_global, sobel_grad_mag, image_gray_f, threshold_grad, frontiers1, frontiers2, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    new_positions = ((pos[0] - 1, pos[1]), (pos[0] + 1, pos[1]),
                     (pos[0], pos[1] - 1), (pos[0], pos[1] + 1))

    for new_pos in new_positions:
        if new_pos[0] >= 0 and new_pos[0] < image_height and new_pos[1] >= 0 and new_pos[1] < image_width:
            if not new_region[new_pos] and not visited_global[new_pos]:
                expand_v3_internal2(pos, new_pos, from_frontiers2, visited_global, sobel_grad_mag, image_gray_f, threshold_grad, frontiers1, frontiers2, new_region)

def expand_v3(pos, visited_global, image_grad_mag, image_gray_f, threshold_grad, new_region, verbose):
    assert(image_gray_f.dtype == float)

    frontiers1 = list()
    frontiers2 = list()
    iter_idx = 0
    new_region[pos] = True
    expand_v3_internal(pos, False, visited_global, image_grad_mag, image_gray_f, threshold_grad, frontiers1, frontiers2, new_region)

    if verbose:
        print("iter: %d begin, frontiers1:" % iter_idx)
        for each_f in frontiers1:
            print(each_f)
        print("frontiers2:")
        for each_f in frontiers2:
            print(each_f)
        print("iter: %d end:" % iter_idx)

    while len(frontiers1) > 0 or len(frontiers2) > 0:
        iter_idx = iter_idx + 1
        # print('iter: %d' % iter_idx)
        new_frontiers1 = list()
        new_frontiers2 = list()

        # apply frontiers1 before frontiers2
        for each_f in frontiers1:
            pos_from, pos_to = each_f
            expand_v3_internal(pos_to, False, visited_global, image_grad_mag, image_gray_f, threshold_grad, new_frontiers1, new_frontiers2, new_region)

        for each_f in frontiers2:
            pos_from, pos_to = each_f
            expand_v3_internal(pos_to, True, visited_global, image_grad_mag, image_gray_f, threshold_grad, new_frontiers1, new_frontiers2, new_region)

        if verbose:
            print("iter: %d begin, frontiers1:" % iter_idx)
            for each_f in frontiers1:
                print(each_f)
            print("frontiers2:")
            for each_f in frontiers2:
                print(each_f)
            print("iter: %d end:" % iter_idx)

        frontiers1 = new_frontiers1
        frontiers2 = new_frontiers2

##--------------------------------------------------------------------------------------##
## v4: with histogram
##--------------------------------------------------------------------------------------##