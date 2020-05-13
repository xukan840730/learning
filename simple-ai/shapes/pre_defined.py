import math as math
import numpy as np
import primitive as prim

num_rows_odd = 16 - 1
num_cols_odd = num_rows_odd
half_rows_odd = int(num_rows_odd / 2)
half_cols_odd = int(num_cols_odd / 2)

# horizontal line
_horiLineData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)
_horiLineData[half_rows_odd, :] = True
g_horiLine = prim.PrimitiveShape()
g_horiLine.setShapeMask(_horiLineData)

# vertical line
_vertLineData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)
_vertLineData[:, half_cols_odd] = True
g_vertLine = prim.PrimitiveShape()
g_vertLine.setShapeMask(_vertLineData)

# triangle outline
_triangleData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)
_triangleData[0, half_rows_odd] = True
_triangleData[num_rows_odd - 1, :] = True

for irow in range(1, num_rows_odd - 1):
    fcol1 = -(num_rows_odd - half_rows_odd) / num_rows_odd * irow + half_cols_odd
    icol1 = math.ceil(fcol1)
    assert(icol1 >= 0 and icol1 < num_cols_odd)

    fcol2 = (num_rows_odd - half_rows_odd) / num_rows_odd * irow + half_cols_odd
    icol2 = math.ceil(fcol2)
    assert (icol2 >= 0 and icol2 < num_cols_odd)

    for jcol in range(icol1, icol2):
        _triangleData[irow, jcol] = True

g_triangle = prim.PrimitiveShape()
g_triangle.setShapeMask(_triangleData)

# square outline
_squareData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)

padding_row = int(math.floor(num_rows_odd / 4))
padding_col = int(math.floor(num_cols_odd / 4))
for irow in range(padding_row, num_rows_odd - padding_row):
    for icol in range(padding_col, num_cols_odd - padding_col):
        _squareData[irow, icol] = True

g_square = prim.PrimitiveShape()
g_square.setShapeMask(_squareData)