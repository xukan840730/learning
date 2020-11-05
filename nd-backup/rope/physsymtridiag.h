/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file physsymtridiag.h
    \author Sergiy Migdalskiy [mailto:sergiy_migdalskiy@naughtydog.com] 
    \brief Symmetrical tridiagonal system.

 */

#ifndef NDPHYS_SYM_TRI_DIAG_H 
#define NDPHYS_SYM_TRI_DIAG_H 

#include "corelib/math/gamemath.h"

///
/// this sytem may or may not by cyclical. "Cyclical" means
/// that the last element is connected with the 0th, so the matrix
/// is tridiagonal with NE and SW corners potentially non-zeroes (but equal)
///
struct PhysSymTriDiag
{
    //PhysSymTriDiag(U32 numVars);
	U32 m_numVars;
	// each of the below arrays must be of the size m_numVars
	// non-cyclical systems must have "0.0f" in the "0"th element
	float *__restrict m_pDiag, *__restrict m_pSubdiag;

	void Solve(float *__restrict pRhs, float *__restrict pSolution, U32F numIterations, float fTolerance)
	{
		SolveScalarCg(pRhs, pSolution, numIterations, fTolerance);
	}

	void Apply(const float *__restrict x, float *__restrict r);
protected:
	// scalar Conjugate gradient withOUT preconditioner
	void SolveScalarCg(float *__restrict pRhs, float *__restrict pSolution, U32F numIterations, float fTolerance);
};

#endif // NDPHYS_SYM_TRI_DIAG_H 

