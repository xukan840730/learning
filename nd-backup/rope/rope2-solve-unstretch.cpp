/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-collector.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/ngen/mesh.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "ndlib/debug//nd-dmenu.h"

//#include <Collide/Shape/Convex/Capsule/hkpCapsuleShape>

#define SIN_45	0.70710678119f

const F32 g_veloRelax = 1.0f;
Scalar g_stepDt(1.0f);

Vector kVecDown(0.0f, -1.0f, 0.0f);

F32 g_unstrechTolerance = 0.0003f; 
F32 g_unstretchRelaxInv = 0.985f;

F32 g_straightenRelax = 0.05f;

F32 g_boundaryBendingCorrect = 0.3f;

bool g_ropeMultiGridStartOnEdges = true;

bool g_ropeUseGpu = true;

Vec4 Rope2::RelaxEdgePosOneSide(U32F iFixed, U32F ii)
{
	Vector vecLastEdge = SafeNormalize(m_pPos[ii] - m_pPos[iFixed], kVecDown);
	Point oldPos = m_pPos[ii];
	Scalar edgeLen = Abs(m_pRopeDist[ii]-m_pRopeDist[iFixed]);
	m_pPos[ii] = m_pPos[iFixed] + vecLastEdge * edgeLen * g_unstretchRelaxInv;
	ASSERT(IsFinite(m_pPos[ii]));
	Vec4 r0((m_pPos[ii] - oldPos).GetVec4());
	return r0*r0;
}

Vec4 Rope2::RelaxEdgePos(U32F i, U32F j)
{
	ASSERT(i < (m_numPoints - 1));
	ASSERT(j < m_numPoints);

	Point p0 = m_pPos[i];
	Point p1 = m_pPos[j];

	F32 w0 = m_pInvRelMass[i];
	F32 w1 = m_pInvRelMass[j];

	Scalar edgeLen0 = m_pRopeDist[j]-m_pRopeDist[i];
	Scalar edgeLen;
	Vector vecEdge = SafeNormalize(p1-p0, kVecDown, edgeLen);
	Vector delta = (edgeLen - edgeLen0 * g_unstretchRelaxInv) * vecEdge;
	Vector deltaLeft = delta * (w0 / (w0 + w1));
	Vector deltaRight = delta - deltaLeft;

	p0 += deltaLeft;
	p1 -= deltaRight;
	ASSERT(IsFinite(p0));
	ASSERT(IsFinite(p1));
	m_pPos[i] = p0;
	m_pPos[j] = p1;

	Vec4 r0 = deltaLeft.GetVec4();
	Vec4 r1 = deltaRight.GetVec4();

	return r0*r0+r1*r1;
}

Vec4 Rope2::RelaxEdgePosUnstretchOnly(U32F i, U32F j)
{
	ASSERT(i < (m_numPoints - 1));
	ASSERT(j < m_numPoints);

	Point p0 = m_pPos[i];
	Point p1 = m_pPos[j];

	F32 w0 = m_pInvRelMass[i];
	F32 w1 = m_pInvRelMass[j];

	Scalar edgeLen0 = m_pRopeDist[j]-m_pRopeDist[i];
	Scalar edgeLen;
	Vector vecEdge = SafeNormalize(p1-p0, kVecDown, edgeLen);
	if (Abs((I32F)j-(I32F)i) > 1 && edgeLen <= edgeLen0*g_unstretchRelaxInv)
	{
		return Vec4(kZero);
	}
	Vector delta = (edgeLen - edgeLen0 * g_unstretchRelaxInv) * vecEdge;
	Vector deltaLeft = delta * (w0 / (w0 + w1));
	Vector deltaRight = delta - deltaLeft;

	p0 += deltaLeft;
	p1 -= deltaRight;
	ASSERT(IsFinite(p0));
	ASSERT(IsFinite(p1));
	m_pPos[i] = p0;
	m_pPos[j] = p1;

	Vec4 r0 = deltaLeft.GetVec4();
	Vec4 r1 = deltaRight.GetVec4();

	return r0*r0+r1*r1;
}

Vec4 Rope2::RelaxEdgePosOneSideUnstretchOnly(U32F iFixed, U32F ii)
{
	Scalar edgeLen = Abs(m_pRopeDist[ii]-m_pRopeDist[iFixed]);
	Vector vecLastEdge = m_pPos[ii] - m_pPos[iFixed];
	Scalar vecLen;
	vecLastEdge = SafeNormalize(vecLastEdge, kVecDown, vecLen);
	if (Abs((I32F)iFixed-(I32F)ii) > 1 && vecLen <= edgeLen*g_unstretchRelaxInv)
	{
		return Vec4(kZero);
	}
	Point oldPos = m_pPos[ii];
	m_pPos[ii] = m_pPos[iFixed] + vecLastEdge * edgeLen*g_unstretchRelaxInv;
	ASSERT(IsFinite(m_pPos[ii]));
	Vec4 r0((m_pPos[ii] - oldPos).GetVec4());
	return r0*r0;
}

SMath::Vec4 Rope2::RelaxPosExternalConstraints(U32F i)
{
	ASSERT(i < m_numPoints);

	return m_pConstr[i].RelaxPos(m_pPos[i], m_pRadius[i]);
}

SMath::Vec4 Rope2::RelaxPosDistanceConstraint(U32F ii)
{
	Point p = Point(m_pDistConstraints[ii]);
	Vector v = m_pPos[ii] - p;
	Scalar dist;
	v = SafeNormalize(v, kZero, dist);
	Scalar err = Max(Scalar(kZero), dist - m_pDistConstraints[ii].W());
	m_pPos[ii] -= err * v;
	return Vec4(err*err);
}

SMath::Vec4 Rope2::RelaxPosDistanceConstraints(U32F iStart, U32F iEnd)
{
	Vec4 resid(kZero);
	for (U32F ii = iStart+1; ii<iEnd; ii++)
	{
		resid += RelaxPosDistanceConstraint(ii);
	}
	return resid;
}

SMath::Vec4 Rope2::RelaxPosFrictionConstraint(U32F ii)
{
	Point p = Point(m_pFrictionConstraints[ii]);
	Vector v = m_pPos[ii] - p;
	Scalar dist0;
	v = SafeNormalize(v, kZero, dist0);
	F32 dist = dist0 * m_pFrictionConstraints[ii].W();
	dist += m_pFrictionConstConstraints[ii];
	dist = Min(dist0, dist);
	m_pPos[ii] -= dist * v;
	return Vec4(Scalar(dist*dist));
}

SMath::Vec4 Rope2::RelaxPosFrictionConstraints(U32F iStart, U32F iEnd)
{
	Vec4 resid(kZero);
	for (U32F ii = iStart; ii<=iEnd; ii++)
	{
		resid += RelaxPosFrictionConstraint(ii);
	}
	return resid;
}

JOB_ENTRY_POINT_CLASS_DEFINE(Rope2, SolveLooseSectionJob)
{
	const LooseSectionData* pData = reinterpret_cast<const LooseSectionData*>(jobParam);
	pData->m_pRope->SolveLooseSection(pData->m_start, pData->m_end);
}

void Rope2::SolveLooseSection(U32F start, U32F end)
{
	F32 dt = m_scStepTime;

	RelaxPositions(start, end);
	PostSolve(start, end);

	CheckStateSane(start, end);
}

void CollisionPlaneEdgeAlfaF(Vector_arg norm, Vector_arg ropeDir, F32 orient, Vector_arg ropeAn, F32 ropeAlfa, F32& alfaF, bool& working)
{
	F32 dt = Dot(norm, ropeDir);
	if (dt > 0.0f)
	{
		Vector AEdge;
		if (dt < 0.99f)
		{
			AEdge = Cross(norm, ropeDir);
			Scalar sinGama;
			AEdge = Normalize(AEdge, sinGama);
			F32 gama = Asin(Min(1.0f, (F32)sinGama));
			F32 edgeAlfa = 0.5f*PI-gama;
			AEdge *= edgeAlfa;
		}
		else
		{
			F32 dt2 = Dot(norm, ropeAn);
			AEdge = (ropeAn - dt2*ropeAn) * 0.5f*PI;
		}

		AEdge *= orient;
		F32 alfaF0 = Max(0.0f, (F32)Dot(ropeAn, AEdge));
		alfaF += alfaF0;
		working = alfaF0 > 0.0f;
	}
}

