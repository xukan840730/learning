import math as math
import numpy as np
import numpy.linalg as linalg
import utils.util as util

# Discrete polar coordinates system

def getPolarCoordsSamples(level):
    if level == 0:
        return 1
    else:
        return 8 * level

# lerp4: lerp between 4 corners
def lerp4(v00, v01, v10, v11, d00, d01, d10, d11):
    _sum = d01 * d10 * d11 + d00 * d10 * d11 + d00 * d01 * d11 + d00 * d01 * d10
    w00 = d01 * d10 * d11 / _sum
    w01 = d00 * d10 * d11 / _sum
    w10 = d00 * d01 * d11 / _sum
    w11 = d00 * d01 * d10 / _sum
    w_sum = w00 + w01 + w10 + w11
    v = w00 * v00 + w01 * v01 + w10 * v10 + w11 * v11
    return v

def imageMaskToPolarCoords(imageMask, origin):
    num_rows = imageMask.shape[0]
    num_cols = imageMask.shape[1]

    x_orig_f = origin[0]
    y_orig_f = origin[1]

    x_orig_i = util.MinMax(x_orig_f, 0, num_rows - 1)
    y_orig_i = util.MinMax(y_orig_f, 0, num_cols - 1)

    x_radius = max(x_orig_i, num_cols - x_orig_i)
    y_radius = max(y_orig_i, num_rows - y_orig_i)

    radius_f = max(x_radius, y_radius)
    radius_i = int(radius_f)

    result = []
    for ir in range(radius_i):
        num_samples = getPolarCoordsSamples(ir)
        samples = np.zeros(num_samples, dtype=bool)

        # fill each sample
        for isample in range(num_samples):
            angle_rad = math.pi * 2 / num_samples * isample
            x_sample_f = x_orig_f + math.cos(angle_rad) * ir
            y_sample_f = y_orig_f + math.sin(angle_rad) * ir

            x_sample_i = int(x_sample_f)
            y_sample_i = int(y_sample_f)

            if x_sample_i >= 0 and x_sample_i < num_rows:
                if y_sample_i >= 0 and y_sample_i < num_cols:
                    samples[isample] = imageMask[x_sample_i, y_sample_i]

        result.append(samples)

    return result

# imageMaskToPolarCoords2: have more samples:
def imageMaskToPolarCoords2(imageMask, origin):
    num_rows = imageMask.shape[0]
    num_cols = imageMask.shape[1]

    x_orig_f = origin[0]
    y_orig_f = origin[1]

    x_orig_i = util.MinMax(x_orig_f, 0, num_rows - 1)
    y_orig_i = util.MinMax(y_orig_f, 0, num_cols - 1)

    x_radius = max(x_orig_i, num_cols - x_orig_i)
    y_radius = max(y_orig_i, num_rows - y_orig_i)

    radius_f = max(x_radius, y_radius)
    radius_i = int(radius_f)

    # grid_vals = np.zeros([num_rows + 2, num_cols + 2], dtype=float)
    # for irow in range(grid_vals.shape[0]):
    #     for icol in range(grid_vals.shape[1]):
    #         v = 0.0
    #         count = 0
    #         irow_minus_1 = irow - 1
    #         icol_minus_1 = icol - 1
    #
    #         if irow < num_rows and icol < num_cols:
    #             if 0 <= irow_minus_1 and irow_minus_1 < num_rows and 0 <= icol_minus_1 and icol_minus_1 < num_cols:
    #                 if imageMask[irow_minus_1, icol_minus_1]:
    #                     v += 1.0
    #                 count += 1
    #
    #             if 0 <= irow_minus_1 and irow_minus_1 < num_rows and 0 <= icol and icol < num_cols:
    #                 if imageMask[irow_minus_1, icol]:
    #                     v += 1.0
    #                 count += 1
    #
    #             if 0 <= irow and irow < num_rows and 0 <= icol_minus_1 and icol_minus_1 < num_cols:
    #                 if imageMask[irow, icol_minus_1]:
    #                     v += 1.0
    #                 count += 1
    #
    #             if 0 <= irow and irow < num_rows and 0 <= icol and icol < num_cols:
    #                 if imageMask[irow, icol]:
    #                     v += 1.0
    #                 count += 1
    #
    #             assert(count > 0)
    #             grid_vals[irow, icol] = v / count

    result = []
    for ir in range(radius_i):
        num_samples = getPolarCoordsSamples(ir)
        samples = np.zeros(num_samples, dtype=float)

        # fill each sample
        for isample in range(num_samples):
            angle_rad = math.pi * 2 / num_samples * isample
            x_sample_f = x_orig_f + math.cos(angle_rad) * ir
            y_sample_f = y_orig_f + math.sin(angle_rad) * ir

            x_sample_i = math.floor(x_sample_f)
            y_sample_i = math.floor(y_sample_f)

            # if x_sample_i >= 0 and x_sample_i < grid_vals.shape[0] and y_sample_i >= 0 and y_sample_i < grid_vals.shape[1]:
            #     dx = x_sample_f - x_sample_i
            #     dy = y_sample_f - y_sample_i
            #     assert(dx >= 0 and dx <= 1.0)
            #     assert(dy >= 0 and dy <= 1.0)
            #     d00 = dx + dy
            #     d01 = 1 - dx + dy
            #     d10 = dx + 1 - dy
            #     d11 = 1 - dx + 1 - dy
            #     v00 = grid_vals[x_sample_i, y_sample_i]
            #     v01 = grid_vals[x_sample_i, y_sample_i + 1]
            #     v10 = grid_vals[x_sample_i + 1, y_sample_i]
            #     v11 = grid_vals[x_sample_i + 1, y_sample_i + 1]
            #     samples[isample] = lerp4(v00, v01, v10, v11, d00, d01, d10, d11)

            if x_sample_i >= 0 and x_sample_i < num_rows and y_sample_i >= 0 and y_sample_i < num_cols:
                samples[isample] = imageMask[x_sample_i, y_sample_i]

        result.append(samples)

    return result

