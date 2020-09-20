#include <stdio.h>
#include <assert.h>
#include <string.h>

static int DP(const int* seq, const int nseq, int i, int* num_cached, int* prev_nodes)
{
	assert(i >= 0 && i < nseq);

	if (num_cached[i] == -1)
	{
		int prev_node = -1;
		int num_res = 1;

		if (i == 0)
		{
			num_res = 1;
		}
		else
		{
			for (int jj = 0; jj < i; jj++)
			{
				if (seq[i] > seq[jj])
				{
					int dp = DP(seq, nseq, jj, num_cached, prev_nodes) + 1;
					if (dp > num_res)
					{
						prev_node = jj;
						num_res = dp;
					}
				}
			}
		}

		num_cached[i] = num_res;
		prev_nodes[i] = prev_node;
	}

	assert(num_cached[i] != -1);
	return num_cached[i];
}

static int solution(const int* seq, const int nseq)
{
	int num_cached[64];
	int prev_nodes[64];
	memset(num_cached, -1, sizeof(num_cached));
	memset(prev_nodes, -1, sizeof(prev_nodes));
	int num = DP(seq, nseq, nseq - 1, num_cached, prev_nodes);
	return num;
}

void test_longest_increasing_sequence()
{
	//int seq[] = { 2, 4, 3, 5, 1, 7, 6, 9, 8 };
	int seq[] = { 4, 5, 1, 2, 3, };
	//int seq[] = { 4, 1, 2, };
	//int seq[] = { 2, 4, 3, 5 };
	int nlen = sizeof(seq) / sizeof(seq[0]);
	int val = solution(seq, nlen);
	printf("val: %d\n", val);
}