/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/voxel-mgr.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/level/level.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

NpcVoxelMgr g_npcVoxelMgr;

const F32 Voxelization::kBigVoxelSize	= 2.f;
const F32 Voxelization::kSmallVoxelSize	= kBigVoxelSize / kVoxelsPerAxis;

void Voxelization::Login(Level* pLevel)
{
	g_npcVoxelMgr.AddVoxelization(this, pLevel);
}

void Voxelization::Logout(Level* pLevel)
{
	g_npcVoxelMgr.RemoveVoxelization(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// 3D Bresenham implementation adapted from here:
// https://gist.github.com/yamamushi/5823518
bool Voxelization::Raycast(Point_arg fromWs, Point_arg toWs, Point* pHitPos/*=nullptr*/, bool debug/*=false*/) const
{
	PROFILE(AI, VoxelRaycast);

	Point fromClipWs = fromWs;
	Point toClipWs = toWs;
	const Aabb aabb(m_minPosWs, m_maxPosWs);
	Scalar tMin, tMax;
	const bool intersects = aabb.IntersectSegment(fromClipWs, toClipWs, tMin, tMax);

	if (!intersects)
		return false;

	tMin = Clamp(tMin, Scalar(0.f), Scalar(1.f));
	tMax = Clamp(tMax, Scalar(0.f), Scalar(1.f));

	fromClipWs = Lerp(fromClipWs, toClipWs, tMin);
	toClipWs = Lerp(fromClipWs, toClipWs, tMax);

	const Point fromLs = WorldToLocal(fromClipWs);
	const Point toLs = WorldToLocal(toClipWs);

	const Voxel fromVoxelLs(fromLs);
	const Voxel toVoxelLs(toLs);

	I32 bigVoxelIdx;
	I32 temp;

#if 0
	g_prim.Draw(DebugBox(m_minPosWs, m_maxPosWs, kColorOrange, PrimAttrib(kPrimEnableWireframe)));
	g_prim.Draw(DebugLine(fromWs, toWs, kColorPurple, kColorPink));
	g_prim.Draw(DebugCross(fromClipWs, 0.5f, kColorPurple));
	g_prim.Draw(DebugCross(toClipWs, 0.5f, kColorPink));
#endif

	const bool success = GetIndices(fromVoxelLs, &bigVoxelIdx, &temp);
	if (!success)
		return false;

    const I32 dx = toVoxelLs.x - fromVoxelLs.x;
    const I32 dy = toVoxelLs.y - fromVoxelLs.y;
    const I32 dz = toVoxelLs.z - fromVoxelLs.z;

    const U32 l = abs(dx);
    const U32 m = abs(dy);
    const U32 n = abs(dz);

    const U32 dx2 = l << 1;
    const U32 dy2 = m << 1;
    const U32 dz2 = n << 1;

    const I32 xInc = (dx < 0) ? -1 : 1;
    const I32 yInc = (dy < 0) ? -1 : 1;
    const I32 zInc = (dz < 0) ? -1 : 1;

	// Vs = Voxel Space, local to a big voxel
	const I32 xVs = fromVoxelLs.x % kVoxelsPerAxis;
	const I32 yVs = fromVoxelLs.y % kVoxelsPerAxis;
	const I32 zVs = fromVoxelLs.z % kVoxelsPerAxis;

	Voxel currVoxelVs(xVs, yVs, zVs);

	const U32 xCount = m_count[0];
	const U32 yCount = m_count[1];
	const U32 zCount = m_count[2];

    if ((l >= m) && (l >= n))
	{
        I32 err1 = dy2 - l;
        I32 err2 = dz2 - l;
		for (U32 i = 0; i < l; i++)
		{
			if (bigVoxelIdx >= m_numBigVoxels || bigVoxelIdx < 0)
				return false;

			const BigVoxel* pBigVoxel = m_ppBigVoxels[bigVoxelIdx];

#if 0
			{
				U32 tempBigVoxelIdx = bigVoxelIdx;
				const U32 bigZ = tempBigVoxelIdx / (xCount * yCount);
				tempBigVoxelIdx -= bigZ * (xCount * yCount);

				const U32 bigY = tempBigVoxelIdx / xCount;
				tempBigVoxelIdx -= bigY * xCount;

				const U32 bigX = tempBigVoxelIdx;

				const Vector smallVoxelSize = Vector(kSmallVoxelSize, kSmallVoxelSize, kSmallVoxelSize);
				const Point bigVoxelPosWs = m_minPosWs + (Vector(bigX, bigY, bigZ) * kBigVoxelSize);
				const Vector voxelOffset = (Vector(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z) * kSmallVoxelSize);
				const Point posWs = bigVoxelPosWs + voxelOffset;

				Color color = kColorGreen;
				if (pBigVoxel)
				{
					const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
					if (pBigVoxel->IsBitSet(smallVoxelIdx))
						color = kColorRed;
				}
				else
				{
					color = kColorGray;
				}

				g_prim.Draw(DebugBox(posWs, posWs + smallVoxelSize, color, PrimAttrib(kPrimEnableWireframe)));
				g_prim.Draw(DebugString(posWs + smallVoxelSize/2.f, StringBuilder<32>("%d", i).c_str(), color, 0.4f));
			}
#endif

			if (pBigVoxel)
			{
				const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
				if (pBigVoxel->IsBitSet(smallVoxelIdx))
					return true;
			}

			if (err1 > 0)
			{
				currVoxelVs.y += yInc;
				err1 -= dx2;

				if (currVoxelVs.y >= kVoxelsPerAxis)
				{
					currVoxelVs.y -= kVoxelsPerAxis;
					bigVoxelIdx += xCount;
				}
				else if (currVoxelVs.y < 0)
				{
					currVoxelVs.y += kVoxelsPerAxis;
					bigVoxelIdx -= xCount;
				}
			}
			if (err2 > 0)
			{
				currVoxelVs.z += zInc;
				err2 -= dx2;

				if (currVoxelVs.z >= kVoxelsPerAxis)
				{
					currVoxelVs.z -= kVoxelsPerAxis;
					bigVoxelIdx += xCount * yCount;
				}
				else if (currVoxelVs.z < 0)
				{
					currVoxelVs.z += kVoxelsPerAxis;
					bigVoxelIdx -= xCount * yCount;
				}
			}
			err1 += dy2;
			err2 += dz2;
			currVoxelVs.x += xInc;

			if (currVoxelVs.x >= kVoxelsPerAxis)
			{
				currVoxelVs.x -= kVoxelsPerAxis;
				bigVoxelIdx += 1;
			}
			else if (currVoxelVs.x < 0)
			{
				currVoxelVs.x += kVoxelsPerAxis;
				bigVoxelIdx -= 1;
			}
		}
	}
	else if ((m >= l) && (m >= n))
	{
		I32 err1 = dx2 - m;
		I32 err2 = dz2 - m;
		for (U32 i = 0; i < m; i++)
		{
			if (bigVoxelIdx >= m_numBigVoxels || bigVoxelIdx < 0)
				return false;

			const BigVoxel* pBigVoxel = m_ppBigVoxels[bigVoxelIdx];

#if 0
			{
				U32 tempBigVoxelIdx = bigVoxelIdx;
				const U32 bigZ = tempBigVoxelIdx / (xCount * yCount);
				tempBigVoxelIdx -= bigZ * (xCount * yCount);

				const U32 bigY = tempBigVoxelIdx / xCount;
				tempBigVoxelIdx -= bigY * xCount;

				const U32 bigX = tempBigVoxelIdx;

				const Vector smallVoxelSize = Vector(kSmallVoxelSize, kSmallVoxelSize, kSmallVoxelSize);
				const Point bigVoxelPosWs = m_minPosWs + (Vector(bigX, bigY, bigZ) * kBigVoxelSize);
				const Vector voxelOffset = (Vector(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z) * kSmallVoxelSize);
				const Point posWs = bigVoxelPosWs + voxelOffset;

				Color color = kColorGreen;
				if (pBigVoxel)
				{
					const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
					if (pBigVoxel->IsBitSet(smallVoxelIdx))
						color = kColorRed;
				}
				else
				{
					color = kColorGray;
				}

				g_prim.Draw(DebugBox(posWs, posWs + smallVoxelSize, color, PrimAttrib(kPrimEnableWireframe)));
				g_prim.Draw(DebugString(posWs + smallVoxelSize/2.f, StringBuilder<32>("%d", i).c_str(), color, 0.4f));
			}
#endif

			if (pBigVoxel)
			{
				const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
				if (pBigVoxel->IsBitSet(smallVoxelIdx))
					return true;
			}

			if (err1 > 0)
			{
				currVoxelVs.x += xInc;
				err1 -= dy2;

				if (currVoxelVs.x >= kVoxelsPerAxis)
				{
					currVoxelVs.x -= kVoxelsPerAxis;
					bigVoxelIdx += 1;
				}
				else if (currVoxelVs.x < 0)
				{
					currVoxelVs.x += kVoxelsPerAxis;
					bigVoxelIdx -= 1;
				}
			}
			if (err2 > 0)
			{
				currVoxelVs.z += zInc;
				err2 -= dy2;

				if (currVoxelVs.z >= kVoxelsPerAxis)
				{
					currVoxelVs.z -= kVoxelsPerAxis;
					bigVoxelIdx += xCount * yCount;
				}
				else if (currVoxelVs.z < 0)
				{
					currVoxelVs.z += kVoxelsPerAxis;
					bigVoxelIdx -= xCount * yCount;
				}
			}
			err1 += dx2;
			err2 += dz2;
			currVoxelVs.y += yInc;

			if (currVoxelVs.y >= kVoxelsPerAxis)
			{
				currVoxelVs.y -= kVoxelsPerAxis;
				bigVoxelIdx += xCount;
			}
			else if (currVoxelVs.y < 0)
			{
				currVoxelVs.y += kVoxelsPerAxis;
				bigVoxelIdx -= xCount;
			}
		}
	}
	else
	{
		I32 err1 = dy2 - n;
		I32 err2 = dx2 - n;
		for (U32 i = 0; i < n; i++)
		{
			if (bigVoxelIdx >= m_numBigVoxels || bigVoxelIdx < 0)
				return false;

			const BigVoxel* pBigVoxel = m_ppBigVoxels[bigVoxelIdx];

#if 0
			{
				U32 tempBigVoxelIdx = bigVoxelIdx;
				const U32 bigZ = tempBigVoxelIdx / (xCount * yCount);
				tempBigVoxelIdx -= bigZ * (xCount * yCount);

				const U32 bigY = tempBigVoxelIdx / xCount;
				tempBigVoxelIdx -= bigY * xCount;

				const U32 bigX = tempBigVoxelIdx;

				const Vector smallVoxelSize = Vector(kSmallVoxelSize, kSmallVoxelSize, kSmallVoxelSize);
				const Point bigVoxelPosWs = m_minPosWs + (Vector(bigX, bigY, bigZ) * kBigVoxelSize);
				const Vector voxelOffset = (Vector(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z) * kSmallVoxelSize);
				const Point posWs = bigVoxelPosWs + voxelOffset;

				Color color = kColorGreen;
				if (pBigVoxel)
				{
					const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
					if (pBigVoxel->IsBitSet(smallVoxelIdx))
						color = kColorRed;
				}
				else
				{
					color = kColorGray;
				}

				g_prim.Draw(DebugBox(posWs, posWs + smallVoxelSize, color, PrimAttrib(kPrimEnableWireframe)));
				g_prim.Draw(DebugString(posWs + smallVoxelSize/2.f, StringBuilder<32>("%d", i).c_str(), color, 0.4f));
			}
#endif

			if (pBigVoxel)
			{
				const U32 smallVoxelIdx = GetSmallVoxelIdx(currVoxelVs.x, currVoxelVs.y, currVoxelVs.z);
				if (pBigVoxel->IsBitSet(smallVoxelIdx))
					return true;
			}

			if (err1 > 0)
			{
				currVoxelVs.y += yInc;
				err1 -= dz2;

				if (currVoxelVs.y >= kVoxelsPerAxis)
				{
					currVoxelVs.y -= kVoxelsPerAxis;
					bigVoxelIdx += xCount;
				}
				else if (currVoxelVs.y < 0)
				{
					currVoxelVs.y += kVoxelsPerAxis;
					bigVoxelIdx -= xCount;
				}
			}
			if (err2 > 0)
			{
				currVoxelVs.x += xInc;
				err2 -= dz2;

				if (currVoxelVs.x >= kVoxelsPerAxis)
				{
					currVoxelVs.x -= kVoxelsPerAxis;
					bigVoxelIdx += 1;
				}
				else if (currVoxelVs.x < 0)
				{
					currVoxelVs.x += kVoxelsPerAxis;
					bigVoxelIdx -= 1;
				}
			}
			err1 += dy2;
			err2 += dx2;
			currVoxelVs.z += zInc;

			if (currVoxelVs.z >= kVoxelsPerAxis)
			{
				currVoxelVs.z -= kVoxelsPerAxis;
				bigVoxelIdx += xCount * yCount;
			}
			else if (currVoxelVs.z < 0)
			{
				currVoxelVs.z += kVoxelsPerAxis;
				bigVoxelIdx -= xCount * yCount;
			}
		}
    }

#if 0
	if (TestVoxel(currVoxelVs))
	{
		if (pOutVoxel)
			*pOutVoxel = currVoxelVs;
		return true;
	}
#endif

#if 0
	if (pHitPos)
	{
		const Point hitLs = hitVoxel.ToPoint();
		*pHitPos = LocalToWorld(hitLs);
	}
#endif

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Voxelization::GetIndices(const Voxel& voxel, I32* pBigVoxelIdx, I32* pSmallVoxelIdx) const
{
	const U32 smallX = voxel.x % kVoxelsPerAxis;
	const U32 smallY = voxel.y % kVoxelsPerAxis;
	const U32 smallZ = voxel.z % kVoxelsPerAxis;

	const U32 largeX = voxel.x / kVoxelsPerAxis;
	const U32 largeY = voxel.y / kVoxelsPerAxis;
	const U32 largeZ = voxel.z / kVoxelsPerAxis;

	const U32 smallVoxelIdx = GetSmallVoxelIdx(smallX, smallY, smallZ);
	const I32 bigVoxelIdx = GetBigVoxelIdx(largeX, largeY, largeZ);

	if (bigVoxelIdx >= m_numBigVoxels)
		return false;;

	ALWAYS_ASSERT(smallVoxelIdx < kVoxelsPerBigVoxel);

	*pBigVoxelIdx = bigVoxelIdx;
	*pSmallVoxelIdx = smallVoxelIdx;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Voxelization::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const Vector smallVoxelSize = Vector(kSmallVoxelSize, kSmallVoxelSize, kSmallVoxelSize);
	const Vector bigVoxelSize = Vector(kBigVoxelSize, kBigVoxelSize, kBigVoxelSize);

	const RenderCamera& renderCam = GetRenderCamera(0);
	const Point camCentreWs = renderCam.m_position;

	const U32 xCount = m_count[0];
	const U32 yCount = m_count[1];
	const U32 zCount = m_count[2];

	const U32 numBigVoxels = xCount*yCount*zCount;

	U32 numNonEmptyVoxels = 0;

	for (U32 iVoxel = 0; iVoxel < numBigVoxels; iVoxel++)
	{
		if (m_ppBigVoxels[iVoxel])
			numNonEmptyVoxels++;
	}

	MsgCon("%d voxels, %d non empty\n", numBigVoxels, numNonEmptyVoxels);

	for (U32 x = 0; x<xCount; x++)
	{
		for (U32 y = 0; y<yCount; y++)
		{
			for (U32 z = 0; z<zCount; z++)
			{
				const U32 bigVoxelIdx = GetBigVoxelIdx(x, y, z);

				if (bigVoxelIdx >= numBigVoxels)
					continue;

				const BigVoxel* pBigVoxel = m_ppBigVoxels[bigVoxelIdx];
				const Point bigVoxelPosWs = m_minPosWs + (Vector(x, y, z) * kBigVoxelSize);

				if (Dist(bigVoxelPosWs, camCentreWs) > 8.f)
					continue;

				if (!renderCam.IsSphereInFrustum(Sphere(bigVoxelPosWs, kBigVoxelSize)))
					continue;

				if (!pBigVoxel)
				{
					//g_prim.Draw(DebugBox(bigVoxelPosWs, bigVoxelPosWs + bigVoxelSize, kColorPurple, PrimAttrib(kPrimEnableWireframe)));
				}
				else
				{
					for (U32 i = 0; i < kVoxelsPerAxis; i++)
					{
						for (U32 j = 0; j < kVoxelsPerAxis; j++)
						{
							for (U32 k = 0; k < kVoxelsPerAxis; k++)
							{
								const U32 voxelIdx = GetSmallVoxelIdx(i, j, k);
								const Vector voxelOffset = (Vector(i, j, k) * kSmallVoxelSize);
								const Point posWs =  bigVoxelPosWs + voxelOffset;

								if (Dist(posWs, camCentreWs) > 8.f)
									continue;

								if (!renderCam.IsPointInFrustum(posWs))
									continue;

								const bool solid = pBigVoxel->IsBitSet(voxelIdx);
								if (solid)
								{
									g_prim.Draw(DebugBox(posWs, posWs + smallVoxelSize, kColorOrange, PrimAttrib(kPrimEnableWireframe)));
								}
							}
						}
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NpcVoxelMgr::NpcVoxelMgr() 
{
	m_usedVoxelizations.ClearAllBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NpcVoxelMgr::Raycast(Point_arg fromWs, Point_arg toWs, Point* pHitPos /* = nullptr */, bool debug /* = false */) const
{
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);
	VoxelizationBits::Iterator iter(m_usedVoxelizations);

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		const Voxelization* pVox = m_voxelizations[i];
		AI_ASSERT(pVox);

		Point hitPos;
		const bool hit = pVox->Raycast(fromWs, toWs, &hitPos, debug);

		if (hit)
		{
			if (!pHitPos)
				return true;

			// TODO determine the closest hit pos
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NpcVoxelMgr::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_ndAiOptions.m_drawNpcVoxels)
		return;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);
	VoxelizationBits::Iterator iter(m_usedVoxelizations);

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		const Voxelization* pVox = m_voxelizations[i];
		AI_ASSERT(pVox);
		pVox->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NpcVoxelMgr::AddVoxelization(Voxelization* pVoxelization, const Level* pLevel)
{
	AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);

	const U32 managerId = m_usedVoxelizations.FindFirstClearBit();
	AI_ASSERTF(managerId != ~0U, ("Too many voxelizations are logged in!"));

	pVoxelization->SetManagerId(managerId);
	pVoxelization->SetLevelId(pLevel->GetNameId());
	m_voxelizations[managerId] = pVoxelization;
	m_usedVoxelizations.SetBit(managerId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NpcVoxelMgr::RemoveVoxelization(Voxelization* pVoxelization)
{
	AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);

	const U32 managerId = pVoxelization->GetManagerId();
	AI_ASSERT(managerId != ~0U);

	pVoxelization->SetManagerId(~0U);
	m_voxelizations[managerId] = nullptr;
	m_usedVoxelizations.ClearBit(managerId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
VoxelRayCastJob::VoxelRayCastJob()
{
	m_pCounter = nullptr;

	m_bOpen = false;
	m_bDone = false;
	m_bKicked = false;
}

void VoxelRayCastJob::SetProbeExtents(U32 iProbe, Point_arg start, Point_arg end)
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(!m_bKicked);
	ALWAYS_ASSERT(!m_bDone);

	ProbeData& data = m_pProbes[iProbe];

	data.Clear();

	data.m_inStartWs = start;
	data.m_inEndWs = end;

	data.m_inValid = true;
}

Point VoxelRayCastJob::GetProbeStart(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);

	return m_pProbes[iProbe].m_inStartWs;
}

Point VoxelRayCastJob::GetProbeEnd(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);

	return m_pProbes[iProbe].m_inEndWs;
}

Vector VoxelRayCastJob::GetProbeUnitDir(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);

	const Vector vecWs = GetProbeDir(iProbe);
	return Normalize(vecWs);
}

Vector VoxelRayCastJob::GetProbeDir(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);

	return GetProbeEnd(iProbe) - GetProbeStart(iProbe);
}

bool VoxelRayCastJob::IsContactValid(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(m_bKicked);
	ALWAYS_ASSERT(m_bDone);

	return m_pProbes[iProbe].m_outHit;
}

F32 VoxelRayCastJob::GetContactT(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(m_bKicked);
	ALWAYS_ASSERT(m_bDone);

	const ProbeData& probeData = m_pProbes[iProbe];
	if (!probeData.m_outHit)
		return 1.f;

	return GetContactLength(iProbe) / Length(GetProbeDir(iProbe));
}

F32 VoxelRayCastJob::GetContactLength(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(m_bKicked);

	const ProbeData& probeData = m_pProbes[iProbe];

	return Dist(GetContactPoint(iProbe), GetProbeStart(iProbe));
}

Point VoxelRayCastJob::GetContactPoint(U32 iProbe) const
{
	ALWAYS_ASSERT(iProbe < m_numProbes);
	ALWAYS_ASSERT(m_pProbes[iProbe].m_inValid);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(m_bKicked);
	ALWAYS_ASSERT(m_bDone);

	return m_pProbes[iProbe].m_outHitPosWs;
}

void VoxelRayCastJob::Open(U32 numProbes, U16 nFlags)
{
	ALWAYS_ASSERT(!m_pCounter);
	ALWAYS_ASSERT(!m_bOpen);
	ALWAYS_ASSERT(!m_bKicked);
	ALWAYS_ASSERT(!m_bDone);

	m_numProbes = numProbes;
	m_pProbes = NDI_NEW (kAllocSingleGameFrame) ProbeData[numProbes];

	m_bOpen = true;
	m_bDone = false;
	m_bKicked = false;

	m_openFlags = nFlags;
}

void VoxelRayCastJob::Kick(const char *sourceFile, U32F sourceLine, const char *srcFunc, U32 actualNumProbes)
{
	ALWAYS_ASSERT(!m_pCounter);
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(!m_bKicked);
	ALWAYS_ASSERT(!m_bDone);

	if (actualNumProbes > 0)
		m_numProbes = actualNumProbes;

	m_bKicked = true;

	if (m_numProbes == 0)
	{
		m_bDone = true;
		return;
	}

	m_file = sourceFile;
	m_line = sourceLine;
	m_func = srcFunc;

	if ((m_openFlags & kVoxelCastSynchronous) == kVoxelCastSynchronous)
	{
		DoRayCasts();
	}
	else
	{
		ndjob::JobDecl jobDecl(DoRayCastJob, (uintptr_t)this);
		ndjob::RunJobs(&jobDecl, 1, &m_pCounter, FILE_LINE_FUNC);
	}
}

void VoxelRayCastJob::Wait()
{
	ALWAYS_ASSERT(m_bOpen);
	ALWAYS_ASSERT(m_bKicked);
	ALWAYS_ASSERT(!m_bDone);

	if (m_pCounter)
	{

		ndjob::WaitForCounterAndFree(m_pCounter);
		m_pCounter = nullptr;
	}

	m_bDone = true;
}

void VoxelRayCastJob::Close()
{
	m_bOpen = false;
	m_bKicked = false;
	m_bDone = false;
}

void VoxelRayCastJob::DoRayCasts()
{
	for (U32 iProbe = 0; iProbe < m_numProbes; iProbe++)
	{
		ProbeData& probeData = m_pProbes[iProbe];
		Point hitPosWs;
		const bool blocked = g_npcVoxelMgr.Raycast(probeData.m_inStartWs, probeData.m_inEndWs, &hitPosWs, false);

		probeData.m_outHit = blocked;
		probeData.m_outHitPosWs = hitPosWs;
	}
}

CLASS_JOB_ENTRY_POINT_IMPLEMENTATION(VoxelRayCastJob, DoRayCastJob)
{
	VoxelRayCastJob* pJob = reinterpret_cast<VoxelRayCastJob*>(jobParam);
	pJob->DoRayCasts();
}

