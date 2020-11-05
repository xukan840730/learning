/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-game-object-collector.h"

#include "corelib/util/timer.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool g_ndgoFindGameObjectsInSphereDraw = false;
bool g_ndgoFindGameObjectsInCylinderDraw = false;

/// --------------------------------------------------------------------------------------------------------------- ///

#if !SUBMISSION_BUILD
#define GOC_PROFILING 1
#endif

struct GocProfileNugget
{
	U64 m_startCyc = 0;
	F32 m_elapsed_ms = 0.0f;
	I32 m_count = 0;

	void Begin()
	{
		m_startCyc = TimerGetRawCount();
		++m_count;
	}

	void End()
	{
		U64 endCyc = TimerGetRawCount();
		U64 elapsedCyc = endCyc - m_startCyc;
		m_elapsed_ms += ConvertGlobalClockTicksToMilliseconds(elapsedCyc);
	}

	void Dump(const char* name)
	{
		const float elapsedPerCall_ms = (m_count != 0) ? (m_elapsed_ms / (float)m_count) : 0.0f;
		MsgCon("%30s: %4d total calls/frame, %7.3f ms/frame, %7.3f ms/call\n", name, m_count, m_elapsed_ms, elapsedPerCall_ms);
	}

	void Clear()
	{
		m_count = 0;
		m_elapsed_ms = 0.0f;
	}
};

struct GocProfileJanitor
{
#if GOC_PROFILING
	GocProfileNugget& m_nugget;

	GocProfileJanitor(GocProfileNugget& nugget) : m_nugget(nugget)
	{
		nugget.Begin();
	}

	~GocProfileJanitor()
	{
		m_nugget.End();
	}
#else
	GocProfileJanitor(GocProfileNugget&) { }
	~GocProfileJanitor() { }
#endif
};

bool g_gocShowProfile = false;
GocProfileNugget g_gocFindNdGameObjects;
GocProfileNugget g_gocFindNdGameObjectsInSphere;
GocProfileNugget g_gocFindNdGameObjectsInCylinder;
GocProfileNugget g_gocFindInteractablesInSphere;

void NdGameObjectCollector_FrameEnd()
{
#if GOC_PROFILING
	if (g_gocShowProfile)
	{
		g_gocFindNdGameObjects.Dump("FindNdGameObjects");
		g_gocFindNdGameObjectsInSphere.Dump("FindNdGameObjectsInSphere");
		g_gocFindNdGameObjectsInCylinder.Dump("FindNdGameObjectsInCylinder");
		g_gocFindInteractablesInSphere.Dump("FindInteractablesInSphere");
	}
	g_gocFindNdGameObjects.Clear();
	g_gocFindNdGameObjectsInSphere.Clear();
	g_gocFindNdGameObjectsInCylinder.Clear();
	g_gocFindInteractablesInSphere.Clear();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjects(NdGameObject** pOutputList, I32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjects);

	PROFILE(Processes, FindNdGameObjects);
	ProcessHeapRecord* pRootNode = EngineComponents::GetProcessMgr()->m_pRootTree->GetProcessHeapRecord();
	ProcessHeapRecord* pCurNode = pRootNode;
	ProcessHeapRecord* pSkipNode = EngineComponents::GetProcessMgr()->GetSkipGui2Handle().GetHeapRecord();
	I32F i = 0;
	while (i < maxOutputCount)
	{
		pCurNode = Process::DepthFirstNextRecord(pCurNode, pRootNode, pSkipNode);
		if (!pCurNode)
			break;
		if (NdGameObject* pProc = NdGameObject::FromProcess((Process*)pCurNode->m_pMem))
		{
			if (!pProc->IsProcessDead() && !pProc->HasProcessErrorOccurred())
			{
				pOutputList[i++] = pProc;
			}
		}
	}
	return i;
}