void Rope2::PostSolve(U32F start, U32F end)
{
	PROFILE(Rope, PostSolve);

	BackstepVelocities(start, end, m_scStepTime);

	if (!m_bPostSolve)
		return;

	ScopedTempAllocator jjAlloc(FILE_LINE_FUNC);

	U32 numNodes = end-start+1;

	Vector* pVelSolver = NDI_NEW Vector[numNodes];
	for (U32 ii = 0; ii<numNodes; ii++)
	{
		pVelSolver[ii] = m_pVel[start+ii];
	}

#if !FINAL_BUILD
	if (m_pDebugPreProjPos)
	{
		memcpy(m_pDebugPreProjPos+start, m_pPos+start, numNodes*sizeof(m_pPos[0]));
	}
#endif

	// Project nodes back to their collision AABBs to make sure it did not go through collision we did not anticipate
	if (!g_ropeMgr.m_disableNodeAabbPostProjection)
	{
		for (U32 ii = start; ii<=end; ii++)
		{
			if (IsNodeKeyframedInt(ii))
				continue;

			Aabb aabb = m_pNodeAabb[ii];
			aabb.Expand(m_pRadius[ii]);
			while (1)
			{
				Point p0 = m_pPos[ii];
				Point p = p0;
				aabb.ClipPoint(p);
				m_pPos[ii] = p;
				RelaxPosExternalConstraints(ii);
				if (DistSqr(m_pPos[ii], p0) < 0.0001f)
					break;
			}
		}
	}

	BackstepVelocities(start, end, m_scStepTime);
	Vector* pVelProject = NDI_NEW Vector[numNodes];
	for (U32 ii = 0; ii<numNodes; ii++)
	{
		pVelProject[ii] = m_pVel[start+ii];
	}
	
	// Freaking friction (Ff)
	F32* pT = NDI_NEW F32[numNodes]; // rope tension
	F32* pNiT = NDI_NEW F32[numNodes]; // coef tension -> friction
	F32* pFg = NDI_NEW F32[numNodes]; // gravity friction
	//Point* pEdgePoints = NDI_NEW Point[numNodes];
	//Vec4* pEdges = NDI_NEW Vec4[numNodes];
	U8* pPatSurface = NDI_NEW U8[numNodes];
	F32* pAlfaF = NDI_NEW F32[numNodes];
	{
		const F32 Ni = 0.03f; // friction coef for gravity induced friction contacts
		const F32 NiAlfa = 0.1f; // friction coef for "wrap around" friction
		const F32 maxT = 50.0f; 
		const F32 Gm = 10.0f; // gravity force per meter
		const F32 bendTFallOff = 50.0f / 180.0f * PI;
		const F32 bendTZero = 500.0f / 180.0f * PI;

		F32* pDTg = NDI_NEW F32[numNodes]; // additional tension from node gravity

		F32 T;
		if (IsNodeKeyframedInt(start))
		{
			T = maxT;
		}
		else
		{
			T = 0.0f;
		}
		pT[0] = T;
		pNiT[0] = 0.0f;
		pFg[0] = 0.0f;

		for (U32F ii = 0; ii<numNodes; ii++)
		{
			U32F iN = start + ii;
			if (IsNodeKeyframedInt(iN))
			{
				if (ii == 0)
					T = maxT;
				pT[ii] = T;
				pNiT[ii] = 0.0f;
				pFg[ii] = 0.0f;
				pDTg[ii] = 0.0f;
				pAlfaF[ii] = 0.0f;
				pPatSurface[ii] = 0;
			}
			else
			{
				Constraint& con = m_pConstr[iN];

				Vector d0 = ii > 0 ? SafeNormalize(m_pPos[iN] - m_pPos[iN-1], kUnitYAxis) : kUnitYAxis;
				Vector d1 = ii < numNodes-1 ? SafeNormalize(m_pPos[iN+1] - m_pPos[iN], d0) : d0;
				F32 segl = 0.5f * ((ii > 0 ? m_pRopeDist[ii]-m_pRopeDist[ii-1] : 0.0f) + ii<numNodes-1 ? m_pRopeDist[ii+1]-m_pRopeDist[ii] : 0.0f);

				F32 G = Gm * segl;

				Quat qA = QuatFromVectors(d0, d1);
				Vector An;
				F32 alfa;
				{
					Vec4 An4;
					qA.GetAxisAndAngle(An4, alfa);
					An = Vector(An4);
				}
				
				F32 Tred = T;

				pNiT[ii] = 0.0f;
				pPatSurface[ii] = 0;
				if (ii > 0 && ii<numNodes-1)
				{
					// Bending over collision edges
					F32 alfaF = 0.0f; // friction angle

					// Friction (especially static) can cause node not to move at all and make it appear like collision constraint is not working
					// so here we need to set collision constraint as working if it's generating friction
					bool workingPlane[Constraint::kMaxNumPlanes];
					memset(workingPlane, 0, Constraint::kMaxNumPlanes*sizeof(workingPlane[0]));

					for (U32 iEdge = 0; iEdge<con.m_numEdges; iEdge++)
					{
						U32 iPl0 = con.m_edgePlanes[iEdge*2];
						U32 iPl1 = con.m_edgePlanes[iEdge*2+1];
						U8 planeMask = 0;
						Vec4 edgePlane = con.GetEdgePlane(iEdge, m_pPos[iN], planeMask);
						F32 h = Dot4(m_pPos[iN].GetVec4(), edgePlane) - m_pRadius[iN];
						F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
						if (k > 0.0f && (planeMask & ~con.m_frictionlessFlags) != 0)
						{
							F32 alfaF0 = 0.0f;
							if (planeMask == ((Constraint::kIsWorking1 << iPl0) | (Constraint::kIsWorking1 << iPl1)))
							{
								// This node is on the edge
								F32 f = Min(0.0f, Dot4(m_pPos[iN-1].GetVec4(), con.m_planes[iPl0])) - Min(0.0f, Dot4(m_pPos[iN-1].GetVec4(), con.m_planes[iPl1]))
									- Min(0.0f, Dot4(m_pPos[iN+1].GetVec4(), con.m_planes[iPl0])) + Min(0.0f, Dot4(m_pPos[iN+1].GetVec4(), con.m_planes[iPl1]));
								F32 edgeOrient = f > 0.0f ? 1.0f : -1.0f;

								// Get the edge axis and angle
								Quat qAEdge = QuatFromVectors(Vector(con.m_planes[iPl0]), Vector(con.m_planes[iPl1]));
								Vector AEdge;
								{
									F32 edgeAlfa;
									Vec4 An4;
									qAEdge.GetAxisAndAngle(An4, edgeAlfa);
									Vector AEdgeN = Vector(An4);
									AEdge = edgeOrient * AEdgeN * edgeAlfa;
								}

								// Project onto rope bending axis
								alfaF0 = Dot(An, AEdge);
								alfaF0 = Max(alfaF0, 0.0f);
								if (alfaF0 > 0.0f)
								{
									workingPlane[iPl0] = true;
									workingPlane[iPl1] = true;
								}
							}
							else if (planeMask == (Constraint::kIsWorking1 << iPl0))
							{
								CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl0]), d0, 1.0f, An, alfa, alfaF0, workingPlane[iPl0]);
								CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl0]), -d1, -1.0f, An, alfa, alfaF0, workingPlane[iPl0]);
							}
							else
							{
								CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl1]), d0, 1.0f, An, alfa, alfaF0, workingPlane[iPl1]);
								CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl1]), -d1, -1.0f, An, alfa, alfaF0, workingPlane[iPl1]);
							}

							// And accumulate
							alfaF += k * alfaF0;
						}
					}

					// Now do this for all potential situations of 2 nodes being on each side of an edge
					for (U32 iPl = con.m_firstNoEdgePlane; iPl<con.m_numPlanes; iPl++)
					{
						F32 h = Dot4(m_pPos[iN].GetVec4(), con.m_planes[iPl]) - m_pRadius[iN];
						F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
						if (k > 0.0f && (con.m_frictionlessFlags & (1 << iPl)) == 0)
						{
							F32 alfaF0 = 0.0f;
							CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl]), d0, 1.0f, An, alfa, alfaF0, workingPlane[iPl]);
							CollisionPlaneEdgeAlfaF(Vector(con.m_planes[iPl]), -d1, -1.0f, An, alfa, alfaF0, workingPlane[iPl]);
							alfaF += k * alfaF0;
						}
					}
					for (U32 iWorking = 0; iWorking<Constraint::kMaxNumPlanes; iWorking++)
					{
						if (workingPlane[iWorking])
							con.m_flags |= 1 << iWorking;
					}

					F32 maxAlfaF = alfaF;
					if (alfaF > 0.0f)
					{
						pPatSurface[ii] = con.m_patSurface;
					}

					{
						F32 alfaFCon0 = 0.0f;
						Constraint& con0 = m_pConstr[iN-1];
						memset(workingPlane, 0, Constraint::kMaxNumPlanes*sizeof(workingPlane[0]));
						for (U32 iEdge = 0; iEdge<con0.m_numEdges; iEdge++)
						{
							U32 iPl0 = con0.m_edgePlanes[iEdge*2];
							U32 iPl1 = con0.m_edgePlanes[iEdge*2+1];
							U8 planeMask = 0;
							Vec4 edgePlane = con0.GetEdgePlane(iEdge, m_pPos[iN-1], planeMask);
							F32 h = Dot4(m_pPos[iN-1].GetVec4(), edgePlane) - m_pRadius[iN-1];
							F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
							if (k > 0.0f && (planeMask & ~con0.m_frictionlessFlags) != 0)
							{
								F32 alfaF0 = 0.0f;
								bool working = false;
								CollisionPlaneEdgeAlfaF(Vector(edgePlane), -d0, -1.0f, An, alfa, alfaF0, working);
								if (working)
								{
									workingPlane[iPl0] = true;
									workingPlane[iPl1] = true;
								}
								alfaFCon0 += k * alfaF0;
							}
						}
						for (U32 iPl = con0.m_firstNoEdgePlane; iPl<con0.m_numPlanes; iPl++)
						{
							F32 h = Dot4(m_pPos[iN-1].GetVec4(), con0.m_planes[iPl]) - m_pRadius[iN-1];
							F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
							if (k > 0.0f && (con0.m_frictionlessFlags & (1 << iPl)) == 0)
							{
								F32 alfaF0 = 0.0f;
								CollisionPlaneEdgeAlfaF(Vector(con0.m_planes[iPl]), -d0, -1.0f, An, alfa, alfaF0, workingPlane[iPl]);
								alfaFCon0 += k * alfaF0;
							}
						}
						for (U32 iWorking = 0; iWorking<Constraint::kMaxNumPlanes; iWorking++)
						{
							if (workingPlane[iWorking])
								con0.m_flags |= 1 << iWorking;
						}

						if (alfaFCon0 > maxAlfaF)
						{
							pPatSurface[ii] = con0.m_patSurface;
							maxAlfaF = alfaFCon0;
						}
						alfaF += alfaFCon0;

						F32 alfaFCon1 = 0.0f;
						Constraint& con1 = m_pConstr[iN+1];
						memset(workingPlane, 0, Constraint::kMaxNumPlanes*sizeof(workingPlane[0]));
						for (U32 iEdge = 0; iEdge<con1.m_numEdges; iEdge++)
						{
							U32 iPl0 = con1.m_edgePlanes[iEdge*2];
							U32 iPl1 = con1.m_edgePlanes[iEdge*2+1];
							U8 planeMask = 0;
							Vec4 edgePlane = con1.GetEdgePlane(iEdge, m_pPos[iN+1], planeMask);
							F32 h = Dot4(m_pPos[iN+1].GetVec4(), edgePlane) - m_pRadius[iN+1];
							F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
							if (k > 0.0f && (planeMask & ~con1.m_frictionlessFlags) != 0)
							{
								F32 alfaF0 = 0.0f;
								bool working = false;
								CollisionPlaneEdgeAlfaF(Vector(edgePlane), d1, 1.0f, An, alfa, alfaF0, working);
								if (working)
								{
									workingPlane[iPl0] = true;
									workingPlane[iPl1] = true;
								}
								alfaFCon1 += k * alfaF0;
							}
						}
						for (U32 iPl = con1.m_firstNoEdgePlane; iPl<con1.m_numPlanes; iPl++)
						{
							F32 h = Dot4(m_pPos[iN+1].GetVec4(), con1.m_planes[iPl]) - m_pRadius[iN+1];
							F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
							if (k > 0.0f && (con1.m_frictionlessFlags & (1 << iPl)) == 0)
							{
								F32 alfaF0 = 0.0f;
								CollisionPlaneEdgeAlfaF(Vector(con1.m_planes[iPl]), d1, 1.0f, An, alfa, alfaF0, workingPlane[iPl]);
								alfaFCon1 += k * alfaF0;
							}
						}
						for (U32 iWorking = 0; iWorking<Constraint::kMaxNumPlanes; iWorking++)
						{
							if (workingPlane[iWorking])
								con1.m_flags |= 1 << iWorking;
						}

						if (alfaFCon1 > maxAlfaF)
						{
							pPatSurface[ii] = con1.m_patSurface;
							maxAlfaF = alfaFCon1;
						}
						alfaF += alfaFCon1;
					}

					// Get friction factor
					alfaF = Min(alfa, alfaF);
					pAlfaF[ii] = alfaF;
					F32 beta = 0.5f * (PI - alfaF);
					F32 fx = alfaF < 0.01f ? 0.0f : alfaF > PI-0.01f ? 2.0f : Sin(alfaF)/Sin(beta);
					pNiT[ii] += NiAlfa * fx;

					alfa -= alfaF;
				}
				else
				{
					// free end
					alfa = 0.0f;
					Tred = 0.0f; 
					pAlfaF[ii] = 0.0f;
				}

				if (ii > 2 && numNodes-ii > 3)
				{
					// Reduced max T if there is too much bend in the rope not caused by bending around collision edges
					Tred = LerpScale(bendTFallOff*segl, bendTZero*segl, maxT, 0.0f, alfa);
				}

				if (Tred < T)
				{
					T = Tred;

#if !FINAL_BUILD
					if (m_pDebugTensionBreak)
						m_pDebugTensionBreak[ii] = true;
#endif

					// Now go back and see if we need to reduce tension in any previous node
					F32 Tback = T;
					for (I32 jj = ii-1; jj>=0; jj--)
					{
						U32 jN = start + jj;
						Tback -= Min(0.0f, pDTg[jj]);
						Tback = MinMax(Tback, 0.0f, maxT);
						Tback += pNiT[jj] * Tback + pFg[jj];
						if (Tback >= pT[jj])
						{
							break;
						}
#if !FINAL_BUILD
						if (m_pDebugTensionBreak)
							m_pDebugTensionBreak[jj] = false;
#endif
						pT[jj] = Tback;
					}
				}

				F32 Ny = 0.0f;
				for (U32 iPl = 0; iPl<con.m_numPlanes; iPl++)
				{
					F32 h = Dot4(m_pPos[iN].GetVec4(), con.m_planes[iPl]) - m_pRadius[iN];
					F32 k = LerpScale(0.001f, 0.002f, 1.0f, 0.0f, h);
					if (k > 0.0f)
					{
						Ny += k * Max(0.0f, (F32)con.m_planes[iPl].Y());
					}
				}
				F32 Gf = Min(1.0f, Ny) * G; // gravity force absorbed by contact and going into friction
				pFg[ii] = Ni * Gf;

				T += pNiT[ii] * T; // add bending friction to the tension
				T += pFg[ii]; // add gravity friction to the tension
				T = MinMax(T, 0.0f, maxT);
				pT[ii] = T;

				if (ii<numNodes-1)
				{
					pDTg[ii] = Dot(d1, Vector(0.0f, G-Gf, 0.0f));
					T += Max(0.0f, pDTg[ii]);
					T = MinMax(T, 0.0f, maxT);
				}
			}
		}

		for (U32 ii = 0; ii<numNodes; ii++)
		{
			m_pTensionFriction[ii+start] = pFg[ii] + pNiT[ii]*pT[ii];
			PHYSICS_ASSERT(IsFinite(m_pTensionFriction[ii+start]));
		}

