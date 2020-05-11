import math as math
import numpy as np
import utils.util as util

# Discrete polar coordinates system

def getPolarCoordsSamples(level):
    if level == 0:
        return 1
    else:
        return 8 * level

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

def imagePolarHistogram(imagePolarMask):
    num_levels = len(imagePolarMask)
    # num_bins = imagePolarMask[num_levels - 1].size
    num_bins = 16
    bins = np.zeros(num_bins, dtype=int)

    #TODO: PARRALIZE
    ilevel = 0
    for level in imagePolarMask:
        if level.size > 1:
            num_nonzeros = np.count_nonzero(level)
            if level.size != num_nonzeros:
                for isample in range(level.size):
                    if level[isample]:
                        num_samples = level.size
                        bin_idx_f = isample / num_samples * num_bins
                        bin_idx_i = int(bin_idx_f)
                        bins[bin_idx_i] += ilevel

        ilevel += 1


    return bins