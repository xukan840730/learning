import numpy as np

# dijkstra's algorithm:
def maxout_holidays(data):
    # data is a matrix
    # row: number of months
    # col: number of countries.

    num_months = data.shape[0]
    num_countries = data.shape[1]

    max_holidays = np.zeros([num_months, num_countries], dtype=np.int32)
    max_holidays_from = np.zeros([num_months, num_countries], dtype=np.int32)

    # first month
    for k in range(num_countries):
        max_holidays[0, k] = data[0, k]
        max_holidays_from[0, k] = k

    # for rest of months
    for m in range(1, num_months):
        for k in range(num_countries):
            max_num_holiday = 0
            for j in range(num_countries):
                if k != j:
                    num_holiday = data[m, k] + max_holidays[m - 1, j]
                    if num_holiday > max_num_holiday:
                        max_holidays[m, k] = num_holiday
                        max_holidays_from[m, k] = j
                        max_num_holiday = num_holiday


    # last step is to find the max vacation
    best_idx = -1
    max_num_holiday = 0
    for k in range(num_countries):
        if max_holidays[num_months - 1, k] > max_num_holiday:
            best_idx = k
            max_num_holiday = max_holidays[num_months - 1, k]

    # trace back
    seq = list()
    seq.append(best_idx)

    idx = best_idx
    for m in range(num_months - 1, 0, -1):
        idx_from = max_holidays_from[m, idx]
        seq.append(idx_from)
        idx = idx_from

    seq.reverse()

    return max_num_holiday, seq

data = np.array(
    [[1, 2, 0],
    [2, 3, 0],
    [3, 4, 10],
    [4, 5, 0]]
)

data2 = np.array(
    [[0, 2, 0],
    [0, 10, 0],
    [0, 9, 0]]
)

max_num_holidays, seq = maxout_holidays(data2)
print(max_num_holidays)
print(seq)