#include <assert.h>
#include <stdio.h>
#include <string.h>

static int DP(const char* str, int nstr, int i, int j)
{
	assert(i < nstr && j < nstr);
	assert(i <= j);
	if (i == j)
		return 1;
	else if (i == j - 1)
		return 2;

	int count[3];
	count[0] = (str[i] == str[j] ? 2 : 0) + DP(str, nstr, i + 1, j - 1);
	count[1] = DP(str, nstr, i, j - 1);
	count[2] = DP(str, nstr, i + 1, j);

	int max_count = -1;
	for (int kk = 0; kk < 3; kk++)
	{
		if (count[kk] > max_count)
		{
			max_count = count[kk];
		}
	}
	return max_count;
}

static void solution(const char* str)
{
	int nstr = (int)strlen(str);
	int max_count = DP(str, nstr, 0, nstr - 1);
	printf("max_count: %d\n", max_count);
}

void test_longest_parlindromic_substring()
{
	solution("abcbaeabc");
}