import numpy as np
import primitive as prim

num_rows_odd = 15
num_cols_odd = 15

_horiLineData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)
_horiLineData[7, :] = True
g_horiLine = prim.PrimitiveShape()
g_horiLine.setShapeMask(_horiLineData)

_vertLineData = np.zeros([num_rows_odd, num_cols_odd], dtype=bool)
_vertLineData[:, 7] = True
g_vertLine = prim.PrimitiveShape()
g_vertLine.setShapeMask(_vertLineData)
