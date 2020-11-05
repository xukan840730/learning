/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-bg-anim-object.h"

#include "gamelib/level/art-item-skeleton.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/background.h"
#include "ndlib/render/scene.h"

// -------------------------------------------------------------------------------------------------
// NdBgAnimObject
// -------------------------------------------------------------------------------------------------

struct AttachedBgEntry
{
	U32							m_numMappings;
	const int*					m_pFgMap;
	const Background**			m_ppBackGround;
	const Locator*				m_pJointLocsWs;
	const int*					m_pJointIndex;
	const AttachedFgMapping**	m_ppAttachedFgMapping;
};

static const U32 kMaxAttachedBgEntries = 64;
static AttachedBgEntry s_attachedBgEntryList[kMaxAttachedBgEntries];
static U32 s_numAttachedBgEntries = 0;

PROCESS_REGISTER(NdBgAnimObject, NdSimpleObject);

Err NdBgAnimObject::Init(const ProcessSpawnInfo& spawn)
{
	PROFILE(Processes, NdBgAnimObject_Init);
	Err err = ParentClass::Init(spawn);
	if ( err.Failed() )
	{
		return err;
	}
	
	m_tagId = spawn.GetData<StringId64>(SID("AttachedFGTagName"), INVALID_STRING_ID_64);
	if (m_tagId == INVALID_STRING_ID_64)
	{
		GoError("No property AttachedFGTagName found");
		return Err::kErrGeneral;
	}

	m_numMappings = 0;
	for(int i=0; i<kMaxNumMappings; i++)
	{
		m_pBackground[i] = nullptr;
		m_pAttachedFgMapping[i] = nullptr;
		m_jointIndex[i] = -1;
		m_backgroundNameId[i] = INVALID_STRING_ID_64;
		m_iFgMap[i] = -1;
	}

	ChangeParentProcess( EngineComponents::GetProcessMgr()->m_pPlatformTree );

	return err;
}

Err NdBgAnimObject::SetupAnimControl(AnimControl* pAnimControl)
{
	AnimControl* pMasterAnimControl = GetAnimControl();
	FgAnimData& animData = pMasterAnimControl->GetAnimData();
	const I32 foundAnimations = ResourceTable::GetNumAnimationsForSkelId(animData.m_curSkelHandle.ToArtItem()->m_skelId);
	const U32F numNeededSimpleLayerInstances = 1;

	pAnimControl->AllocateSimpleLayer(numNeededSimpleLayerInstances);
	AnimLayer* pBaseLayer = pAnimControl->CreateSimpleLayer(SID("base"), ndanim::kBlendSlerp, 0);
	pBaseLayer->Fade(1.0, 0.0f);
	return Err::kOK;
}


Err NdBgAnimObject::PostInit(const ProcessSpawnInfo& spawn)
{
	// Play initial animation.
	StringId64 animId = SID("idle");
	AnimControl* pAnimCtrl = GetAnimControl();
	if (pAnimCtrl)
	{
		AnimSimpleLayer* pLayer = pAnimCtrl->GetSimpleLayerById(SID("base"));
		if (pLayer)
		{
			pLayer->RequestFadeToAnim(animId);
		}
	}

	Err result = ParentClass::PostInit(spawn);
	return result;
}

