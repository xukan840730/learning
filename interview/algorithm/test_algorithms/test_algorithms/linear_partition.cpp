#include <algorithm>

static void partition(const int s[], int n, int k)
{
#define MAXN 64

	int m[MAXN][MAXN];	// DP table for values
	int d[MAXN][MAXN];	// DP table for dividers
	int p[MAXN];		// prefix sums array

	p[0] = 0;
	for (int i = 1; i < n; i++)
		p[i] = p[i - 1] + s[i];

	for (int i = 0; i < n; i++)
		m[i][0] = p[i];

	for (int j = 0; j < k; j++)
		m[0][j] = s[0];

	for (int i = 1; i < n; i++)
	{
		for (int j = 1; j < k; j++)
		{
			m[i][j] = 0x7FFFFFFF;

			for (int x = 0; x < (i - 1); x++)
			{
				int cost = std::max(m[x][j - 1], p[i] - p[x]);
				if (m[x][j] > cost)
				{
					m[i][j] = cost;
					d[i][j] = x;
				}
			}
		}
	}
}

void test_linear_partition()
{
	//int seq[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	int seq[] = { 1, 2, 3, };
	int nseq = sizeof(seq) / sizeof(seq[0]);
	partition(seq, nseq, 2);
}