def imagePolarHistogram(imagePolarMask):
    num_levels = len(imagePolarMask)
    num_bins = imagePolarMask[num_levels - 1].size
    # num_bins = 16
    num_bins = 32
    bins = np.zeros(num_bins, dtype=float)

    bin_angle_rad = np.zeros(num_bins, dtype=float)
    for i in range(num_bins):
        bin_angle_rad[i] = math.pi * 2 / num_bins * i

    #TODO: PARRALIZE
    level_f = 0.0
    for level in imagePolarMask:
        if level.size > 1:
            num_nonzeros = np.count_nonzero(level)
            if level.size != num_nonzeros:
                num_samples = level.size
                for isample in range(level.size):
                    if level[isample]:
                        angle_rad = math.pi * 2 / num_samples * isample

                        # TODO: simpify this.
                        best_idx = -1
                        best_dist = 100

                        for itest in range(num_bins):
                            edist = abs(bin_angle_rad[itest] - angle_rad)
                            if edist < best_dist:
                                best_dist = edist
                                best_idx = itest

                        assert(best_idx >= 0 and best_idx < num_bins)
                        bins[best_idx] += level_f

        level_f += 1.0

    return bins

def findDominantAxis(imagePolarHist):
    print(imagePolarHist)

    _mean = np.mean(imagePolarHist)
    num_elements = imagePolarHist.shape[0]

    _mask = imagePolarHist > _mean

    axis_ranges = []

    while True:
        max_v = 0
        max_idx = -1

        for idx in range(imagePolarHist.shape[0]):
            if _mask[idx]:
                if imagePolarHist[idx] > max_v:
                    max_v = imagePolarHist[idx]
                    max_idx = idx

        if max_idx < 0:
            break

        new_range = []
        new_range.append(max_idx)

        new_idx = max_idx
        while True:
            new_idx = (new_idx + 1) % num_elements
            if _mask[new_idx] and new_idx != max_idx: # stop at myself
                new_range.append(new_idx)
            else:
                break

        new_idx = max_idx
        while True:
            new_idx = (new_idx - 1 + num_elements) % num_elements
            if _mask[new_idx] and new_idx != max_idx: # stop at myself
                new_range.append(new_idx)
            else:
                break

        axis_ranges.append(new_range)

        # add new_range to masks
        _mask[new_range] = False

    result = []

    for ir in axis_ranges:
        each_axis = {}
        vec_total = np.zeros([2], dtype=float)

        for iangle in ir:
            angle_rad = math.pi * 2 / num_elements * iangle
            vec = np.zeros([2], dtype=float)
            vec[0] = math.cos(angle_rad)
            vec[1] = math.sin(angle_rad)
            vec *= imagePolarHist[iangle]
            vec_total += vec

        vec_angle_rad = math.atan2(vec_total[1], vec_total[0])
        vec_strength = math.sqrt(np.dot(vec_total, vec_total))

        each_axis['angle_rad'] = vec_angle_rad
        each_axis['strength'] = vec_strength
        each_axis['range'] = ir

        result.append(each_axis)

    return result