#if !FINAL_BUILD
		if (m_pDebugTension)
			memcpy(m_pDebugTension+start, pT, numNodes*sizeof(m_pDebugTension[0]));
#endif

		if (m_pEdgeSlideSounds)
		{
			Point mergedPoints[kMaxEdgeSlideSounds];
			Vector mergedEdges[kMaxEdgeSlideSounds];
			F32 mergedStrength[kMaxEdgeSlideSounds];
			F32 maxStrength[kMaxEdgeSlideSounds];
			F32 mergedSpeed[kMaxEdgeSlideSounds];
			U8 mergedPat[kMaxEdgeSlideSounds];
			U32 numMerged = 0;
			for (U32 ii = 0; ii<numNodes; ii++)
			{
				U32F iN = start + ii;
				F32 speed = Length(m_pVel[iN]);
				F32 s = speed * pAlfaF[ii];
				F32 speedDist = speed * m_scStepTime;
				if (pAlfaF[ii] > 0.05f*PI && s > 0.01f)
				{
					if (numMerged > 0 && DistSqr(m_pPos[iN], mergedPoints[numMerged-1]) < 0.09f)
					{
						//if (Abs(Dot(mergedEdges[numMerged-1], Vector(pEdges[ii]))) > 0.95f)
						{
							F32 sSum = mergedStrength[numMerged-1] + s;
							F32 f0 = mergedStrength[numMerged-1]/sSum;
							F32 f1 = 1.0f - f0;
							mergedPoints[numMerged-1] = kOrigin + (f0 * (mergedPoints[numMerged-1] - kOrigin) + f1 * (m_pPos[iN] - kOrigin));
							//mergedEdges[numMerged-1] = SafeNormalize(f0 * mergedEdges[numMerged-1] + f1 * Vector(pEdges[ii]), f0 > f1 ? mergedEdges[numMerged-1] : Vector(pEdges[ii]));
							mergedSpeed[numMerged-1] = f0 * mergedSpeed[numMerged-1] + f1 * speedDist;
							mergedStrength[numMerged-1] = sSum;
							if (s > maxStrength[numMerged-1])
							{
								mergedPat[numMerged-1] = pPatSurface[ii];
								maxStrength[numMerged-1] = s;
							}
							continue;
						}
					}
					ASSERT(numMerged < kMaxEdgeSlideSounds);
					if (numMerged == kMaxEdgeSlideSounds)
						break;

					mergedPoints[numMerged] = m_pPos[iN];
					//mergedEdges[numMerged] = Vector(pEdges[ii]);
					mergedSpeed[numMerged] = speedDist;
					mergedStrength[numMerged] = s;
					maxStrength[numMerged] = s;
					mergedPat[numMerged] = pPatSurface[ii];
					numMerged++;
				}
			}

			//for (U32 ii = 0; ii<numMerged; ii++)
			//{
			//	DebugDrawLine(mergedPoints[ii] - 0.25f*mergedEdges[ii], mergedPoints[ii] + 0.25f*mergedEdges[ii], kColorRed, kPrimDuration1FramePauseable);
			//	g_prim.Draw(DebugStringFmt(mergedPoints[ii], kColorWhite, 0.7f, "%.3f", mergedStrength[ii]), kPrimDuration1FramePauseable);
			//}

			// Match existing sounds with new ones
			for (U32 ii = 0; ii<numMerged; ii++)
			{
				bool bMatch = false;
				for (U32 iSfx = 0; iSfx<m_numEdgeSlideSounds; iSfx++)
				{
					if (Dist(mergedPoints[ii], m_pEdgeSlideSounds[iSfx].m_edgePos) < (0.3f + mergedSpeed[ii])) // && Abs(Dot(mergedEdges[ii], m_pEdgeSlideSounds[iSfx].m_edgeVec)) > 0.9f)
					{
						//F32 distLong = Dot(m_pEdgeSlideSounds[iSfx].m_edgePos-mergedPoints[ii], mergedEdges[ii]);
						//F32 distPerp = Dist(m_pEdgeSlideSounds[iSfx].m_edgePos, mergedPoints[ii] + distLong * mergedEdges[ii]);
						//if (distPerp < 0.1f * Abs(distLong))
						{
							// Match
							m_pEdgeSlideSounds[iSfx].m_edgePos = mergedPoints[ii];
							//m_pEdgeSlideSounds[iSfx].m_edgeVec = mergedEdges[ii];
							m_pEdgeSlideSounds[iSfx].m_strength = mergedStrength[ii];
							m_pEdgeSlideSounds[iSfx].m_patSurface = mergedPat[ii];
							if (mergedStrength[ii] > m_soundDef.m_minEdgeSlideStrength)
							{
								m_pEdgeSlideSounds[iSfx].m_coolDown = 0.0f;
							}
							bMatch = true;
							break;
						}
					}
				}

				if (!bMatch && mergedStrength[ii] > m_soundDef.m_minEdgeSlideStrength)
				{
					U32 iEnter = m_numEdgeSlideSounds;
					if (m_numEdgeSlideSounds == kMaxEdgeSlideSounds)
					{
						I32 iRemove = -1;
						F32 maxCoolDown = 0.0f;
						for (U32 iSfx = 0; iSfx<m_numEdgeSlideSounds; iSfx++)
						{
							if (m_pEdgeSlideSounds[iSfx].m_coolDown > maxCoolDown)
							{
								iRemove = iSfx;
								maxCoolDown = iSfx;
							}
						}
						if (iRemove > 0)
						{
							KillProcess(m_pEdgeSlideSounds[iRemove].m_hSound);
							iEnter = iRemove;
						}
					}
					else
					{
						m_numEdgeSlideSounds++;
					}
					if (iEnter < kMaxEdgeSlideSounds)
					{
						m_pEdgeSlideSounds[iEnter].m_edgePos = mergedPoints[ii];
						//m_pEdgeSlideSounds[iEnter].m_edgeVec = mergedEdges[ii];
						m_pEdgeSlideSounds[iEnter].m_strength = mergedStrength[ii];
						m_pEdgeSlideSounds[iEnter].m_coolDown = 0.0f;
						m_pEdgeSlideSounds[iEnter].m_patSurface = mergedPat[ii];
					}
				}
			}
		}
	}

