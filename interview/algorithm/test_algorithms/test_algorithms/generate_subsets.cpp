#include <stdio.h>
#include <assert.h>

static bool finished = false;

static bool is_a_solution(int a[], int k, int n)
{
	return k == n;
}

static void construct_candidates(int a[], int k, int n, int c[], int* ncandidates)
{
	c[0] = true;
	c[1] = false;
	*ncandidates = 2;
}

static void process_solution(int a[], int k)
{
	printf("{");
	for (int i = 0; i < k; i++)
		if (a[i])
			printf(" %d", i);

	printf(" }\n");
}

static void backtrack(int a[], int k, int n)
{
	static const int MAX_CANDIDATES = 2;
	int c[MAX_CANDIDATES];
	int ncandidates;

	if (is_a_solution(a, k, n))
	{
		process_solution(a, k);
	}
	else
	{
		construct_candidates(a, k, n, c, &ncandidates);
		assert(ncandidates <= MAX_CANDIDATES);

		for (int i = 0; i < ncandidates; i++)
		{
			a[k] = c[i];
			//make_move(a, k, input);
			backtrack(a, k + 1, n);
			//unmake_move(a, k, input);

			if (finished) // terminate early
				return;
		}
	}
}

static void generate_subsets(int n)
{
	assert(n < 64);
	int a[64];
	
	backtrack(a, 0, n);
}

void test_generate_subsets()
{
	generate_subsets(3);
}