I32F FindNdGameObjectsOfType(NdGameObject** pOutputList, StringId64 typeId, StringId64 excludeTypeId, I32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjects);

	PROFILE(Processes, FindNdGameObjectsOfType);
	ProcessHeapRecord* pRootNode = EngineComponents::GetProcessMgr()->m_pRootTree->GetProcessHeapRecord();
	ProcessHeapRecord* pCurNode = pRootNode;
	ProcessHeapRecord* pSkipNode = EngineComponents::GetProcessMgr()->GetSkipGui2Handle().GetHeapRecord();
	I32F i = 0;
	while (i < maxOutputCount)
	{
		pCurNode = Process::DepthFirstNextRecord(pCurNode, pRootNode, pSkipNode);
		if (!pCurNode)
			break;
		if (NdGameObject* pProc = NdGameObject::FromProcess((Process*)pCurNode->m_pMem))
		{
			if (!pProc->IsProcessDead() && !pProc->HasProcessErrorOccurred() && pProc->IsKindOf(typeId) && (excludeTypeId == INVALID_STRING_ID_64 || !pProc->IsType(excludeTypeId)))
			{
				pOutputList[i++] = pProc;
			}
		}
	}
	return i;
}

I32F FindNdGameObjectsOfTypeBucket(NdGameObject** pOutputList, StringId64 typeId, StringId64 excludeTypeId, I32F maxOutputCount, I32 bucket)
{
	GocProfileJanitor gj(g_gocFindNdGameObjects);

	PROFILE(Processes, FindNdGameObjectsOfTypeBucket);
	ALWAYS_ASSERTF(bucket >= 0 && bucket < kProcessBucketCount, ("FindNdGameObjectsOfTypeBucket: INVALID BUCKET: %d", bucket));
	ProcessHeapRecord* pRootNode = EngineComponents::GetProcessMgr()->m_pBucketSubtree[bucket]->GetProcessHeapRecord();
	ProcessHeapRecord* pCurNode = pRootNode;
	ProcessHeapRecord* pSkipNode = EngineComponents::GetProcessMgr()->GetSkipGui2Handle().GetHeapRecord();
	I32F i = 0;
	while (i < maxOutputCount)
	{
		pCurNode = Process::DepthFirstNextRecord(pCurNode, pRootNode, pSkipNode);
		if (!pCurNode)
			break;
		if (NdGameObject* pProc = NdGameObject::FromProcess((Process*)pCurNode->m_pMem))
		{
			if (!pProc->IsProcessDead() && !pProc->HasProcessErrorOccurred() && pProc->IsKindOf(typeId) && (excludeTypeId == INVALID_STRING_ID_64 || !pProc->IsType(excludeTypeId)))
			{
				pOutputList[i++] = pProc;
			}
		}
	}
	return i;
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
	static void DebugDrawCylinderIntersectsSphere(const Cylinder& cylinder, const Sphere& sphere, const bool isIntersecting)
	{
		const Point center = sphere.GetCenter();
		const Point closestPt = cylinder.ClosestPointInCylinder(center);

		const Color color = (isIntersecting ? kColorYellow : kColorGray);
		const Color colorTrans = (isIntersecting ? kColorYellowTrans : kColorGrayTrans);
		g_prim.Draw(DebugSphere(sphere, colorTrans, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimDisableWireframe)));
		g_prim.Draw(DebugSphere(sphere, color, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimEnableWireframe)));
		g_prim.Draw(DebugLine(closestPt, center, color, 4.0f, PrimAttrib(0)));
	}
#endif // #if !FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
static void FindNdGameObjectsInShapeFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	FindNdGameObjectsInShapeData* pSearchData = reinterpret_cast<FindNdGameObjectsInShapeData*>(data);

	if (pSearchData->currOutputCount >= pSearchData->maxOutputCount
		|| (pSearchData->excludeTypeId != INVALID_STRING_ID_64 && pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId))
		|| !pAnimData->m_hProcess.IsKindOf(g_type_NdGameObject))
	{
		//MsgCon("FindNdGameObjectsInShapeFunctor: hit max objects, possibly the player can't pick up weapons.\n");
		return;
	}

	bool shapesIntersect;
	if (pSearchData->useCylinder)
	{
		const Sphere jointSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere);

		// early out on sphere-sphere intersection if possible
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, jointSphere) && pSearchData->searchCylinder.IntersectsSphere(jointSphere);