#if !FINAL_BUILD
	if (m_pDebugPreProjPos)
	{
		memcpy(m_pDebugPreSlidePos+start, m_pPos+start, numNodes*sizeof(m_pPos[0]));
		memcpy(m_pDebugPreSlideCon+start, m_pConstr+start, numNodes*sizeof(m_pConstr[0]));
	}
#endif

	// Slide nodes along the rope to conforming to expected rope dist
	// This should help the solver in situations where to rope is being pulled quickly over some collision edges
	// where the collision and solver alone are not able to let the rope slide very quickly
	while (!g_ropeMgr.m_disablePostSlide)
	{
		static const F32 kMinSlideStretchFactor = 1.02f;
		static const F32 kSlideFactor = 0.5f;

		F32* pDist3d = NDI_NEW F32[numNodes];
		Point* pPos = NDI_NEW Point[numNodes];
		Constraint* pConstr = NDI_NEW Constraint[numNodes];

		F32 dist3d = 0.0f;
		pDist3d[0] = dist3d;
		for (U32 ii = 1; ii<numNodes; ii++)
		{
			dist3d += Dist(m_pPos[start+ii], m_pPos[start+ii-1]);
			pDist3d[ii] = dist3d;
		}

		if (dist3d == 0.0f)
			break;

		F32 len = m_pRopeDist[end] - m_pRopeDist[start];

		if (IsNodeKeyframedInt(end) && IsNodeKeyframedInt(start))
		{
			F32 f = len / dist3d;
			for (U32 ii = 0; ii<numNodes; ii++)
			{
				pDist3d[ii] *= f;
			}
			dist3d = len;
		}

		U32 iOld = 0;
		for (U32 ii = 0; ii<numNodes; ii++)
		{
			if (IsNodeKeyframedInt(start+ii))
				continue;
			F32 s = m_pRopeDist[start+ii] - m_pRopeDist[start];
			F32 d3d = pDist3d[ii];
			F32 ss;
			if (IsNodeKeyframedInt(start) && d3d > s*kMinSlideStretchFactor)
			{
				ss = s*kMinSlideStretchFactor + kSlideFactor * (d3d-s*kMinSlideStretchFactor);
			}
			else if (IsNodeKeyframedInt(end) && (dist3d-d3d) > (len-s)*kMinSlideStretchFactor)
			{
				ss = dist3d - ((len-s)*kMinSlideStretchFactor + kSlideFactor * (dist3d-d3d-(len-s)*kMinSlideStretchFactor));
			}
			else
			{
				pPos[ii] = m_pPos[start+ii];
				pConstr[ii] = m_pConstr[start+ii];
#if !FINAL_BUILD
				if (m_pDebugSlideTarget)
					m_pDebugSlideTarget[start+ii] = ii;
#endif
				continue;
			}

			while (ss > pDist3d[iOld+1] && iOld+1 < numNodes-1)
			{
				iOld++;
			}
			F32 f = MinMax01(ss-pDist3d[iOld])/(pDist3d[iOld+1]-pDist3d[iOld]);
			pPos[ii] = Lerp(m_pPos[start+iOld], m_pPos[start+iOld+1], f);
			if (f < 0.85f)
				m_pConstr[start+iOld].RelaxPos(pPos[ii], m_pRadius[start+ii]);
			if (f > 0.15f)
				m_pConstr[start+iOld+1].RelaxPos(pPos[ii], m_pRadius[start+ii]);
			pConstr[ii] = f < 0.5f ? m_pConstr[start+iOld] : m_pConstr[start+iOld+1];
			m_pTensionFriction[ii+start] = Lerp(pFg[iOld] + pNiT[iOld]*pT[iOld], pFg[iOld+1] + pNiT[iOld+1]*pT[iOld+1], f);
			PHYSICS_ASSERT(IsFinite(m_pTensionFriction[ii+start]));
#if !FINAL_BUILD
			if (m_pDebugSlideTarget)
				m_pDebugSlideTarget[start+ii] = start+iOld+f;
#endif
		}

		for (U32 ii = 0; ii<numNodes; ii++)
		{
			if (IsNodeKeyframedInt(start+ii))
				continue;
			m_pPos[start+ii] = pPos[ii];
			m_pConstr[start+ii] = pConstr[ii];
		}

		break;
	}

	BackstepVelocities(start, end, m_scStepTime);
	for (U32 ii = 0; ii<numNodes; ii++)
	{
		if (IsNodeKeyframedInt(start+ii))
			continue;
		Vector velProj = pVelSolver[ii] - pVelProject[ii];
		Vector velSlide = m_pVel[start+ii] - pVelProject[ii];
		Scalar projLen;
		Vector projDir = Normalize(velProj, projLen);
		Scalar slideLen;
		Vector slideDir = Normalize(velSlide, slideLen);
		if (projLen > slideLen)
		{
			m_pVel[start+ii] = pVelSolver[ii] + velSlide - Dot(velSlide, projDir); 
		}
		else if (slideLen > 0.0f)
		{
			m_pVel[start+ii] = m_pVel[start+ii] + velProj - Dot(velProj, slideDir); 
		}
	}
}

