
def DP(i, X, weights, values):
    # last item
    if i == len(weights) - 1:
        if X >= weights[i]:
            return values[i]
        else:
            return 0

    # recursion.
    if X >= weights[i]:
        v1 = DP(i + 1, X, weights, values)
        v2 = DP(i + 1, X - weights[i], weights, values) + values[i]
        if v1 > v2:
            return v1
        else:
            return v2
    else:
        return DP(i + 1, X, weights, values)


def solver(weights, values, total_weight):
    return DP(0, total_weight, weights, values)


total_weight = 12
weights = [3, 4, 2, 5, 6]
values = [4, 6, 8, 10, 2]

res = solver(weights, values, total_weight)
print(res)
