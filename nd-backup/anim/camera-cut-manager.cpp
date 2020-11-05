/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/camera-cut-manager.h"

#include "corelib/util/bigsort.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/engine-components.h"
#include "ndlib/nd-game-info.h"
#include "gamelib/level/art-item-anim.h"



/// --------------------------------------------------------------------------------------------------------------- ///
void CameraCutManager::Init()
{
}


/// --------------------------------------------------------------------------------------------------------------- ///
void CameraCutManager::RegisterAnim(const ArtItemAnim* pAnim)
{
	if (EngineComponents::GetNdGameInfo()->m_speechWhitelist) // don't actually log in streams when in speechwhitelist mode
		return;

	if (pAnim->m_flags & ArtItemAnim::kCameraCutAnim)
	{
		GAMEPLAY_ASSERTF(m_cameraCutNumEntries < kNumCameraCutAnimEntries, ("CameraCutManager - Camera cut anim limit reached (%d).", kNumCameraCutAnimEntries));
		if (m_cameraCutNumEntries == kNumCameraCutAnimEntries)
			return;

		// Find a free slot
		CameraCutAnimEntry* pEntry = &m_cameraCutAnimEntries[m_cameraCutNumEntries];

		pEntry->m_pArtItemAnim = pAnim;

		ANIM_ASSERT(strlen(pAnim->GetName()) < 255);
		char origAnimName[256];
		strncpy(origAnimName, pAnim->GetName(), 255);

		const char kSearchStrCut[] = "--camera-cut-";
		char* pOriginalAnimNameEnd = strstr(origAnimName, kSearchStrCut);

		const char kSearchStrChunk[] = "-chunk-";
		char* pChunkName = strstr(origAnimName, kSearchStrChunk);
		*pChunkName = 0;

		MsgOut("Camera cut anim: %s (Frames: %d)\n", origAnimName, pAnim->m_pClipData->m_numTotalFrames);
		//ANIM_ASSERT(pOriginalAnimNameEnd);
		if (pOriginalAnimNameEnd)
		{
			// This is an added camera cut frame
			*pOriginalAnimNameEnd = 0;
			
			pEntry->m_pArtItemAnim = pAnim;
			pEntry->m_associatedAnimNameId = StringToStringId64(origAnimName);
			pEntry->m_cameraCutFrameIndex = atoi(pOriginalAnimNameEnd + strlen(kSearchStrCut));
			m_cameraCutNumEntries++;
			m_needsSort = true;
		}
		//else
		//{
		//	// This is the original anim
		//	pEntry->m_pArtItemAnim = pAnim;
		//	pEntry->m_associatedAnimNameId = StringToStringId64(origAnimName);
		//	pEntry->m_cameraCutFrameIndex = 0;
		//	m_cameraCutNumEntries++;
		//	m_needsSort = true;
		//}

	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void CameraCutManager::UnregisterAnim(const ArtItemAnim* pAnim)
{
	if (pAnim->m_flags & ArtItemAnim::kCameraCutAnim)
	{
		for (int i = 0; i < m_cameraCutNumEntries; ++i)
		{
			if (m_cameraCutAnimEntries[i].m_pArtItemAnim == pAnim)
			{
				m_cameraCutAnimEntries[i] = m_cameraCutAnimEntries[m_cameraCutNumEntries-1];
				m_cameraCutNumEntries--;
				m_needsSort = true;
				return;
			}
		}

		ANIM_ASSERTF(false, ("Camera Cut Animation '%s' was not found in the CameraCutManager and could not be unregistered.", pAnim->GetName()));
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
int CameraCutManager::GetCameraCutsInAnimation(StringId64 animNameId, CameraCutAnimEntry* pEntries, int maxNumEntries)
{
	SortCameraCutFrames();

	int numFoundEntries = 0;

	for (int i = 0; i < m_cameraCutNumEntries && numFoundEntries < maxNumEntries; ++i)
	{
		if (m_cameraCutAnimEntries[i].m_associatedAnimNameId == animNameId)
			pEntries[numFoundEntries++] = m_cameraCutAnimEntries[i];
	}

	return numFoundEntries;
}

int CameraCutManager::GetNumCameraCutAnims() const
{
	return m_cameraCutNumEntries;
}

const CameraCutAnimEntry* CameraCutManager::FindAnimEntry(const ArtItemAnim* pAnim) const
{
	for (int i=0; i<m_cameraCutNumEntries; i++)
	{
		const CameraCutAnimEntry* pEntry = &m_cameraCutAnimEntries[i];
		if (pAnim == pEntry->m_pArtItemAnim)
			return pEntry;
	}

	return nullptr;
}

const CameraCutAnimEntry* CameraCutManager::FindAnimEntry(StringId64 associatedAnimId, int frameNum) const
{
	for (int i=0; i<m_cameraCutNumEntries; i++)
	{
		const CameraCutAnimEntry* pEntry = &m_cameraCutAnimEntries[i];
		if (associatedAnimId == pEntry->m_associatedAnimNameId && frameNum == pEntry->m_cameraCutFrameIndex)
			return pEntry;
	}

	return nullptr;
}


void CameraCutManager::SortCameraCutFrames()
{
	if (!m_needsSort)
		return;

	QuickSort(m_cameraCutAnimEntries, m_cameraCutNumEntries,
		[](const CameraCutAnimEntry& lhs, const CameraCutAnimEntry& rhs) -> int { return lhs.m_cameraCutFrameIndex - rhs.m_cameraCutFrameIndex; });
	m_needsSort = false;
}