void Rope2::SolverStep()
{
	PROFILE(Rope, Rope_SolverStep);

	ResetNodeDebugInfo();

	if (m_lastDynamicPoint < 0 || m_firstDynamicPoint < 0)
		return;

	//RelaxPositionsFromConstraints(0, m_numPoints-1);

	// Precalculate for bending test
	{
		F32 halfFreeBendAnglePerSeg = Min(m_fFreeBendAngle * m_fSegmentLength, 0.5f*PI);
		m_fBendingMinE = Max(FLT_EPSILON, Sin(halfFreeBendAnglePerSeg) * m_fSegmentLength);
		m_fBendingMinMR = Max(FLT_EPSILON, Tan(halfFreeBendAnglePerSeg));
	}

	I32F startNode = m_firstDynamicPoint > 0 ? m_firstDynamicPoint-1 : m_firstDynamicPoint;
	I32F numPoints = m_lastDynamicPoint < m_numPoints-1 ? m_lastDynamicPoint+2 : m_numPoints;

	if (m_useGpu && g_ropeUseGpu && numPoints - startNode > 31)
	{
		// GPU
		OpenCompute();
		RelaxPositionsCompute(startNode, numPoints-1);
		CloseCompute();
		WaitAndGatherCompute();
	}
	else
	{
		// CPU
		if (m_pGpuWaitCounter)
		{
			ndjob::FreeCounter(m_pGpuWaitCounter);
			m_pGpuWaitCounter = nullptr;
		}

		// Per sections
		U32 maxNumSections = Max(m_numKeyPoints*2u, 1u);
		LooseSectionData* pSections = STACK_ALLOC(LooseSectionData, maxNumSections);
		ALWAYS_ASSERT(pSections);
		U32F numSections = 0;

		U32F numJobs = 0;
		U32F numDependentSections = 0;
		U32F numGpuJobs = 0;
		while (startNode < m_lastDynamicPoint)
		{
			while (startNode < numPoints-1 && (m_pNodeFlags[startNode+1] & (kNodeStrained|kNodeKeyframed)) != 0)
				startNode++;
			if (startNode >= numPoints-1)
				break; // end of rope
			I32F endNode = startNode+1;
			while (endNode < numPoints-1 && (m_pNodeFlags[endNode] & (kNodeStrained|kNodeKeyframed)) == 0)
				endNode++;
			startNode = Max(startNode, 0);

			PHYSICS_ASSERT(numSections < maxNumSections);
			pSections[numSections].m_pRope = this;
			pSections[numSections].m_start = startNode;
			pSections[numSections].m_end = endNode;
			pSections[numSections].m_indep = (startNode == 0 || (m_pNodeFlags[startNode-1] & (kNodeKeyframed|kNodeStrained))) && (endNode == numPoints-1 || (m_pNodeFlags[endNode+1] & (kNodeKeyframed|kNodeStrained)));
			if (pSections[numSections].m_indep)
				numJobs++;
			else
				numDependentSections++;
			numSections++;

			startNode = endNode;
		}

		if (numDependentSections == 0 && numJobs)
			numJobs--;

		ndjob::JobDecl* pJobDecl = nullptr; 
		ndjob::CounterHandle pCounter = nullptr;
		if (numJobs)
		{
			pJobDecl = STACK_ALLOC_ALIGNED(ndjob::JobDecl, numJobs, kAlign64); 
			pCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, numJobs);
			ALWAYS_ASSERT(pJobDecl);
		}

		for (U32F iSec = 0; iSec < numSections; iSec++)
		{
			if (pSections[iSec].m_indep && numJobs && pJobDecl)
			{
				numJobs--;
				pJobDecl[numJobs] = ndjob::JobDecl(SolveLooseSectionJob, (uintptr_t)(pSections+iSec));
				pJobDecl[numJobs].m_associatedCounter = pCounter;
				ndjob::RunJobs(&pJobDecl[numJobs], 1, nullptr, FILE_LINE_FUNC);
			}
			else
			{
				SolveLooseSection(pSections[iSec].m_start, pSections[iSec].m_end);
			}
		}

		if (pCounter)
		{
			ndjob::WaitForCounterAndFree(pCounter);
		}
	}

	if (!m_pGpuWaitCounter)
		PostGatherCompute();
}

void Rope2::PostGatherCompute()
{
	RelaxVelExternalConstraints();
	CheckSleeping();
}

void Rope2::RelaxPositionsTowardsStraight(U32 iStart, U32 iEnd)
{
	if (!IsNodeKeyframedInt(iStart) || !IsNodeKeyframedInt(iEnd))
	{
		return;
	}

	Scalar ropeLen = m_pRopeDist[iEnd] - m_pRopeDist[iStart];
	Scalar straightDist = Min(ropeLen, Scalar(GetStraightDist(m_pRopeDist[iStart], m_pRopeDist[iEnd])));

	Scalar distF = straightDist / ropeLen;
	if (distF < 0.7f)
	{
		return;
	}

	Scalar f = (distF - 0.7f) / 0.1f;

	Scalar simLen(kZero);
	for (U32F ii = iStart + 1; ii <= iEnd; ii++)
	{
		simLen += Dist(m_pPos[ii], m_pPos[ii - 1]);
	}

	if (simLen <= ropeLen)
		return;

	Scalar b = (simLen - ropeLen) / (simLen - straightDist) * f * 0.5f;

	for (U32F ii = iStart + 1; ii<iEnd; ii++)
	{
		m_pPos[ii] += (GetStraightPos(m_pRopeDist[ii]) - m_pPos[ii]) * b;
		PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
	}
}