void NdBgAnimObject::PostAnimBlending_Async()
{
	ParentClass::PostAnimBlending_Async();

	{	
		PROFILE(Animation, AttachedFg);

		const FgAnimData* pAnimData = GetAnimData();
		const JointCache* pJointCache = &pAnimData->m_jointCache;

		U32 forceCache = 0;
		for (int i=0; i<m_numMappings; i++)
		{
			forceCache |= m_pBackground[i]->GetNameId().GetValue() ^ m_backgroundNameId[i].GetValue();
		}

		if (forceCache != 0 || m_numMappings == 0)
		{
			PROFILE(Animation, AttachedFg_Cache);

			m_numMappings = 0;
			for (I32F iScene = 0; iScene < g_scene.GetNumBackgrounds(); ++iScene)
			{
				const Background* pBackground = g_scene.GetBackground(iScene);
				if (!pBackground->GetAttachedFg().m_pAttachedFgMapping || !pBackground->GetAttachedFg().m_pAttachedTransforms || 
					!pBackground->GetAttachedFg().m_pAttachedBoundingSpheres)
				{
					continue;
				}

				for (I32F iFgMap = 0; iFgMap < pBackground->GetAttachedFg().m_numAttachedFgMappings; ++iFgMap)
				{
					const AttachedFgMapping& attachedFgMapping = pBackground->GetAttachedFg().m_pAttachedFgMapping[iFgMap];

					if (attachedFgMapping.m_tagId == m_tagId)
					{
						m_iFgMap[m_numMappings] = iFgMap;
						m_pBackground[m_numMappings] = pBackground;
						m_backgroundNameId[m_numMappings] = pBackground->GetNameId();
						m_pAttachedFgMapping[m_numMappings] = &attachedFgMapping;
						m_jointIndex[m_numMappings] = pAnimData->FindJoint(attachedFgMapping.m_jointId);

						if (m_jointIndex[m_numMappings] >= 0)
						{
							m_numMappings++;
							ALWAYS_ASSERT(m_numMappings < kMaxNumMappings);
						}
					}
				}
			}
		}

		// don't just stomp over random memory!
		ASSERT( s_numAttachedBgEntries < kMaxAttachedBgEntries );
		if ( s_numAttachedBgEntries < kMaxAttachedBgEntries)
		{
			PROFILE(Animation, AttachedFg_Apply);

			AttachedBgEntry& entry = s_attachedBgEntryList[s_numAttachedBgEntries++];
			entry.m_numMappings = m_numMappings;
			entry.m_pFgMap = &m_iFgMap[0];
			entry.m_ppBackGround = &m_pBackground[0];
			entry.m_pJointLocsWs = pJointCache->GetJointLocatorsWs();
			entry.m_pJointIndex = &m_jointIndex[0];
			entry.m_ppAttachedFgMapping = &m_pAttachedFgMapping[0];
		}
		else
		{
			GoError( "NdBgAnimObject: exceeded the max attached bg-entries count\n" );
		}
	}
}


STATE_REGISTER(NdBgAnimObject, Active, kPriorityNormal);



/// --------------------------------------------------------------------------------------------------------------- ///
void ClearBatchUpdateListForAllBgAnimObjects()
{
	s_numAttachedBgEntries = 0;
}


void WorkBatchUpdateAllBgAnimObjects()
{
	for (U32F i = 0; i < s_numAttachedBgEntries; ++i)
	{
		const AttachedBgEntry& entry = s_attachedBgEntryList[i];

		const int* pFgMap = entry.m_pFgMap;
		const Background** ppBackGround = entry.m_ppBackGround;
		const Locator* pJointLocsWs = entry.m_pJointLocsWs;
		const int* pJointIndex = entry.m_pJointIndex;
		const AttachedFgMapping** ppAttachedFgMapping = entry.m_ppAttachedFgMapping;
		for (U32F j = 0; j < entry.m_numMappings; j++)
		{
			const int fgMapIndex = *pFgMap++;
			const Background* bg = *ppBackGround++;
			const U32F jointIndex = *pJointIndex++;
			const AttachedFgMapping* mapping = *ppAttachedFgMapping++;

			const Transform& localTransform = bg->GetAttachedFg().m_pAttachedTransforms[fgMapIndex];
			const Vec4 objBoundingSphere = bg->GetAttachedFg().m_pAttachedBoundingSpheres[fgMapIndex];
			const Scalar objRadius = objBoundingSphere.W();

			const Locator&  loc              = pJointLocsWs[jointIndex];
			const Transform newTransform     = localTransform * loc.AsTransform();

			const Point     objSphereCenter = Point(objBoundingSphere); 
			const Point     newSphereCenter  = objSphereCenter * newTransform;

			Vec4 newBoundingSphere = newSphereCenter.GetVec4();
			newBoundingSphere.SetW(objRadius);

#ifdef NDI_PLAT_PS3
			mapping->m_pInstance->m_boundingSphere = newBoundingSphere;
			mapping->m_pInstance->m_transform      = newTransform;
#else
			// WIN - TODO
#endif			
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void KickBatchUpdateAllBgAnimObjects()
{
	PROFILE(Animation, KickBatchUpdateAllBgAnimObjects);

	if (s_numAttachedBgEntries > 0)
	{
// WIN - TODO
#ifdef NDI_PLAT_PS3
		SetWorkerThreadType(kWorkerThreadType_AnimBgUpdate);
		KickWorkerThread();
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WaitForBatchUpdateAllBgAnimObjects()
{
	PROFILE(Animation, WaitForBatchUpdateAllBgAnimObjects);

	if (s_numAttachedBgEntries > 0)
	{
// WIN - TODO
#ifdef NDI_PLAT_PS3
		WaitForWorkerThread();
#endif
	}
}
