#include <stdio.h>
#include <assert.h>

static int DP(const int* seq, const int nseq, int i)
{
	assert(i >= 0 && i < nseq);

	if (i == 0)
		return 1;
	
	int max_val = 0;
	for (int j = 0; j < i; j++)
	{
		int dp = DP(seq, nseq, j);
		if (seq[i] > seq[j])
			dp += 1;

		if (dp > max_val)
		{
			max_val = dp;
		}
	}

	return max_val;
}

static int solution(const int* seq, const int nseq)
{
	return DP(seq, nseq, nseq - 1);
}

void test_longest_increasing_sequence()
{
	//int seq[] = { 2, 4, 3, 5, 1, 7, 6, 9, 8 };
	int seq[] = { 4, 5, 1, 2, 3, };
	//int seq[] = { 2, 4, 3, 5 };
	int nlen = sizeof(seq) / sizeof(seq[0]);
	int val = solution(seq, nlen);
	printf("val: %d\n", val);
}