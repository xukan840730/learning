/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-streaming.h"

#include "corelib/memory/memory-map.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-stream-loader.h"
#include "ndlib/anim/anim-stream-manager.h"
#include "ndlib/anim/anim-stream.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/frame-params.h"
#include "ndlib/nd-game-info.h"

#include "gamelib/level/art-item-anim.h"

extern StreamLoaderPool* s_pStreamLoaderPool;

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamIsBusy()
{
	if (s_pStreamLoaderPool)
	{
		for (int i = 0; i < kMaxAnimStreamLoaders; ++i)
		{
			if (s_pStreamLoaderPool->m_animStreamLoaders[i].IsReading())
				return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Public Interface
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoginStreamDef(void* pData)
{
	PROFILE(Level, AnimStreamLoginStreamDef);

	if (EngineComponents::GetNdGameInfo()->m_speechWhitelist) // don't actually log in streams when in speechwhitelist mode
		return;

	const AnimStreamDef* pAnimStreamDef = (AnimStreamDef*)pData;
	pAnimStreamDef->m_streamNameId = StringToStringId64(pAnimStreamDef->m_pStreamName);
	EngineComponents::GetAnimStreamManager()->RegisterStreamDef(pAnimStreamDef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLogoutStreamDef(void* pData)
{
	const AnimStreamDef* pAnimStreamDef = (AnimStreamDef*)pData;
	EngineComponents::GetAnimStreamManager()->UnregisterStreamDef(pAnimStreamDef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamNotifyUsage(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float animPhase)
{	
	if (animPhase == 1.0f)
	{
		// this is the last chunk, if we have a last chunk, don't set the phase
		StringId64 animIdChunkName = StringId64Concat(animNameId, "-chunk-last");
		const ArtItemAnim* pArtItemAnimLast = AnimMasterTable::LookupAnim(pArtItemAnim->m_skelID,
																		  pArtItemAnim->m_pClipData->m_animHierarchyId,
																		  animIdChunkName).ToArtItem();
		if (pArtItemAnimLast != nullptr)
			return;
	}

	PROFILE(Animation, AnimStreamNotifyUsage);
	EngineComponents::GetAnimStreamManager()->NotifyAnimUsage(pArtItemAnim, animNameId, animPhase, GetCurrentFrameNumber());
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStreamLookupRealArtItem(const ArtItemAnim* pChunkArtItem)
{
	AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStreamFromChunk(pChunkArtItem);
	if (pAnimStream)
		return pAnimStream->GetArtItemAnimForChunk(pChunkArtItem);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamUpdateAll()
{
	PROFILE(Animation, AnimStreamUpdateAll);

#if ENABLE_ANIM_STREAMING_PRELOADING
	// Free up prefetch ops
	for (I64 prefetchOpIndex = 0; prefetchOpIndex < kMaxPrefetchOps; ++prefetchOpIndex)
	{
		if (g_prefetchOps[prefetchOpIndex] && EngineComponents::GetFileSystem()->IsDone(g_prefetchOps[prefetchOpIndex]))
		{
//			MsgAnimStreamDebug("Preload Read : Completed opIndex %lld \n", prefetchOpIndex);
			EngineComponents::GetFileSystem()->ReleaseOpEx(g_prefetchOps[prefetchOpIndex], FILE_LINE_FUNC);
			g_prefetchOps[prefetchOpIndex] = 0;
		}
	}
#endif

	if (s_pStreamLoaderPool)
		s_pStreamLoaderPool->Update();
	EngineComponents::GetAnimStreamManager()->UpdateAll();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamResetAll()
{
	EngineComponents::GetAnimStreamManager()->ResetAll();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const ArtItemAnim* AnimStreamGetArtItem_Internal(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float phase)
{
	PROFILE(Animation, AnimStreamGetArtItem_Internal);

	if (phase == 1.0f)
	{
		// this is the last chunk, return the embeded last chunk if it is available
		StringId64 animIdChunkName = StringId64Concat(animNameId, "-chunk-last");
		const ArtItemAnim* pArtItemAnimLast = AnimMasterTable::LookupAnim(pArtItemAnim->m_skelID, pArtItemAnim->m_pClipData->m_animHierarchyId, animIdChunkName).ToArtItem();
		if (pArtItemAnimLast != nullptr)
		{
			ANIM_ASSERT((uintptr_t)pArtItemAnimLast != 0x5555555555555555ULL && (uintptr_t)pArtItemAnimLast != 0x3333333333333333ULL);
			return pArtItemAnimLast;
		}
	}

	AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStream(pArtItemAnim, animNameId);
	if (pAnimStream)
		return pAnimStream->GetArtItemAnim(pArtItemAnim->m_skelID, animNameId, phase);

	const ArtItemAnim* pFirstChunk = AnimStreamGetArtItemFirstChunk(pArtItemAnim, animNameId);
	ANIM_ASSERT(pFirstChunk);

	ANIM_ASSERT((uintptr_t)pFirstChunk != 0x5555555555555555ULL && (uintptr_t)pFirstChunk != 0x3333333333333333ULL);
	return pFirstChunk;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStreamGetArtItem(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float phase)
{
	//Allowed to attach to a stream
	return AnimStreamGetArtItem_Internal(pArtItemAnim, animNameId, phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStreamGetStreamPhase(const ArtItemAnim* pArtItem, StringId64 animNameId)
{
	return EngineComponents::GetAnimStreamManager()->GetAnimStreamPhase(pArtItem, animNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStreamGetArtItemFirstChunk(const ArtItemAnim* pArtItemAnim, StringId64 animNameId)
{
	PROFILE(Animation, AnimStreamGetArtItemFirstChunk);

	//return the embedded first chunk if it is available
	StringId64 animIdChunkName = StringId64Concat(animNameId, "-chunk-0");
	const ArtItemAnim* pArtItemAnimLast = AnimMasterTable::LookupAnim(pArtItemAnim->m_skelID, pArtItemAnim->m_pClipData->m_animHierarchyId, animIdChunkName).ToArtItem();
	
	return pArtItemAnimLast;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamReset(const ArtItemAnim* pArtItemAnim, StringId64 animNameId)
{
	PROFILE(Animation, AnimStreamGetArtItem);

	AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStream(pArtItemAnim, animNameId);
	if (pAnimStream)
		pAnimStream->Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStreamGetChunkPhase(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float animPhase)
{
	PROFILE(Animation, AnimStreamGetChunkPhase);

	// This is to deal with animations using the last chunk, if we're at the end, we don't want
	// to look at the stream
	if (animPhase == 1.0f)
		return animPhase;

	AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStream(pArtItemAnim, animNameId);
	if (pAnimStream)
		return pAnimStream->ChunkPhase(animPhase);

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStreamHasLoaded(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float animPhase)
{
	PROFILE(Animation, AnimStreamHasLoaded);

	AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStream(pArtItemAnim, animNameId);
	if (pAnimStream)
	{
		return pAnimStream->ValidatePhase(&animPhase, 1);
	}

	return false;
}