void Rope2::RelaxPositions(U32 iStart, U32 iEnd)
{
	PROFILE(Havok, Rope_RelaxPositions);

	//RelaxPositionsTowardsStraight(iStart, iEnd);

	U32F numEdges = iEnd-iStart;

	F32 multiGridItersPerEdge = m_fNumMultiGridItersPerEdge; //(IsNodeKeyframedInt(iStart) && IsNodeKeyframedInt(iEnd)) ? m_fNumMultiGridItersPerEdge : m_fNumMultiGridItersPerEdgeFreeEnd;
	if (multiGridItersPerEdge * numEdges > 2.0f)
	{
		// First do this once to get some "working constraints" approximate before multigrid hides it all
		RelaxPositionsFromConstraints(iStart, iEnd);

		RelaxPositionsMultiGrid(iStart, iEnd, multiGridItersPerEdge);
	}

	F32 ropeLen = m_pRopeDist[iEnd] - m_pRopeDist[iStart];
	F32 resTol = Sqr(g_unstrechTolerance * ropeLen);
	U32 numIterations = 10 + (U32F)(numEdges * m_fNumItersPerEdge);
#ifdef STRAIGTEN_SUB_STEP
	F32 straightLen = Min(ropeLen, GetStraightDist(m_pRopeDist[iStart], m_pRopeDist[iEnd]));
#endif

	SMath::Vec4 resid = kZero;
	U32 numIter = 0;
	for(; numIter < numIterations; ++numIter)
	{
		resid = Vec4(kZero);

		RelaxPositionsForBending(iStart, iEnd);

		// This version preserves constraint but may violate the segment length
		if (!IsNodeKeyframedInt(iStart))
		{
			resid += RelaxEdgePos(iStart);
			resid += RelaxPosExternalConstraints(iStart);
		}
		else
		{
			resid += RelaxEdgePosOneSide(iStart, iStart+1);
		}
		for(U32 ii = iStart+1; ii < iEnd-1; ++ii)
		{
			resid += RelaxEdgePos(ii);
			resid += RelaxPosExternalConstraints(ii);
		}

		RelaxPositionsForBending(iEnd, iStart);

		if (m_pDistConstraints)
			resid += RelaxPosDistanceConstraints(iStart, iEnd);

		if (m_pFrictionConstraints)
			resid += RelaxPosFrictionConstraints(iStart, iEnd);

		if (!IsNodeKeyframedInt(iEnd))
		{
			resid += RelaxEdgePos(iEnd-1);
			resid += RelaxPosExternalConstraints(iEnd);
		}
		else
		{
			resid += RelaxEdgePosOneSide(iEnd, iEnd-1);
		}
		resid += RelaxPosExternalConstraints(iEnd-1);

		for(I32F ii = (I32F)iEnd-2; ii > (I32F)iStart; --ii)
		{
			resid += RelaxEdgePos(ii);
			resid += RelaxPosExternalConstraints(ii+1);
		}
		resid += RelaxPosExternalConstraints(iStart+1);
		if (!IsNodeKeyframedInt(iStart))
		{
			resid += RelaxPosExternalConstraints(iStart);
		}

		//if( Sum3(resid) < resTol )
		//	break;

#ifdef STRAIGTEN_SUB_STEP
		if (IsNodeKeyframedInt(iStart) && IsNodeKeyframedInt(iEnd))
		{
			if (ropeLen < 1.1f*straightLen)
			{
				F32 len = 0;
				for (U32F ii = iStart+1; ii<=iEnd; ii++)
				{
					len += Dist(m_pPos[ii-1], m_pPos[ii]);
				}

				if (len > ropeLen)
				{
					F32 f = g_straightenRelax * (ropeLen - len) / (straightLen - len);
					for (U32F ii = iStart+1; ii<iEnd; ii++)
					{
						m_pPos[ii] += (GetStraightPos(m_pRopeDist[ii]) - m_pPos[ii]) * f;
						PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
					}
				}
			}
		}
#endif
	}

#ifdef STRAIGTEN_STEP
	if (/*numIter >= numIterations && */IsNodeKeyframedInt(iStart) && IsNodeKeyframedInt(iEnd))
	{
		F32 straightLen = Min(ropeLen, GetStraightDist(m_pRopeDist[iStart], m_pRopeDist[iEnd]));
		if (ropeLen < 1.1f*straightLen)
		{
			F32 len = 0;
			for (U32F ii = iStart+1; ii<=iEnd; ii++)
			{
				len += Dist(m_pPos[ii-1], m_pPos[ii]);
			}

			if (len > ropeLen)
			{
				F32 f = g_straightenRelax * (ropeLen - len) / (straightLen - len);
				for (U32F ii = iStart+1; ii<iEnd; ii++)
				{
					m_pPos[ii] += (GetStraightPos(m_pRopeDist[ii]) - m_pPos[ii]) * f;
					PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
				}
			}
		}
	}
#endif

#ifdef JSINECKYx
	//MsgPhys("Iter: %i\n", numIter);
	if (numIter >= numIterations)
		MsgPhys("Relaxation error after %i iter: %f (%f per edge)\n", numIterations, (F32)(resid.X() + resid.Y() + resid.Z()), (F32)(resid.X() + resid.Y() + resid.Z())/(iEnd-iStart));
#endif
}

Vec4 Rope2::RelaxPositionsGridLevel(U32 iStart, U32 iEnd, U32* pGroups)
{
	Vec4 resid(kZero);
	U32F group = 0;
	U32F ii = iStart;
	U32F jj = ii + pGroups[group++];
	if (!IsNodeKeyframedInt(ii))
	{
		resid += RelaxEdgePosUnstretchOnly(ii, jj);
		resid += RelaxPosExternalConstraints(ii);
	}
	else
	{
		resid += RelaxEdgePosOneSideUnstretchOnly(ii, jj);
	}

	while (1)
	{
		ii = jj;
		jj += pGroups[group++];
		if (jj >= iEnd)
			break;
		resid += RelaxEdgePosUnstretchOnly(ii, jj);
		resid += RelaxPosExternalConstraints(ii);
	}

	ASSERT(jj == iEnd);
	if (!IsNodeKeyframedInt(jj))
	{
		resid += RelaxEdgePosUnstretchOnly(ii, jj);
		resid += RelaxPosExternalConstraints(jj);
	}
	else
	{
		resid += RelaxEdgePosOneSideUnstretchOnly(jj, ii);
	}
	resid += RelaxPosExternalConstraints(ii);

	group -= 2;
	while (1)
	{
		jj = ii;
		ii -= pGroups[group--];
		if (ii <= iStart)
			break;
		resid += RelaxEdgePosUnstretchOnly(ii, jj);
		resid += RelaxPosExternalConstraints(jj);
	}

	ASSERT(ii == iStart);
	resid += RelaxPosExternalConstraints(jj);
	if (!IsNodeKeyframedInt(ii))
	{
		resid += RelaxPosExternalConstraints(ii);
	}

	if (m_pDistConstraints)
	{
		group = 0;
		ii = iStart;
		while (1)
		{
			resid += RelaxPosDistanceConstraint(ii);
			if (ii >= iEnd)
				break;
			ii += pGroups[group++];
		}
	}

	return resid;
}

U32F Rope2::PrepareNextGridLevel(U32F iStart, U32F iEnd, const Point* pStartPos, U32F* pGroups0, U32F* pGroups1, U32F*& pGroups, U32F& numGroups)
{
	U32F* pNewGroups = pGroups == pGroups0 ? pGroups1 : pGroups0;

	U32F group = 0;
	U32F newGroup = 0;
	U32F lastNode = iStart;
	U32F largestGroupSize = 0;
	U32F ii = iStart;
	do
	{
		U32F groupSize = pGroups[group++];
		if (groupSize > 1)
		{
			// Sub divide group
			U32F parentA = ii;
			U32F parentB = ii + groupSize;
			U32F subGroupSize = groupSize / 2;
			pNewGroups[newGroup++] = subGroupSize;
			ii += subGroupSize;

			// Move newly included node by proportional movement of it's coarse level neighbors
			F32 fA = (m_pRopeDist[ii] - m_pRopeDist[parentA]) / (m_pRopeDist[parentB] - m_pRopeDist[parentA]);
			m_pPos[ii] += fA * (m_pPos[parentA] - pStartPos[parentA-iStart]);
			m_pPos[ii] += (1.0f - fA) * (m_pPos[parentB] - pStartPos[parentB-iStart]);
			PHYSICS_ASSERT(IsFinite(m_pPos[ii]));

			groupSize = groupSize - subGroupSize;
		}
		pNewGroups[newGroup++] = groupSize;
		ii += groupSize;
		largestGroupSize = Max(largestGroupSize, groupSize);
	} while (ii < iEnd);
	ASSERT(ii == iEnd);

	pGroups = pNewGroups;
	numGroups = newGroup;
	return largestGroupSize;
}

