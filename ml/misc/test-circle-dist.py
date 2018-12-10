import numpy as np
import math as math
import matplotlib.pyplot as plt

num_samples_row = 20
step = 2.0 / (num_samples_row - 3)

total_samples = num_samples_row * num_samples_row
samples = np.empty([total_samples, 2])

row_start = -(num_samples_row - 1) // 2 * step

# stupid, but works
for irow in range(num_samples_row):
    for icol in range(num_samples_row):
        index = irow * num_samples_row + icol
        if irow % 2 == 0:
            samples[index, 0] = -1.0 + icol * step - step
            samples[index, 1] = row_start + irow * step
        else:
            samples[index, 0] = -1.0 - step / 2.0 + icol * step - step
            samples[index, 1] = row_start + irow * step
print(samples)

in_circle_mask = np.empty([total_samples], dtype=bool)
out_cirlce_mask = np.empty([total_samples], dtype=bool)

for irow in range(num_samples_row):
    for icol in range(num_samples_row):
        index = irow * num_samples_row + icol
        if math.sqrt(samples[index, 0] * samples[index, 0] + samples[index, 1] * samples[index, 1]) <= 1.0:
            in_circle_mask[index] = True
            out_cirlce_mask[index] = False
        else:
            in_circle_mask[index] = False
            out_cirlce_mask[index] = True
print(in_circle_mask)

print(np.count_nonzero(in_circle_mask))

in_samples = samples[in_circle_mask]
out_samples = samples[out_cirlce_mask]

fig = plt.figure()
ax = fig.add_subplot(111)
ax.set_aspect(aspect=1.0)
plt.plot(in_samples[:, 0], in_samples[:, 1], 'rx')
plt.plot(out_samples[:, 0], out_samples[:, 1], 'bx')
plt.show()