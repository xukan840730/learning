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

    x_orig_f = origin[1]
    y_orig_f = origin[0]

    x_orig_i = int(round(x_orig_f))
    y_orig_i = int(round(y_orig_f))
    x_orig_i = util.MinMax(x_orig_i, 0, num_cols - 1)
    y_orig_i = util.MinMax(y_orig_i, 0, num_rows - 1)

    x_radius = max(x_orig_i, num_cols - x_orig_i)
    y_radius = max(y_orig_i, num_rows - y_orig_i)

    radius = max(x_radius, y_radius)

    result = []
    for ir in range(radius):
        num_samples = getPolarCoordsSamples(ir)
        samples = np.zeros(num_samples, dtype=bool)

        # fill each sample
        for isample in range(num_samples):
            angle_rad = math.pi * 2 / num_samples * isample
            x_sample_f = x_orig_i + math.cos(angle_rad) * ir
            y_sample_f = y_orig_i + math.sin(angle_rad) * ir

            x_sample_i = int(round(x_sample_f))
            y_sample_i = int(round(y_sample_f))

            if x_sample_i >= 0 and x_sample_i < num_cols:
                if y_sample_i >= 0 and y_sample_i < num_rows:
                    samples[isample] = imageMask[y_sample_i, x_sample_i]

        result.append(samples)

    return result