void Rope2::RelaxPositionsMultiGrid(U32 iStart, U32 iEnd, F32 itersPerEdge)
{
	PROFILE(Havok, Rope_MultiGrid);

	U32F numEdges = iEnd-iStart;
	F32 ropeLen = m_pRopeDist[iEnd] - m_pRopeDist[iStart];
	F32 resTol = Sqr(g_unstrechTolerance * ropeLen);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	Point* pStartPos = NDI_NEW Point[numEdges+1];
	memcpy(pStartPos, m_pPos+iStart, (numEdges+1)*sizeof(Point));

	U32F* pGroups0 = NDI_NEW U32F[2*numEdges];
	U32F* pGroups1 = pGroups0 + numEdges;
	U32F* pGroups = pGroups0;

	// First (the coarsest level) will be composed of all edging nodes
	U32F group = 0;
	U32F lastNode = iStart;
	U32F largestGroupSize = 0;
	if (g_ropeMultiGridStartOnEdges)
	{
		for(U32F ii = iStart+1; ii <= iEnd-1; ii++)
		{
			if (m_pConstr[ii].m_numEdges > 0)
			{
				U32F groupSize = ii - lastNode;
				largestGroupSize = Max(largestGroupSize, groupSize);
				pGroups[group++] = groupSize;
				lastNode = ii;
			}
		}
	}
	U32F groupSize = iEnd - lastNode;
	largestGroupSize = Max(largestGroupSize, groupSize);
	pGroups[group++] = groupSize;
	if (group == 1)
	{
		// No edges, subdivide
		largestGroupSize = PrepareNextGridLevel(iStart, iEnd, pStartPos, pGroups0, pGroups1, pGroups, group);
	}

	while (largestGroupSize > 1)
	{
		U32F numIterations = (U32F)((F32)numEdges / (F32)largestGroupSize * itersPerEdge);
		for (U32F ii = 0; ii<numIterations; ii++)
		{
			Vec4 resid = RelaxPositionsGridLevel(iStart, iEnd, pGroups);
			if( resid.X() + resid.Y() + resid.Z() < resTol )
				break;
		}
		largestGroupSize = PrepareNextGridLevel(iStart, iEnd, pStartPos, pGroups0, pGroups1, pGroups, group);
	}
}

void Rope2::RelaxPositionsFromConstraints(U32 iStart, U32 iEnd)
{
	if (!IsNodeKeyframedInt(iStart))
	{
		RelaxPosExternalConstraints(iStart);
	}
	for(U32 ii = iStart+1; ii < iEnd-1; ++ii)
	{
		RelaxPosExternalConstraints(ii);
	}
	if (!IsNodeKeyframedInt(iEnd))
	{
		RelaxPosExternalConstraints(iEnd);
	}

	CheckStateSane(iStart, iEnd);
}

/*U32 Rope2::FindClosestStrainedCollision(U32F iStart, U32F iEnd)
{
	I32F iStep = iEnd > iStart ? 1 : -1;
	U32F iTest, iNext;
	for (iTest = iStart+iStep; iTest!=iEnd; iTest += iStep)
	{
		if (m_pConstr[iTest].IsInContact())
			break;
		m_pNodeFlags[iTest] &= ~kNodeCollision;
	}
	while (iTest != iEnd)
	{
		for (iNext = iTest+iStep; iNext!=iEnd; iNext += iStep)
		{
			if (m_pConstr[iNext].IsInContact())
				break;
		}
		Vector lineVec = SafeNormalize(m_pPos[iNext] - m_pPos[iStart], kZero);
		Point testReleasePos = m_pPos[iStart] + Dot(m_pPos[iTest] - m_pPos[iStart], lineVec) * lineVec;
		Vector testVec = testReleasePos - m_pPos[iTest];
		if (m_pConstr[iTest].CheckContactDir(testVec))
		{
			m_pNodeFlags[iTest] |= kNodeCollision;
			return iTest;
		}
		else
		{
			iTest = iNext;
		}
		m_pNodeFlags[iTest] &= ~kNodeCollision;
	}
	return iEnd;
}*/

/*U32 Rope2::FindClosestStrainedCollision(U32F iStart, U32F iEnd)
{
	I32F iStep = iEnd > iStart ? 1 : -1;
	for (U32F iTest = iStart+iStep; iTest!=iEnd; iTest += iStep)
	{
		if (m_pConstr[iTest].IsInContact())
		{
			Vector v1 = SafeNormalize(m_pPos[iTest-iStep] - m_pPos[iTest], kZero);
			//Vector v10 = SafeNormalize(m_pPos[iStart] - m_pPos[iTest], kZero);
			Vector v2 = SafeNormalize(m_pPos[iTest+iStep] - m_pPos[iTest], kZero);
			//Vector v20 = SafeNormalize(m_pPos[iEnd] - m_pPos[iTest], kZero);
			Vector testVec = v1 + v2;
			if (m_pConstr[iTest].CheckContactDir(testVec))
			{
				m_pNodeFlags[iTest] |= kNodeCollision;
				return iTest;
			}
		}
		m_pNodeFlags[iTest] &= ~kNodeCollision;
	}
	return iEnd;
}*/

void Rope2::CheckStateSane(U32F iStart, U32F iEnd)
{
#ifdef JSINECKY0
	bool sane = true;
	for (U32F ii = iStart; ii<=iEnd; ii++)
	{
		sane = sane && IsFinite(m_pPos[ii]) && Length(m_pPos[ii]) < 10000.0f;
		ALWAYS_ASSERT(sane);
		sane = sane && IsFinite(m_pVel[ii]) && Length(m_pVel[ii]) < 10000.0f;
		ALWAYS_ASSERT(sane);
	}
	if (!sane)
	{
		g_ndConfig.m_pDMenuMgr->SetProgPause(true);
	}
#endif
}

void Rope2::RelaxPositionsForBending(U32 aStart, U32 aEnd)
{
	if (m_fBendingStiffness == 0.0f)
		return;

	Scalar scBendingStiffnes = Min(1.0f, m_fBendingStiffness * m_scStepTime * 30.0f);

	I32F iStart = aStart;
	I32F iEnd = aEnd;
	I32F iInc = iEnd > iStart ? 1 : -1;
	I32F numNodes = (iEnd-iStart)*iInc + 1;

	if (!IsNodeKeyframedInt(iStart))
	{
		if (numNodes >= 3 && !IsNodeKeyframedInt(iStart+2*iInc))
		{
			RelaxVertexForBending(iStart+iInc, scBendingStiffnes);
		}
	}
	else
	{
		if (iStart-iInc >= 0 && iStart-iInc < m_numPoints)
		{
			RelaxVertexForBendingBoundary(iStart-iInc, iStart, scBendingStiffnes);
		}
		if (numNodes >= 3 && !IsNodeKeyframedInt(iStart+2*iInc))
		{
			RelaxVertexForBending2Nodes(iStart, iStart+iInc, iStart+2*iInc, scBendingStiffnes);
		}
	}

	if (numNodes >= 4)
	{
		for(U32F ii = iStart+2*iInc; ii != iEnd-iInc; ii += iInc)
		{
			RelaxVertexForBending(ii, scBendingStiffnes);
		}
	}

	if (!IsNodeKeyframedInt(iEnd))
	{
		if (numNodes >= 3 && !IsNodeKeyframedInt(iEnd-2*iInc))
		{
			RelaxVertexForBending(iEnd-iInc, scBendingStiffnes);
		}
	}
	else
	{
		if (numNodes >= 3 && !IsNodeKeyframedInt(iEnd-2*iInc))
		{
			RelaxVertexForBending2Nodes(iEnd, iEnd-iInc, iEnd-2*iInc, scBendingStiffnes);
		}
		if (iEnd+iInc >= 0 && iEnd+iInc < m_numPoints)
		{
			RelaxVertexForBendingBoundary(iEnd+iInc, iEnd, scBendingStiffnes);
		}
	}
}

#define IRREG_DISCRET_ELIM 0

