#include <stdio.h>
#include <string.h>
#include <assert.h>

static bool is_a_solution(int a[], int k, int n)
{
	return k == n;
}

static int construct_candidates(int a[], int k, int n, int c[])
{
	int ncandidates = 0;

	bool founds[64];
	memset(founds, 0, sizeof(founds));

	for (int ii = 0; ii < k; ii++)
	{
		const int index = a[ii];
		assert(index < 64);
		founds[index] = true;
	}

	for (int ii = 0; ii < n; ii++)
	{
		if (!founds[ii])
			c[ncandidates++] = ii;
	}

	return ncandidates;
}

static void process_solution(const int a[], int k)
{
	for (int i = 0; i < k; i++)
		printf(" %d", a[i]);
	printf("\n");
}

static void backtrack(int a[], int k, int n)
{
	static const int MAX_CANDIDATES = 64;
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

static void generate_permutations(int n)
{
	assert(n < 64);
	int a[64];

	backtrack(a, 0, n);
}

void test_generate_permutations()
{
	generate_permutations(4);
}