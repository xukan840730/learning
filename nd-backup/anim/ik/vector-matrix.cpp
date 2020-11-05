/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/vector-matrix.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/profiling/profiling.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ScalarMatrix::ScalarMatrix(int r, int c)
{
	PROFILE(Math, Matrix_constructor);
	m_rows	 = (I16)r;
	m_cols	 = (I16)c;
	m_matrix = NDI_NEW float[GetSize()];
}

/// --------------------------------------------------------------------------------------------------------------- ///
ScalarMatrix::ScalarMatrix(const ScalarMatrix& m)
{
	PROFILE(Math, Matrix_constructor);
	m_rows	 = m.m_rows;
	m_cols	 = m.m_cols;
	m_matrix = NDI_NEW float[GetSize()];
	memcpy(m_matrix, m.m_matrix, GetSize() * sizeof(float));
}

/// --------------------------------------------------------------------------------------------------------------- ///
ScalarMatrix::~ScalarMatrix()
{
	Free();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ScalarMatrix& ScalarMatrix::operator=(const ScalarMatrix& m)
{
	if (GetSize() != m.GetSize())
	{
		Free();
		m_matrix = NDI_NEW float[m.GetSize()];
	}

	m_rows = m.m_rows;
	m_cols = m.m_cols;
	memcpy(m_matrix, m.m_matrix, GetSize() * sizeof(float));

	return *this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ScalarMatrix::Get(int r, int c) const
{
	ANIM_ASSERT(r >= 0 && r < m_rows);
	ANIM_ASSERT(c >= 0 && c < m_cols);
	return m_matrix[Index(r, c)];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScalarMatrix::Set(int r, int c, float val)
{
	ANIM_ASSERT(r >= 0 && r < m_rows);
	ANIM_ASSERT(c >= 0 && c < m_cols);
	m_matrix[Index(r, c)] = val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScalarMatrix::Transpose(ScalarMatrix* res) const
{
	PROFILE(Math, Matrix_Transpose);
	ANIM_ASSERT(m_rows == res->m_cols && m_cols == res->m_rows);

	for (int i = 0; i < m_rows; i++)
	{
		for (int j = 0; j < m_cols; j++)
			res->Set(j, i, Get(i, j));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScalarMatrix::AddToDiagonal(float num)
{
	PROFILE(Math, AddToDiagonal);
	ANIM_ASSERT(m_rows == m_cols);

	for (int i = 0; i < m_rows; i++)
		Set(i, i, num + Get(i, i));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScalarMatrix::Solve()
{
	PROFILE(Math, Matrix_Solve);

	ANIM_ASSERT(m_rows + 1 == m_cols);

	int diagonalIndex = 0;
	for (int i = 0; i < m_rows; i++)
	{
		float maxVal  = m_matrix[diagonalIndex];
		int maxValRow = i;

		for (int j = i + 1; j < m_rows; j++)
		{
			float currVal = Get(j, i);
			if (Abs(currVal) > Abs(maxVal))
			{
				maxVal	  = currVal;
				maxValRow = j;
			}
		}

		if (i != maxValRow)
			RowSwap(i, maxValRow);

		ANIM_ASSERT(maxVal != 0.0f);

		int startIndex = diagonalIndex - i;
		for (int j = 0; j < i; j++)
			RowSubMult(i, j, m_matrix[startIndex++]);

		RowMult(i, 1.0f / m_matrix[diagonalIndex]);

		diagonalIndex += m_cols + 1;
	}

	int index = -1;
	for (int i = 0; i < m_rows; i++)
	{
		index += i + 2;
		for (int j = i + 1; j < m_rows; j++)
			RowSubMult(i, j, m_matrix[index++]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScalarMatrix::Free()
{
	PROFILE(Math, Matrix_Free);
	I64 jobId = ndjob::GetActiveJobId();

	// If we're allocating from single frame memory, DON'T DELETE THESE!
	// We're probably in a scoped allocator, so this is handled for us automatically!
	const Memory::Allocator* pSingleFrameAlloc = Memory::GetAllocator(kAllocSingleGameFrame);
	const Memory::Allocator* pScopedTempAlloc  = (jobId >= 0) ? GetScopedTempAllocatorForJob(jobId) : nullptr;
	const Memory::Allocator* pTopAlloc		   = Memory::TopAllocator();
	if (pTopAlloc != pSingleFrameAlloc && pTopAlloc != pScopedTempAlloc)
	{
		if (m_matrix)
		{
			NDI_DELETE[] m_matrix;
			m_matrix = nullptr;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MatrixMult(ScalarMatrix* result, const ScalarMatrix& mat, float mult)
{
	PROFILE(Math, Matrix_Mult);

	ANIM_ASSERT(result->GetNumRows() == mat.GetNumRows() && result->GetNumCols() == mat.GetNumCols());

	for (int i = 0; i < result->GetNumRows(); i++)
	{
		for (int j = 0; j < result->GetNumCols(); j++)
			result->Set(i, j, mat.Get(i, j) * mult);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MatrixMult(ScalarMatrix* result, float mult, const ScalarMatrix& mat)
{
	PROFILE(Math, Matrix_Mult);

	ANIM_ASSERT(result->GetNumRows() == mat.GetNumRows() && result->GetNumCols() == mat.GetNumCols());

	for (int i = 0; i < result->GetNumRows(); i++)
	{
		for (int j = 0; j < result->GetNumCols(); j++)
			result->Set(i, j, mult * mat.Get(i, j));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MatrixMult(ScalarMatrix* result, const ScalarMatrix& m1, const ScalarMatrix& m2)
{
	PROFILE(Math, Matrix_Mult);

	ANIM_ASSERT(result->GetNumRows() == m1.GetNumRows() && result->GetNumCols() == m2.GetNumCols());
	ANIM_ASSERT(m1.GetNumCols() == m2.GetNumRows());

	int sumCount = m1.GetNumCols();
	ANIM_ASSERT(sumCount > 0);

	float sum;
	int m1IndexStart;
	int m1Index;
	int m2Index;

	int resIndex = 0;

	for (int i = 0; i < result->GetNumRows(); i++)
	{
		m1IndexStart = i * m1.GetNumCols();

		for (int j = 0; j < result->GetNumCols(); j++)
		{
			m1Index = m1IndexStart;
			m2Index = j;

			sum = 0.0f;
			for (int k = 0; k < sumCount; k++)
			{
				sum += m1.m_matrix[m1Index] * m2.m_matrix[m2Index];

				++m1Index;
				m2Index += m2.GetNumCols();
			}

			result->m_matrix[resIndex++] = sum;
		}
	}
}
