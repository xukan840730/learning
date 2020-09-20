#include <stdio.h>
#include <string.h>
#include <assert.h>

struct EdgeMatrix
{
	int m_numRows = 0;
	int m_numCols = 0;
	int** m_pData = nullptr;

	void Allocate(int numRows, int numCols)
	{
		m_numRows = numRows;
		m_numCols = numCols;

		m_pData = new int* [numRows];
		for (int r = 0; r < m_numRows; r++)
		{
			m_pData[r] = new int[m_numCols];
			memset(m_pData[r], 0, sizeof(int) * m_numCols);
		}
	}

	void Release()
	{
		for (int r = 0; r < m_numRows; r++)
		{
			delete[] m_pData[r];
		}
		delete[] m_pData;
	}

	int Get(int rowIdx, int colIdx) const
	{
		assert(rowIdx < m_numRows);
		assert(colIdx < m_numCols);
		return m_pData[rowIdx][colIdx];
	}

	void Set(int rowIdx, int colIdx, int v)
	{
		assert(rowIdx < m_numRows);
		assert(colIdx < m_numCols);
		m_pData[rowIdx][colIdx] = v;
	}
};

static bool HasIncomingEdges(const int vertexIndex, const int num_vertices, const EdgeMatrix* pEdges)
{
	bool hasIncomingEdge = false;
	for (int j = 0; j < num_vertices; j++)
	{
		if (vertexIndex == j)
			continue;

		if (pEdges->Get(j, vertexIndex) > 0)
		{
			hasIncomingEdge = true;
			break;
		}
	}

	return hasIncomingEdge;
}

static bool top_sort(const int num_vertices, EdgeMatrix* pEdges, int* L, int* outNumL)
{
	*outNumL = 0;

	int* S = new int[num_vertices];
	int numS = 0;

	// find all vertices without incoming edges.
	for (int i = 0; i < num_vertices; i++)
	{
		bool hasIncomingEdge = HasIncomingEdges(i, num_vertices, pEdges);
		if (!hasIncomingEdge)
		{
			S[numS++] = i;
		}
	}

	while (numS > 0)
	{
		int curr_node = S[numS - 1];
		numS--;

		L[*outNumL] = curr_node;
		(*outNumL) += 1;

		for (int j = 0; j < num_vertices; j++)
		{
			if (curr_node == j)
				continue;

			const int temp = pEdges->Get(curr_node, j);
			if (temp > 0)
			{
				pEdges->Set(curr_node, j, temp - 1);

				if (!HasIncomingEdges(j, num_vertices, pEdges))
				{
					S[numS++] = j;
				}
			}
		}
	}

	bool foundEdge = false;
	for (int i = 0; i < num_vertices; i++)
	{
		for (int j = 0; j < num_vertices; j++)
		{
			if (i != j)
			{
				if (pEdges->Get(i, j) > 0)
				{
					foundEdge = true;
					break;
				}
			}
		}

		if (foundEdge)
			break;
	}

	return foundEdge;
}

void test_top_sort()
{
	int num_vertices = 5;

	EdgeMatrix edges;
	edges.Allocate(5, 5);

	edges.Set(0, 1, true);
	edges.Set(0, 2, true);
	edges.Set(1, 3, true);
	edges.Set(2, 3, true);
	edges.Set(3, 4, true);
	//edges.Set(4, 2, true);

	int L[64];
	int numL = 0;
	bool ret = top_sort(num_vertices, &edges, L, &numL);
	printf("ret: %d\n", ret);

	if (!ret)
	{
		printf("{");
		for (int i = 0; i < numL; i++)
		{
			printf(" %d", L[i]);
		}
		printf(" }");
	}

	edges.Release();
}