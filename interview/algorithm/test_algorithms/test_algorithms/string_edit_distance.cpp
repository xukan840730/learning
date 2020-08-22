#include <stdio.h>
#include <string.h>

#define MATCH 0
#define INSERT 1
#define DELETE 2

int cost_match(char s, char t) 
{ 
	if (s == t)
		return 0;
	else
		return 1;
}
int cost_indel(char s) { return 1; }

static int M[64][64];

int string_compare(const char* s, const char* t, int i, int j)
{
	if (i < 0) return (j + 1) * cost_indel(' ');
	if (j < 0) return (i + 1) * cost_indel(' ');

	// not thread safe!
	if (M[i][j] == -1)
	{
		int opt[3];
		opt[MATCH] = string_compare(s, t, i - 1, j - 1) + cost_match(s[i], t[j]);
		opt[INSERT] = string_compare(s, t, i, j - 1) + cost_indel(t[j]);
		opt[DELETE] = string_compare(s, t, i - 1, j) + cost_indel(s[i]);

		int lowest_cost = opt[MATCH];
		for (int kk = 1; kk <= DELETE; kk++)
		{
			if (opt[kk] < lowest_cost)
				lowest_cost = opt[kk];
		}

		M[i][j] = lowest_cost;
	}

	return M[i][j];
}

void string_compare(const char* s, const char* t)
{
	int s_i = (int)strlen(s) - 1;
	int t_j = (int)strlen(t) - 1;
	memset(M, -1, sizeof(M));
	int cost = string_compare(s, t, s_i, t_j);
	printf("cost: %d\n", cost);
}

void test_string_edit_distance()
{
	string_compare("ah", "ab");
}