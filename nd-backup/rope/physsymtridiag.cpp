/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file physsymtridiag.cpp
    \author Sergiy Migdalskiy [mailto:sergiy_migdalskiy@naughtydog.com] 
    \brief .

 */

#include "gamelib/ndphys/rope/physsymtridiag.h"
#include "gamelib/ndphys/rope/physalignedarrayonstack.h"
#include "gamelib/ndphys/rope/physblas.h"
#include "corelib/math/gamemath.h"


///
/// scalar Conjugate gradient with _NO_ preconditioner
///
void PhysSymTriDiag::SolveScalarCg(float *__restrict b, float *__restrict x, U32F numIterations, float fTolerance)
{
	AlignedArrayOnStack<float> rBuf(m_numVars, FILE_LINE_FUNC);
	AlignedArrayOnStack<float> dBuf(m_numVars, FILE_LINE_FUNC);
	AlignedArrayOnStack<float> qBuf(m_numVars, FILE_LINE_FUNC);
	float *__restrict r = rBuf.GetPtr(), *__restrict d = dBuf.GetPtr(), *__restrict q = qBuf.GetPtr();
	// say the initial guess is 0
	memset(x, 0, sizeof(float)*m_numVars);
	memcpy(r, b, sizeof(float)*m_numVars); // r = b-Ax
	memcpy(d, r, sizeof(float)*m_numVars); // d = r
	float deltaNew = lenSqA(m_numVars, r);            // dnew = r'r
	float delta0   = deltaNew;
	U32F nIteration;
	for(nIteration = 0; nIteration < numIterations && deltaNew > fTolerance; ++nIteration)
	{
		Apply(d, q);    // q = Ad
		float alpha = deltaNew / blasDot(m_numVars, d,q); // alpha = dnew/d'q
		blasAxpy(m_numVars, alpha, d, x); // x = x + alpha d
		blasAxpy(m_numVars, -alpha, q, r); // r = r - alpha q
		float deltaOld = deltaNew;
		deltaNew = lenSqA(m_numVars, r);
		float beta = deltaNew / deltaOld;
		blasAypx(m_numVars, beta, r, d); // d = r + beta d
	}

	#ifdef _DEBUG
	Apply(x, q); // should be close to r, and r should be close to 0
	#endif
	qBuf.Deallocate();
	dBuf.Deallocate();
	rBuf.Deallocate();
}

///
/// Compute r = Ax
///
void PhysSymTriDiag::Apply(const float *__restrict x, float *__restrict r)
{
	for(U32F i = 0; i < m_numVars; ++i)
	{
		U32F prev = (i+m_numVars-1)%m_numVars;
		U32F next = (i+1)%m_numVars;
		r[i] = m_pSubdiag[i] * x[prev] + m_pDiag[i] * x[i] + m_pSubdiag[next] * x[next];
	}
}
