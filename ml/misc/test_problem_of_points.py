import numpy as np
import scipy.misc as misc

# probability of choose m from n:
def p_choose(n, m):
    p1 = misc.factorial(n, True)
    p2 = misc.factorial(m, True)
    p3 = misc.factorial(n-m, True)
    return p1/(p2*p3)

def p_bino(n, m, p):
    return p_choose(n, m) * (p**m) * ((1-p)**(n-m))

def p_sum_bino(n, m, p):
    res = 0.0
    for i in range(m, n):
        res += p_bino(n, i, p)

    return res

pt = p_sum_bino(8, 6, 0.5)
pw = p_sum_bino(8, 4, 0.5)
pz = p_sum_bino(8, 0, 0.5)
print(pt)
print(pw)
print(pz)

print(p_bino(250, 5, 0.02))
