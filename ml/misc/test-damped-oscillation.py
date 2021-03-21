from scipy . integrate import odeint
import numpy as N
from matplotlib import pyplot as plt

def dy(y, t, zeta, w0):
    """
    The right-hand side of the damped oscillator ODE
    """
    x, p = y[0], y[1]
    dx = p
    dp = -2 * zeta * w0 * p - w0**2 * x
    return [dx, dp]

# initial state:
y0 = [1.0, 0.0]

# time coodinate to solve the ODE for
t = N.linspace(0, 10, 1000)
w0 = 2*N.pi*1.0

# solve the ODE problem for three different values of the damping ratio
# y1 = odeint(dy, y0, t, args=(0.0, w0)) # undamped
# y2 = odeint(dy, y0, t, args=(0.2, w0)) # under damped
y3 = odeint(dy, y0, t, args=(1.0, w0)) # critial damping
# y4 = odeint(dy, y0, t, args=(5.0, w0)) # over damped

fig, ax = plt.subplots()
# ax.plot(t, y1[:,0], 'k', label="undamped", linewidth=0.25)
# ax.plot(t, y2[:,0], 'r', label="under damped")
ax.plot(t, y3[:,0], 'b', label=r"critical damping")
# ax.plot(t, y4[:,0], 'g', label="over damped")
plt.show()
ax.legend()
