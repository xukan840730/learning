import numpy as np
import primitive as prim

num_rows_odd = 32 - 1
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
    icol1 = round(fcol1)
    assert(icol1 >= 0 and icol1 < num_cols_odd)
    _triangleData[irow, icol1] = True

    fcol2 = (num_rows_odd - half_rows_odd) / num_rows_odd * irow + half_cols_odd
    icol2 = round(fcol2)
    assert (icol2 >= 0 and icol2 < num_cols_odd)
    _triangleData[irow, icol2] = True

g_triangle = prim.PrimitiveShape()
g_triangle.setShapeMask(_triangleData)
