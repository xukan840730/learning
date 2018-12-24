import numpy as np

def expand_v1_internal(pos, visited_global, sobel_grad_f, threshold, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    def expand_internal(new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region):
        vg = visited_global[new_pos]
        vn = new_region[new_pos]
        if not vg and not vn:
            grad = sobel_grad_f[new_pos]
            grad_mag = np.sqrt(grad[0] * grad[0] + grad[1] * grad[1])
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


def expand_v2_internal2(pos_from, pos_to, visited_global, sobel_grad_f, threshold, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]
    direction = (pos_to[0] - pos_from[0], pos_to[1] - pos_from[1])
    grad_from = sobel_grad_f[pos_from]
    grad_to = sobel_grad_f[pos_to]
    grad_from_mag = np.sqrt(grad_from[0] * grad_from[0] + grad_from[1] * grad_from[1])
    grad_to_mag = np.sqrt(grad_to[0] * grad_to[0] + grad_to[1] * grad_to[1])

    if direction[0] < 0:
        # explore up
        if pos_to[0] == 0:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            grad_next = sobel_grad_f[pos_next]
            grad_next_mag = np.sqrt(grad_next[0] * grad_next[0] + grad_next[1] * grad_next[1])

            if grad_to_mag >= grad_next_mag:
                # pos_to is a local maximum on that direction.
                new_region[pos_to] = True
            else:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_to, pos_next))

    elif direction[0] > 0:
        # explore down
        if pos_to[0] == image_height - 1:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            grad_next = sobel_grad_f[pos_next]
            grad_next_mag = np.sqrt(grad_next[0] * grad_next[0] + grad_next[1] * grad_next[1])

            if grad_to_mag >= grad_next_mag:
                # pos_to is a local maximum on that direction.
                new_region[pos_to] = True
            else:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_to, pos_next))

    elif direction[1] < 0:
        # explore left
        if pos_to[1] == 0:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            grad_next = sobel_grad_f[pos_next]
            grad_next_mag = np.sqrt(grad_next[0] * grad_next[0] + grad_next[1] * grad_next[1])

            if grad_to_mag >= grad_next_mag:
                # pos_to is a local maximum on that direction.
                new_region[pos_to] = True
            else:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_to, pos_next))

    elif direction[1] > 0:
        # explore right
        if pos_to[1] == image_width - 1:
            if grad_to_mag > grad_from_mag:
                new_region[pos_to] = True
        else:
            pos_next = (pos_to[0] + direction[0], pos_to[1] + direction[1])
            grad_next = sobel_grad_f[pos_next]
            grad_next_mag = np.sqrt(grad_next[0] * grad_next[0] + grad_next[1] * grad_next[1])

            if grad_to_mag >= grad_next_mag:
                # pos_to is a local maximum on that direction.
                new_region[pos_to] = True
            else:
                # keep exploring on that direction
                new_region[pos_to] = True
                frontiers.append((pos_to, pos_next))

    else:
        assert(False)

def expand_v2_internal(pos, visited_global, sobel_grad_f, threshold, frontiers, new_region):
    image_height = visited_global.shape[0]
    image_width = visited_global.shape[1]

    if pos[0] > 0:
        # test up
        new_pos = (pos[0] - 1, pos[1])
        expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[0] < image_height - 1:
        # test down
        new_pos = (pos[0] + 1, pos[1])
        expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[1] > 0:
        # test left
        new_pos = (pos[0], pos[1] - 1)
        expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    if pos[1] < image_width - 1:
        # test right
        new_pos = (pos[0], pos[1] + 1)
        expand_v2_internal2(pos, new_pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)


def expand_v2(pos, visited_global, sobel_grad_f, threshold, new_region):
    frontiers = list()
    expand_v2_internal(pos, visited_global, sobel_grad_f, threshold, frontiers, new_region)

    while len(frontiers) > 0:
        new_fronties = list()

        for each_f in frontiers:
            # expand_v2_internal(x, visited_global, sobel_grad_f, threshold, new_fronties, new_region)
            print(each_f)

        frontiers = new_fronties
