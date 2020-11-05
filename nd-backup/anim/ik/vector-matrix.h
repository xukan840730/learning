/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/math/eigen-common.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// MatrixT decl - A matrix class that allows for any number of rows/cols and
//	any type of element, including Vectors. Designed primary for math involving
//	jacobian matrices.
/// --------------------------------------------------------------------------------------------------------------- ///
class ScalarMatrix
{
	friend void MatrixMult(ScalarMatrix* result, const ScalarMatrix& mat, float mult);
	friend void MatrixMult(ScalarMatrix* result, float mult, const ScalarMatrix& mat);
	friend void MatrixMult(ScalarMatrix* result, const ScalarMatrix& m1, const ScalarMatrix& m2);

private:
	float* m_matrix;
	I16 m_rows;
	I16 m_cols;

public:
	ScalarMatrix(int r, int c);
	ScalarMatrix(const ScalarMatrix& m);
	~ScalarMatrix();

	ScalarMatrix& operator=(const ScalarMatrix& m);

	int GetNumRows() const { return m_rows; }
	int GetNumCols() const { return m_cols; }
	int GetSize() const { return m_rows * m_cols; }

	int Index(int r, int c) const { return r * m_cols + c; }
	float Get(int r, int c) const;
	void Set(int r, int c, float val);

	void Transpose(ScalarMatrix* res) const;
	void AddToDiagonal(float num);

	void Solve();
	void RowSwap(int r1, int r2)
	{
		int r1Index = r1 * m_cols;
		int r2Index = r2 * m_cols;

		float val;
		for (int i = 0; i < m_cols; i++)
		{
			val = m_matrix[r1Index];
			m_matrix[r1Index] = m_matrix[r2Index];
			m_matrix[r2Index] = val;

			++r1Index;
			++r2Index;
		}
	}

	void RowMult(int r, float mult)
	{
		int index = r * m_cols;

		for (int i = 0; i < m_cols; i++)
			m_matrix[index++] *= mult;
	}

	void RowAddMult(int r1, int r2, float mult)
	{
		int r1Index = r1 * m_cols;
		int r2Index = r2 * m_cols;

		for (int i = 0; i < m_cols; i++)
			m_matrix[r1Index++] += mult * m_matrix[r2Index++];
	}

	void RowSubMult(int r1, int r2, float mult)
	{
		int r1Index = r1 * m_cols;
		int r2Index = r2 * m_cols;

		for (int i = 0; i < m_cols; i++)
			m_matrix[r1Index++] -= mult * m_matrix[r2Index++];
	}

private:
	void Free();
};

void MatrixMult(ScalarMatrix* result, const ScalarMatrix& mat, float mult);
void MatrixMult(ScalarMatrix* result, float mult, const ScalarMatrix& mat);
void MatrixMult(ScalarMatrix* result, const ScalarMatrix& m1, const ScalarMatrix& m2);
