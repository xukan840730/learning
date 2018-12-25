import numpy as np

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


def expand_v2_internal2(pos_from, pos_to, visited_global, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]
    direction = (pos_to[0] - pos_from[0], pos_to[1] - pos_from[1])
    grad_from_mag = sobel_grad_mag[pos_from]
    grad_to_mag = sobel_grad_mag[pos_to]

    def expand_internal(pos_from, pos_to, direction, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx):
        pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])

        if grad_to_mag < threshold:
            # keep exploring on that direction
            new_region[pos_to] = True
            frontiers.append((pos_from, pos_to))
        else:
            grad_next_mag = sobel_grad_mag[pos_next]

            if grad_from_mag > grad_to_mag:
                pass
            elif grad_to_mag < grad_next_mag:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_from, pos_to))
            # else:
            #     # pos_to is a local maximum on that direction.
            #     new_region[pos_to] = True

    if direction[0] < 0:
        # explore up
        if pos_to[0] == 0:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            expand_internal(pos_from, pos_to, direction, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

    elif direction[0] > 0:
        # explore down
        if pos_to[0] == image_height - 1:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            expand_internal(pos_from, pos_to, direction, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

    elif direction[1] < 0:
        # explore left
        if pos_to[1] == 0:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            expand_internal(pos_from, pos_to, direction, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

    elif direction[1] > 0:
        # explore right
        if pos_to[1] == image_width - 1:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            expand_internal(pos_from, pos_to, direction, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

    else:
        assert(False)

def expand_v2_internal(pos, visited_global, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    new_positions = ((pos[0] - 1, pos[1]), (pos[0] + 1, pos[1]),
                     (pos[0], pos[1] - 1), (pos[0], pos[1] + 1))

    for new_pos in new_positions:
        if new_pos[0] >= 0 and new_pos[0] < image_height and new_pos[1] >= 0 and new_pos[1] < image_width:
            if not new_region[new_pos] and not visited_global[new_pos]:
                expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

def expand_v2(pos, visited_global, sobel_grad_f, sobel_grad_mag, threshold, new_region, verbose):
    frontiers = list()
    iter_idx = 0
    new_region[pos] = True
    expand_v2_internal(pos, visited_global, sobel_grad_f, sobel_grad_mag, threshold, frontiers, new_region, iter_idx)

    if verbose:
        print("iter: %d begin:" % iter_idx)
        for each_f in frontiers:
            print(each_f)
        print("iter: %d end:" % iter_idx)

    while len(frontiers) > 0:
        iter_idx = iter_idx + 1
        new_fronties = list()

        for each_f in frontiers:
            pos_from, pos_to = each_f
            expand_v2_internal(pos_to, visited_global, sobel_grad_f, sobel_grad_mag, threshold, new_fronties, new_region, iter_idx)

        if verbose:
            print("iter: %d begin:" % iter_idx)
            for each_f in new_fronties:
                print(each_f)
            print("iter: %d end:" % iter_idx)

        frontiers = new_fronties

        # if iter_idx >= 150:
        #     break