void Rope2::RelaxVertexForBending(U32F i, const Scalar& scBendingStiffnes)
{
	// Moves the vertex and the two neighbors into the straight line in a way that we
	// conserve linear and angular momentum
	// See the notes "Bending stiffness" in my drawer ;)

	ASSERT(i >= 1 && i < (m_numPoints - 1));

	Point p0 = m_pPos[i-1];
	Point p1 = m_pPos[i+0];
	Point p2 = m_pPos[i+1];

	F32 w0 = m_pInvRelMass[i-1];
	F32 w1 = m_pInvRelMass[i+0];
	F32 w2 = m_pInvRelMass[i+1];

#if IRREG_DISCRET_ELIM

	// This is nice for GPU but slower on CPU 

	Vector l0Vec = p0 - p1;
	Scalar l0;
	Vector l0Dir = Normalize(l0Vec, l0);

	Vector l2Vec = p2 - p1;
	Scalar l2;
	Vector l2Dir = Normalize(l2Vec, l2);

	if (l0 < FLT_EPSILON || l2 < FLT_EPSILON)
		return;

	Point p0R = p1 + l0Dir;
	Point p2R = p1 + l2Dir;

	Scalar dR;
	Vector dDir = Normalize(p2R - p0R, dR);
	if (dR < FLT_EPSILON)
		return;

	Vector vecE = p1 - Lerp(p0R, p2R, Scalar(0.5f));
	Scalar eR;
	Vector biDir = Normalize(vecE, eR);

	Scalar mR = eR/dR;

	if (mR <= m_fBendingMinMR)
		return;

	if (mR > 5.0f)
		return;

	Scalar d0 = -Dot(l0Vec, dDir);
	Scalar d2 = Dot(l2Vec, dDir);
	ASSERT(d0 > Scalar(kZero) && d2 > Scalar(kZero));
	Scalar d0Sqr = d0 * d0;
	Scalar d2Sqr = d2 * d2;
	Scalar d = d0 + d2;
	Scalar dSqr = d * d;

	Scalar h = d2Sqr * w0 + dSqr * w1 + d0Sqr * w2;
	Vector sa = scBendingStiffnes * 2.0f * (mR-m_fBendingMinMR) * biDir / h;
	Vector sa0 = sa * d2Sqr * d0;
	Vector sa2 = sa * d0Sqr * d2;
	Vector sa1 = - sa0 - sa2;

	Vector s0 = sa0 * w0;
	Vector s1 = sa1 * w1;
	Vector s2 = sa2 * w2;

	m_pPos[i-1] += s0;
	m_pPos[i+0] += s1;
	m_pPos[i+1] += s2;
	JAROS_ASSERT(IsFinite(m_pPos[i-1]));
	JAROS_ASSERT(IsFinite(m_pPos[i+0]));
	JAROS_ASSERT(IsFinite(m_pPos[i+1]));

#else

	Scalar d;
	Vector dir = Normalize(p2-p0, d);
	if (d <= FLT_EPSILON)
		return;

	Scalar d1 = Dot(p1-p0, dir);
	Vector vecE = p1 - (p0 + d1*dir);
	Scalar e;
	Vector biDir = Normalize(vecE, e);

	if (e <= m_fBendingMinE)
		return;

	Scalar d2 = d-d1;

	if (d2 <= 0.2f*e)
	{
		Vector s = 2.0f * (-d2 + e) * dir;
		Vector s0 = s * w0 / (w0 + w2);
		Vector s2 = s - s0;
		m_pPos[i-1] -= s0;
		m_pPos[i+1] += s2;
		JAROS_ASSERT(IsFinite(m_pPos[i-1]));
		JAROS_ASSERT(IsFinite(m_pPos[i+1]));
	}
	/*else if (e > 0.75f*d)
	{
		Vector s = g_bendingStiffness * (e - 0.5f*d) * dir;
		m_pPos[i-1] -= s;
		m_pPos[i+1] += s;
	}*/
	else
	{
		Scalar h = d*d * w1 + d1*d1 * w2 + d2*d2 * w0;
		Vector sa = scBendingStiffnes * (e-m_fBendingMinE)*d/h * biDir;
		Vector s1 = sa * d2 * w0; 
		Vector s2 = -sa * d * w1;
		Vector s3 = sa * d1 * w2;

		m_pPos[i-1] += s1;
		m_pPos[i+0] += s2;
		m_pPos[i+1] += s3;
		JAROS_ASSERT(IsFinite(m_pPos[i-1]));
		JAROS_ASSERT(IsFinite(m_pPos[i+0]));
		JAROS_ASSERT(IsFinite(m_pPos[i+1]));
	}
#endif
}

void Rope2::RelaxVertexForBending2Nodes(U32F iKeyframed, U32F i1, U32F i2, const Scalar& scBendingStiffnes)
{
	Point p0 = m_pPos[iKeyframed];
	Point p1 = m_pPos[i1];
	Point p2 = m_pPos[i2];

	F32 w1 = m_pInvRelMass[i1];
	F32 w2 = m_pInvRelMass[i2];

	Scalar d;
	Vector dir = Normalize(p2-p0, d);
	if (d <= FLT_EPSILON)
		return;

	Scalar d1 = Dot(p1-p0, dir);
	Vector vecE = p1 - (p0 + d1*dir);
	Scalar e;
	Vector biDir = Normalize(vecE, e);

	if (e < m_fBendingMinE)
		return;

	Scalar d2 = d-d1;

	if (d2 <= 0.2f*e)
	{
		Vector s = (-d2 + e) * dir;
		m_pPos[i2] += s;
		JAROS_ASSERT(IsFinite(m_pPos[i2]));
	}
	/*else if (e > 0.75f*d)
	{
		Vector s = scBendingStiffnes * (2.0f*e - d) * dir;
		m_pPos[i2] += s;
	}*/
	else
	{
		Scalar h = d*d * w2 + d1*d1 * w1;
		Vector sa = scBendingStiffnes * (e-m_fBendingMinE)*d/h * biDir;
		Vector s2 = -sa * d * w1;
		Vector s3 = sa * d1 * w2;

		m_pPos[i1] += s2;
		m_pPos[i2] += s3;
		JAROS_ASSERT(IsFinite(m_pPos[i1]));
		JAROS_ASSERT(IsFinite(m_pPos[i2]));
	}
}

void Rope2::RelaxVertexForBendingBoundary(U32F iKeyframed1, U32F iKeyframed2, const Scalar& scBendingStiffnes)
{
	I32F iInc = iKeyframed2 - iKeyframed1;

	RelaxVertexForBending1Node(iKeyframed1, iKeyframed2, iKeyframed2+iInc, scBendingStiffnes, g_boundaryBendingCorrect);

	I32F i2 = iKeyframed2+2*iInc;
	if (i2 < m_numPoints && i2 >= 0 && !IsNodeKeyframedInt(i2))
	{
		Scalar d2 = iInc * (m_pRopeDist[i2] - m_pRopeDist[iKeyframed2]);
		Scalar kf = (2.0f*m_fSegmentLength+0.01f-d2) / m_fSegmentLength;
		//ASSERT(kf >= 0.0f && kf<= 1.0f);
		kf = MinMax(kf, Scalar(kZero), Scalar(1.0f));
		RelaxVertexForBending1Node(iKeyframed1, iKeyframed2, i2, scBendingStiffnes, kf*g_boundaryBendingCorrect);
	}
}

#define USE_ANGLE 1

void Rope2::RelaxVertexForBending1Node(U32F iKeyframed1, U32F iKeyframed2, U32F i, const Scalar& scBendingStiffnes, Scalar_arg kf)
{
	Point p0 = m_pPos[iKeyframed1];
	Point p1 = m_pPos[iKeyframed2];
	Point p2 = m_pPos[i];

#if USE_ANGLE

	Scalar d1;
	Vector dir1 = Normalize(p1-p0, d1);
	if (d1 <= FLT_EPSILON)
		return;

	Scalar d2;
	Vector dir2 = Normalize(p2-p1, d2);
	if (d2 <= FLT_EPSILON)
		return;

	Scalar x = Dot(dir1, dir2);
	Vector vecE = dir2 - x*dir1;
	Scalar y;
	Vector biDir = Normalize(vecE, y);
	if (y < 0.0001f)
		return;

	Scalar alfa = Atan2(y, x); // this alfa is always positive
	Scalar freeAlfa = m_fFreeBendAngle * Abs(m_pRopeDist[i]-m_pRopeDist[iKeyframed2]);
	if (alfa <= freeAlfa)
		return;

	Scalar k = scBendingStiffnes * kf;
	if (!IsNodeKeyframedInt(iKeyframed1))
	{
		// If the node on the other side is not actually keyframed, we will reduce the projection to half because the other 
		// point will do the other half
		k *= 0.5f;
	}
	alfa = freeAlfa + (1.0f - k) * (alfa-freeAlfa);

	Scalar x1, y1;
	SinCos(alfa, y1, x1);
	m_pPos[i] = m_pPos[iKeyframed2] + d2 * x1 * dir1 + d2 * y1 * biDir;
	JAROS_ASSERT(IsFinite(m_pPos[i]));

#else

	Scalar d;
	Vector dir = Normalize(p2-p0, d);
	if (d <= FLT_EPSILON)
		return;

	Scalar d1 = Dot(p1-p0, dir);
	Vector vecE = p1 - (p0 + d1*dir);
	Scalar e;
	Vector biDir = Normalize(vecE, e);
	if (e < 0.0001f)
		return;

	Scalar d2 = d-d1;

	Vector s;
	if (Min(d1, d2) <= 0.2f*e)
	{
		s = (-d2 + e) * dir;
	}
	/*else if (e > 0.75f*d)
	{
		s = scBendingStiffnes * (2.0f*e - d) * dir;
	}*/
	else
	{
		s = scBendingStiffnes * e*d/d1 * biDir;
	}

	if (!IsNodeKeyframedInt(iKeyframed1))
	{
		// If the node on the other side is not actually keyframed, we will reduce the projection to half because the other 
		// point will do the other half
		s *= 0.5f;
	}

	m_pPos[i] += s*kf;
	JAROS_ASSERT(IsFinite(m_pPos[i]));

#endif
}
