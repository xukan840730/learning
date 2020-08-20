#include <stdio.h>
#include <assert.h>

// 0 means left parenthesis, 1 means right parenthesis

static bool is_a_solution(int a[], int k, int n)
{
	return k == n;
}

static int construct_candidates(const int a[], int k, int n, int c[])
{
	int ncandidates = 0;
	int num_allowed = n / 2;

	int num_left = 0;
	int num_right = 0;
	for (int i = 0; i < k; i++)
	{
		if (a[i] == 0)
			num_left++;
		else
			num_right++;
	}

	if (num_left == num_allowed && num_right == num_allowed)
	{
		ncandidates = 0;
	}
	else if (num_left == num_right)
	{
		c[0] = 0;
		ncandidates = 1;
	}
	else if (num_left == num_allowed)
	{
		c[0] = 1;
		ncandidates = 1;
	}
	else if (num_right == num_allowed)
	{
		assert(false);
		ncandidates = 0;
	}
	else if (num_left > num_right)
	{
		c[0] = 0;
		c[1] = 1;
		ncandidates = 2;
	}
	else
	{
		assert(false);
	}

	return ncandidates;
}

static void process_solution(int a[], int k)
{
	printf("{");
	for (int i = 0; i < k; i++)
		if (a[i] == 0)
			printf(" (");
		else if (a[i] == 1)
			printf(")");
		else
			assert(false);

	printf(" }\n");
}

static void backtrack(int a[], int k, int n)
{
	static const int MAX_CANDIDATES = 2;
	int c[MAX_CANDIDATES];

	if (is_a_solution(a, k, n))
	{
		process_solution(a, k);
	}
	else
	{
		int ncandidates = construct_candidates(a, k, n, c);
		assert(ncandidates <= MAX_CANDIDATES);

		for (int i = 0; i < ncandidates; i++)
		{
			a[k] = c[i];
			//make_move(a, k, input);
			backtrack(a, k + 1, n);
			//unmake_move(a, k, input);
		}
	}
}

static void generate_parentheses(int n)
{
	assert(n < 64);
	int a[64];

	assert(n % 2 == 0);
	backtrack(a, 0, n);
}

void test_generate_parentheses()
{
	generate_parentheses(8);
}