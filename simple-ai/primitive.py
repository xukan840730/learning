# essential shape

import numpy as np

# ShapeMask: 2D shape represented by bitmask
class ShapeMask:

    def __init__(self, num_rows, num_cols):
        assert(num_rows > 0)
        assert(num_cols > 0)
        self._rows = num_rows
        self._cols = num_cols
        self._bits = np.empty([num_rows, num_cols], dtype=bool)

class PrimitiveShape:

    def __init__(self):
        self._shapeMask = None

    def __del__(self):
        pass

    def setShapeMask(self, shapeMask):
        self._shapeMask = shapeMask