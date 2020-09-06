
def DP(seq, i):
    nseq = len(seq)

    if i == 0:
        res = list()
        res.append(seq[i])
        return res

    best_num = 1
    ret_list = list()
    ret_list.append(seq[i])

    for j in range(0, i):
        if seq[i] > seq[j]:
            j_res = DP(seq, j)
            num = len(j_res) + 1

            if num > best_num:
                best_num = num
                ret_list = j_res.copy()
                ret_list.append(seq[i])

    return ret_list


def solution(seq):
    nseq = len(seq)
    return DP(seq, nseq - 1)

def test_longest_increasing_sequence():
    seq = [ 2, 4, 3, 5, 1, 7, 6, 9, 8 ]
    # seq = [ 4, 5, 1, 2, 3 ]
    # int seq[] = { 4, 1, 2, };
    # int seq[] = { 2, 4, 3, 5 };
    val = solution(seq)
    print(val)

test_longest_increasing_sequence()