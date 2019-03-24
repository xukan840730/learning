import numpy as np

#-----------------------------------------------------------------------------------#
def edge_equal(e0, e1):
    assert(len(e0) == 2)
    assert(len(e1) == 2)

    if (e0[0][0] == e0[1][0]):
        assert(abs(e0[0][1] - e0[1][1]) == 1)
    elif (e0[0][1] == e0[1][1]):
        assert(abs(e0[0][0] - e0[1][0]) == 1)
    else:
        assert(False)

    if (e1[0][0] == e1[1][0]):
        assert(abs(e1[0][1] - e1[1][1]) == 1)
    elif (e1[0][1] == e1[1][1]):
        assert(abs(e1[0][0] - e1[1][0]) == 1)
    else:
        assert(False)

    if e0[0] == e1[0] and e0[1] == e1[1]:
        return True

    if e0[0] == e1[1] and e0[1] == e1[0]:
        return True

    return False

#-----------------------------------------------------------------------------------#
def normalize(v):
    norm = np.linalg.norm(v)
    if norm == 0:
       return np.zeros(v.shape)
    return v / norm
