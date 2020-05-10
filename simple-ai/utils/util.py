import math as math

def MinMax(v, vmin, vmax):
    if v < vmin:
        return vmin
    elif v > vmax:
        return vmax
    else:
        return v