#if !FINAL_BUILD
		if (g_ndgoFindGameObjectsInCylinderDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
			DebugDrawCylinderIntersectsSphere(pSearchData->searchCylinder, jointSphere, shapesIntersect);
#endif
	}
	else
	{
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, pAnimData->m_pBoundingInfo->m_jointBoundingSphere);
	}

	if (shapesIntersect)
	{
		// Check our list of types to see if there is a match
		if (pAnimData->m_hProcess.HandleValid() && ((pSearchData->excludeTypeId == INVALID_STRING_ID_64) || !pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId)))
		{
			bool add = false;
			if (!pSearchData->typeIds)
			{
				add = true;
			}

			for (U32F i = 0; !add && i < pSearchData->numTypeIds; ++i)
			{
				if ((pSearchData->typeIds[i] == INVALID_STRING_ID_64) || pAnimData->m_hProcess.IsKindOf(pSearchData->typeIds[i]))
				{
					add = true;
					break;
				}
			}

			if (add)
			{
				pSearchData->pOutputList[pSearchData->currOutputCount++] = PunPtr<NdGameObject*>(pAnimData->m_hProcess.ToMutableProcess());
			}
		}
	}

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
	{
		g_prim.Draw(DebugSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere, (pAnimData->m_hProcess.HandleValid() ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FindInteractablesInShapeFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	FindNdGameObjectsInShapeData* pSearchData = reinterpret_cast<FindNdGameObjectsInShapeData*>(data);

	if (pSearchData->currOutputCount >= pSearchData->maxOutputCount
		|| (pSearchData->excludeTypeId != INVALID_STRING_ID_64 && pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId))
		|| !pAnimData->m_hProcess.IsKindOf(g_type_NdGameObject))
	{
		//MsgCon("FindNdGameObjectsInShapeFunctor: hit max objects, possibly the player can't pick up weapons.\n");
		return;
	}

	bool shapesIntersect;
	if (pSearchData->useCylinder)
	{
		const Sphere jointSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere);

		// early out on sphere-sphere intersection if possible
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, jointSphere) && pSearchData->searchCylinder.IntersectsSphere(jointSphere);

#if !FINAL_BUILD
		if (g_ndgoFindGameObjectsInCylinderDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
			DebugDrawCylinderIntersectsSphere(pSearchData->searchCylinder, jointSphere, shapesIntersect);
#endif
	}
	else
	{
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, pAnimData->m_pBoundingInfo->m_jointBoundingSphere);
	}

	if (shapesIntersect)
	{
		// Check our list of types to see if there is a match
		if (pAnimData->m_hProcess.HandleValid())
		{
			bool add = false;
			// type checked above
			const NdGameObject* pGameObj = static_cast<const NdGameObject*>(pAnimData->m_hProcess.ToProcess());
			if (pGameObj && pGameObj->GetInteractControl() != nullptr)
				add = true;

			if (add)
			{
				pSearchData->pOutputList[pSearchData->currOutputCount++] = PunPtr<NdGameObject*>(pAnimData->m_hProcess.ToMutableProcess());
			}
		}
	}

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
	{
		g_prim.Draw(DebugSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere, (pAnimData->m_hProcess.HandleValid() ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
	}
#endif
}


// This is used for interactables without any anim data (i.e. just buddy boosts at this time)
// I hate this name
static void FindNdGameObjectsInShapeWithSphereArgFunctor(MutableNdGameObjectHandle hProcess, Sphere objectBoundingSphere, uintptr_t data)
{
	FindNdGameObjectsInShapeData* pSearchData = reinterpret_cast<FindNdGameObjectsInShapeData*>(data);

	if (pSearchData->currOutputCount >= pSearchData->maxOutputCount
		|| ((pSearchData->excludeTypeId != INVALID_STRING_ID_64) && hProcess.IsType(pSearchData->excludeTypeId)))
	{
		//MsgCon("FindNdGameObjectsInShapeFunctor: hit max objects, possibly the player can't pick up weapons.\n");
		return;
	}

	bool shapesIntersect;
	if (pSearchData->useCylinder)
	{

		// early out on sphere-sphere intersection if possible
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, objectBoundingSphere) && pSearchData->searchCylinder.IntersectsSphere(objectBoundingSphere);

#if !FINAL_BUILD
		if (g_ndgoFindGameObjectsInCylinderDraw && DebugSelection::Get().IsProcessOrNoneSelected(hProcess))
			DebugDrawCylinderIntersectsSphere(pSearchData->searchCylinder, objectBoundingSphere, shapesIntersect);
#endif
	}
	else
	{
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, objectBoundingSphere);
	}

	if (shapesIntersect)
	{
		// Check our list of types to see if there is a match
		if (hProcess.HandleValid())
		{
			bool add = false;
			if (!pSearchData->typeIds)
			{
				add = true;
			}

			for (U32F i = 0; !add && i < pSearchData->numTypeIds; ++i)
			{
				if ((pSearchData->typeIds[i] == INVALID_STRING_ID_64) || hProcess.IsKindOf(pSearchData->typeIds[i]))
				{
					add = true;
					break;
				}
			}

			if (add)
			{
				pSearchData->pOutputList[pSearchData->currOutputCount++] = hProcess;
			}
		}
	}

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw && DebugSelection::Get().IsProcessOrNoneSelected(hProcess))
	{
		g_prim.Draw(DebugSphere(objectBoundingSphere, (hProcess.HandleValid() ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
	}
#endif
}

struct FindNdGameObjectsInShapeJobData
{
	Sphere searchSphere;
	Cylinder searchCylinder;
	ExternalBitArray* pOutputList;
	NdAtomic64*	pCurrOutputCount;
	I32F maxOutputCount;
	const StringId64* typeIds;
	U32F numTypeIds;
	StringId64 excludeTypeId;
	bool useCylinder;
	I32F blockIndex;
	AnimMgr::FgAnimDataFunctor funcPtr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void FindInteractablesInShapeJobFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	FindNdGameObjectsInShapeJobData* pSearchData = reinterpret_cast<FindNdGameObjectsInShapeJobData*>(data);

	if (pSearchData->pCurrOutputCount->Get() >= pSearchData->maxOutputCount
		|| (pSearchData->excludeTypeId != INVALID_STRING_ID_64 && pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId))
		|| !pAnimData->m_hProcess.IsKindOf(g_type_NdGameObject))
	{
		//MsgCon("FindNdGameObjectsInShapeFunctor: hit max objects, possibly the player can't pick up weapons.\n");
		return;
	}

	bool shapesIntersect;
	if (pSearchData->useCylinder)
	{
		const Sphere jointSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere);

		// early out on sphere-sphere intersection if possible
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, jointSphere) && pSearchData->searchCylinder.IntersectsSphere(jointSphere);

#if !FINAL_BUILD
		if (g_ndgoFindGameObjectsInCylinderDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
			DebugDrawCylinderIntersectsSphere(pSearchData->searchCylinder, jointSphere, shapesIntersect);
#endif
	}
	else
	{
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, pAnimData->m_pBoundingInfo->m_jointBoundingSphere);
	}

	if (shapesIntersect)
	{
		// Check our list of types to see if there is a match
		if (pAnimData->m_hProcess.HandleValid())
		{
			bool add = false;
			// type checked above
			const NdGameObject* pGameObj = static_cast<const NdGameObject*>(pAnimData->m_hProcess.ToProcess());
			if (pGameObj && pGameObj->GetInteractControl() != nullptr)
				add = true;

			if (add)
			{
				I64 oldCount = pSearchData->pCurrOutputCount->Add(1);
				if (oldCount < pSearchData->maxOutputCount)
					pSearchData->pOutputList->SetBit(EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData));
			}
		}
	}

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
	{
		g_prim.Draw(DebugSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere, (pAnimData->m_hProcess.HandleValid() ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FindNdGameObjectsInShapeJobFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	FindNdGameObjectsInShapeJobData* pSearchData = reinterpret_cast<FindNdGameObjectsInShapeJobData*>(data);

	if (pSearchData->pCurrOutputCount->Get() >= pSearchData->maxOutputCount
		|| (pSearchData->excludeTypeId != INVALID_STRING_ID_64 && pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId))
		|| !pAnimData->m_hProcess.IsKindOf(g_type_NdGameObject))
	{
		//MsgCon("FindNdGameObjectsInShapeFunctor: hit max objects, possibly the player can't pick up weapons.\n");
		return;
	}

	bool shapesIntersect;
	if (pSearchData->useCylinder)
	{
		const Sphere jointSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere);

		// early out on sphere-sphere intersection if possible
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, jointSphere) && pSearchData->searchCylinder.IntersectsSphere(jointSphere);

#if !FINAL_BUILD
		if (g_ndgoFindGameObjectsInCylinderDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
			DebugDrawCylinderIntersectsSphere(pSearchData->searchCylinder, jointSphere, shapesIntersect);
#endif
	}
	else
	{
		shapesIntersect = SpheresIntersect(pSearchData->searchSphere, pAnimData->m_pBoundingInfo->m_jointBoundingSphere);
	}

	if (shapesIntersect)
	{
		// Check our list of types to see if there is a match
		if (pAnimData->m_hProcess.HandleValid() && ((pSearchData->excludeTypeId == INVALID_STRING_ID_64) || !pAnimData->m_hProcess.IsType(pSearchData->excludeTypeId)))
		{
			bool add = false;
			if (!pSearchData->typeIds)
			{
				add = true;
			}

			for (U32F i = 0; !add && i < pSearchData->numTypeIds; ++i)
			{
				if ((pSearchData->typeIds[i] == INVALID_STRING_ID_64) || pAnimData->m_hProcess.IsKindOf(pSearchData->typeIds[i]))
				{
					add = true;
					break;
				}
			}

			if (add)
			{
				I64 oldCount = pSearchData->pCurrOutputCount->Add(1);
				if (oldCount < pSearchData->maxOutputCount)
					pSearchData->pOutputList->SetBit(EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData));
			}
		}
	}

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess))
	{
		g_prim.Draw(DebugSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere, (pAnimData->m_hProcess.HandleValid() ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(FindNdGameObjectsInShapeJob)
{
	PROFILE(Processes, FindNdGameObjectsInShapeJob);
	FindNdGameObjectsInShapeJobData *pSearchData = (FindNdGameObjectsInShapeJobData*)jobParam;
	EngineComponents::GetAnimMgr()->ForAllUsedAnimDataInBlock(pSearchData->funcPtr, pSearchData->blockIndex, reinterpret_cast<uintptr_t>(pSearchData));
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjectsInSphereOfTypeJobfied(Sphere_arg searchSphere, StringId64 typeId, StringId64 excludeTypeId, ExternalBitArray& outputList, I32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphereOfTypeJobfied);

	// assumes outputList is already initialized to the correct size
	U32 numBlocks = outputList.GetNumBlocks();

	FindNdGameObjectsInShapeJobData* pFindNdGameObjectsInShapeJobDatas = NDI_NEW(kAllocSingleGameFrame) FindNdGameObjectsInShapeJobData[numBlocks];
	ndjob::JobDecl* pFindNdGameObjectsInShapeJobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[numBlocks];
	NdAtomic64  currOutputCount(0);
	ndjob::CounterHandle jobCounter;

	for (U64 i = 0; i < numBlocks; i++)
	{
		FindNdGameObjectsInShapeJobData& searchData = pFindNdGameObjectsInShapeJobDatas[i];
		searchData.searchSphere = searchSphere;
		searchData.useCylinder = false;
		searchData.pOutputList = &outputList;
		searchData.pCurrOutputCount = &currOutputCount;
		searchData.maxOutputCount = maxOutputCount;
		searchData.typeIds = &typeId;
		searchData.numTypeIds = 1;
		searchData.excludeTypeId = excludeTypeId;
		searchData.blockIndex = i;
		searchData.funcPtr = FindNdGameObjectsInShapeJobFunctor;

		ndjob::JobDecl &jobDecl = pFindNdGameObjectsInShapeJobDecls[i];
		jobDecl = ndjob::JobDecl(FindNdGameObjectsInShapeJob, (uintptr_t)&searchData);
		jobDecl.m_flags = ndjob::kDisallowSleep;
	}

	ndjob::RunJobs(pFindNdGameObjectsInShapeJobDecls, numBlocks, &jobCounter, FILE_LINE_FUNC);
	ndjob::WaitForCounterAndFree(jobCounter);

	return outputList.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjectsInSphereOfType(Sphere_arg searchSphere, StringId64 typeId, StringId64 excludeTypeId, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphere);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = &typeId;
	searchData.numTypeIds = 1;
	searchData.excludeTypeId = excludeTypeId;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimData(FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

I32F FindNdGameObjectsInSphereOfTypeBucket(Sphere_arg searchSphere, StringId64 typeId, StringId64 excludeTypeId, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount, I32 bucket)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphere);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = &typeId;
	searchData.numTypeIds = 1;
	searchData.excludeTypeId = excludeTypeId;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimDataInBucket(bucket, FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindNdGameObjectsInSphereOfTypesJobfied(Sphere_arg searchSphere, const StringId64 typeIds[], U32F numTypes, ExternalBitArray& outputList, U32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphereOfTypesJobfied);

	I32 lastUsedAnimIndex = EngineComponents::GetAnimMgr()->GetLastUsedAnimIndex();
	U32 numBlocks = ExternalBitArray::DetermineNumBlocks(lastUsedAnimIndex + 1);
	outputList.Init(lastUsedAnimIndex + 1, NDI_NEW(kAllocSingleGameFrame) U64[numBlocks]);

	FindNdGameObjectsInShapeJobData* pFindNdGameObjectsInShapeJobDatas = NDI_NEW(kAllocSingleGameFrame) FindNdGameObjectsInShapeJobData[numBlocks];
	ndjob::JobDecl* pFindNdGameObjectsInShapeJobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[numBlocks];
	NdAtomic64  currOutputCount(0);
	ndjob::CounterHandle jobCounter;

	for (U64 i = 0; i < numBlocks; i++)
	{
		FindNdGameObjectsInShapeJobData& searchData = pFindNdGameObjectsInShapeJobDatas[i];
		searchData.searchSphere = searchSphere;
		searchData.useCylinder = false;
		searchData.pOutputList = &outputList;
		searchData.pCurrOutputCount = &currOutputCount;
		searchData.maxOutputCount = maxOutputCount;
		searchData.typeIds = typeIds;
		searchData.numTypeIds = numTypes;
		searchData.excludeTypeId = INVALID_STRING_ID_64;
		searchData.blockIndex = i;
		searchData.funcPtr = FindNdGameObjectsInShapeJobFunctor;

		ndjob::JobDecl &jobDecl = pFindNdGameObjectsInShapeJobDecls[i];
		jobDecl = ndjob::JobDecl(FindNdGameObjectsInShapeJob, (uintptr_t)&searchData);
		jobDecl.m_flags = ndjob::kDisallowSleep;
	}

	ndjob::RunJobs(pFindNdGameObjectsInShapeJobDecls, numBlocks, &jobCounter, FILE_LINE_FUNC);
	ndjob::WaitForCounterAndFree(jobCounter);

	return outputList.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindNdGameObjectsInSphereOfTypes(Sphere_arg searchSphere, const StringId64 typeIds[], U32F numTypes, MutableNdGameObjectHandle* pOutputList, U32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphere);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = typeIds;
	searchData.numTypeIds = numTypes;
	searchData.excludeTypeId = INVALID_STRING_ID_64;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimData(FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindInteractablesInSphereJobfied(Sphere_arg searchSphere, ExternalBitArray& outputList, U32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindInteractablesInSphere);

	PROFILE(Processes, FindInteractablesInSphereJobfied);

	// assumes outputList is already initialized to the correct size
	U32 numBlocks = outputList.GetNumBlocks();

	FindNdGameObjectsInShapeJobData* pFindNdGameObjectsInShapeJobDatas = NDI_NEW(kAllocSingleGameFrame) FindNdGameObjectsInShapeJobData[numBlocks];
	ndjob::JobDecl* pFindNdGameObjectsInShapeJobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[numBlocks];
	NdAtomic64  currOutputCount(0);
	ndjob::CounterHandle jobCounter;

	for (U64 i = 0; i < numBlocks; i++)
	{
		FindNdGameObjectsInShapeJobData& searchData = pFindNdGameObjectsInShapeJobDatas[i];
		searchData.searchSphere = searchSphere;
		searchData.useCylinder = false;
		searchData.pOutputList = &outputList;
		searchData.pCurrOutputCount = &currOutputCount;
		searchData.maxOutputCount = maxOutputCount;
		searchData.excludeTypeId = INVALID_STRING_ID_64;
		searchData.blockIndex = i;
		searchData.funcPtr = FindInteractablesInShapeJobFunctor;

		ndjob::JobDecl &jobDecl = pFindNdGameObjectsInShapeJobDecls[i];
		jobDecl = ndjob::JobDecl(FindNdGameObjectsInShapeJob, (uintptr_t)&searchData);
		jobDecl.m_flags = ndjob::kDisallowSleep;
	}

	ndjob::RunJobs(pFindNdGameObjectsInShapeJobDecls, numBlocks, &jobCounter, FILE_LINE_FUNC, ndjob::Priority::kGameFrameBelowNormal);
	ndjob::WaitForCounterAndFree(jobCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindInteractablesInSphere(Sphere_arg searchSphere, MutableNdGameObjectHandle* pOutputList, U32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindInteractablesInSphere);

	PROFILE(Processes, FindInteractablesInSphere);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.excludeTypeId = INVALID_STRING_ID_64;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimData(FindInteractablesInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindNdGameObjectsInSphereOfTypesExcludeBucket(Sphere_arg searchSphere, const StringId64 typeIds[], U32F numTypes, MutableNdGameObjectHandle* pOutputList, U32F maxOutputCount, I32 bucket)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInSphere);

	PROFILE(Processes, FindNdGameObjectsInSphere);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = typeIds;
	searchData.numTypeIds = numTypes;
	searchData.excludeTypeId = INVALID_STRING_ID_64;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimDataExcludeBucket(bucket, FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindInteractablesInSphereExcludeBucket(Sphere_arg searchSphere, MutableNdGameObjectHandle* pOutputList, U32F maxOutputCount, I32 bucket)
{
	GocProfileJanitor gj(g_gocFindInteractablesInSphere);

	PROFILE(Processes, FindInteractablesInSphereExcludeBucket);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchSphere;
	searchData.useCylinder = false;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.excludeTypeId = INVALID_STRING_ID_64;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInSphereDraw)
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimDataExcludeBucket(bucket, FindInteractablesInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjectsInSphere(Sphere_arg searchSphere, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount)
{
	return FindNdGameObjectsInSphereOfType(searchSphere, INVALID_STRING_ID_64, INVALID_STRING_ID_64, pOutputList, maxOutputCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjectsInCylinderOfType(const Cylinder& searchCylinder, StringId64 typeId, StringId64 excludeTypeId, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInCylinder);

	PROFILE_AUTO(Processes);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchCylinder.GetBoundingSphere();
	searchData.searchCylinder = searchCylinder;
	searchData.useCylinder = true;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = &typeId;
	searchData.numTypeIds = 1;
	searchData.excludeTypeId = excludeTypeId;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInCylinderDraw)
	{
		DebugDrawCylinder(searchCylinder, kColorGreenTrans, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimDisableWireframe));
		DebugDrawCylinder(searchCylinder, kColorGreen, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimEnableWireframe));
	}
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimData(FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindNdGameObjectsInCylinderOfTypes(const Cylinder& searchCylinder, const StringId64 typeIds[], U32F numTypes, MutableNdGameObjectHandle* pOutputList, U32F maxOutputCount)
{
	GocProfileJanitor gj(g_gocFindNdGameObjectsInCylinder);

	PROFILE_AUTO(Processes);

	FindNdGameObjectsInShapeData searchData;
	searchData.searchSphere = searchCylinder.GetBoundingSphere();
	searchData.searchCylinder = searchCylinder;
	searchData.useCylinder = true;
	searchData.pOutputList = pOutputList;
	searchData.currOutputCount = 0;
	searchData.maxOutputCount = maxOutputCount;
	searchData.typeIds = typeIds;
	searchData.numTypeIds = numTypes;
	searchData.excludeTypeId = INVALID_STRING_ID_64;

#if !FINAL_BUILD
	if (g_ndgoFindGameObjectsInCylinderDraw)
	{
		DebugDrawCylinder(searchCylinder, kColorGreenTrans, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimDisableWireframe));
		DebugDrawCylinder(searchCylinder, kColorGreen, PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite, kPrimEnableWireframe));
	}
#endif

	EngineComponents::GetAnimMgr()->ForAllUsedAnimData(FindNdGameObjectsInShapeFunctor, reinterpret_cast<uintptr_t>(&searchData));

	EngineComponents::GetNdGameInfo()->ForEachBuddyBoost(FindNdGameObjectsInShapeWithSphereArgFunctor, reinterpret_cast<uintptr_t>(&searchData));

	return searchData.currOutputCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjectsInCylinder(const Cylinder& searchCylinder, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount)
{
  return FindNdGameObjectsInCylinderOfType(searchCylinder, INVALID_STRING_ID_64, INVALID_STRING_ID_64, pOutputList, maxOutputCount);
}
