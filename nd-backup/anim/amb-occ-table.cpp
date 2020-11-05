/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/amb-occ-table.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/render/ndgi/ndgi-consts.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AmbientOccludersTable::Entry AmbientOccludersTable::m_table[kMaxNumEntries];
I32F AmbientOccludersTable::m_numEntries	  = 0;
I32F AmbientOccludersTable::m_currentMemUsage = 0;
I32F AmbientOccludersTable::m_maxMemUsage	  = 0;

/// --------------------------------------------------------------------------------------------------------------- ///
///  observer mechanism
/// --------------------------------------------------------------------------------------------------------------- ///
static AmbientOccludersTable::Observer s_observerList;

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::InitializeObservers()
{
	s_observerList.InitHead();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::RegisterObserver(Observer* pObserver)
{
	pObserver->InitHead();
	pObserver->InsertAfter(&s_observerList);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::UnregisterObserver(Observer* pObserver)
{
	pObserver->Unlink();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AmbientOccludersTable::Entry* AmbientOccludersTable::LookupEntry(StringId64 meshNameId)
{
	for (I32F i = m_numEntries - 1; i >= 0; i--)
	{
		if (m_table[i].m_meshNameId == meshNameId)
		{
			return &m_table[i];
		}
	}

	// couldn't find the desired entry
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AmbientOccludersTable::Entry* AmbientOccludersTable::LookupEntry(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo)
{
	for (I32F i = m_numEntries - 1; i >= 0; i--)
	{		
		if (m_table[i].m_pAmbientOccludersInfo == pAmbientOccludersInfo)
		{
			return &m_table[i];
		}
	}

	// couldn't find the desired entry
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AmbientShadowsOccludersInfo* AmbientOccludersTable::LookupAmbientOccluders(StringId64 meshNameId)
{
	const Entry* pEntry = LookupEntry(meshNameId);
	return pEntry ? pEntry->m_pAmbientOccludersInfo : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::IncrementRefCount(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo)
{
	Entry* pEntry = LookupEntry(pAmbientOccludersInfo);
	if (pEntry)
	{
		++pEntry->m_refCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::DecrementRefCount(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo)
{
	Entry* pEntry = LookupEntry(pAmbientOccludersInfo);
	if (pEntry)
	{
		--pEntry->m_refCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AmbientOccludersTable::GetReferenceCountForPackage(StringId64 packageNameId)
{
	U32F refCount = 0;

	for (I32F i = m_numEntries - 1; i >= 0; i--)
	{
		if (m_table[i].m_packageNameId == packageNameId)
		{
			refCount += m_table[i].m_refCount;
		}
	}

	return refCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::LoginAmbientShadowsOccluder(const AmbientShadowsOccludersInfo* pOccluderInfo,
														const char* pPackageName)
{
	const StringId64 packageNameId = StringToStringId64(pPackageName);
	const StringId64 meshNameId = (pOccluderInfo->m_meshNameId == INVALID_STRING_ID_64) ? packageNameId : pOccluderInfo->m_meshNameId;

	if (m_numEntries >= kMaxNumEntries)
	{
		MsgErr("Too many ambient occluders logged in. Skipping 'package: %s, mesh: %s'\n", pPackageName, DevKitOnly_StringIdToString(meshNameId));
		return;
	}

	const Entry* pEntry = LookupEntry(meshNameId);

	if (!pEntry)
	{
		Entry& newEntry = m_table[m_numEntries];
		AmbientShadowsVolumeOccluderTextures* pOccTex = NDI_NEW(kAlign16) AmbientShadowsVolumeOccluderTextures[pOccluderInfo->m_numVolumeOccluders];

		newEntry.m_pAmbientOccludersInfo			= pOccluderInfo;
		newEntry.m_pAmbientVolumeOccluderTextures   = pOccTex;
		newEntry.m_pPackageName						= pPackageName;
		newEntry.m_packageNameId					= packageNameId;
		newEntry.m_meshNameId						= meshNameId;
		newEntry.m_refCount							= 0;

		for (U32 iVolume = 0; iVolume < pOccluderInfo->m_numVolumeOccluders; iVolume++)
		{
			AmbientShadowsOccludersInfo::VolumeOccluderData&		vol = pOccluderInfo->m_volumeOccluders[iVolume];

			U32 dataSize = vol.m_volumeSizeX * vol.m_volumeSizeY * vol.m_volumeSizeZ * 4;

			ndgi::Texture3dDesc desc(vol.m_volumeSizeX, vol.m_volumeSizeY, vol.m_volumeSizeZ, 1, ndgi::kRgba8unorm, ndgi::kBindDefault, ndgi::kUsageImmutable, 0);

			//ndgi::SubResourceDesc *paSubResource = STACK_ALLOC(ndgi::SubResourceDesc, vol.m_volumeSizeZ);
			//CreateSubResDescription(paSubResource, vol.m_volumeSizeZ, 1, vol.m_volumeSizeX, vol.m_volumeSizeY, ndgi::kRgba8unorm, (U8*)vol.m_data, dataSize);
			// the function above assumes the texture went through regular pipeline. But in this case we are dealing with a custom texture data
			// hence all the hardcoded values, so we just create a description ourselves here, since CreateTexture3d only cares about data pointer

			ndgi::SubResourceDesc srdesc;
			srdesc.m_pData = (U8*)vol.m_data;
			pOccTex[iVolume].m_hTex3d.Create(kAllocGpuTexture, desc, &srdesc);
			pOccTex[iVolume].m_hTex3d.SetDebugName("ShadowOccluderVolume");

			m_currentMemUsage += pOccTex[iVolume].m_hTex3d.GetMemorySize();
			m_maxMemUsage = Max(m_currentMemUsage, m_maxMemUsage);
		}

		m_numEntries++;

		pEntry = &newEntry;
	}
	else
	{
		MsgErr("Ambient occluders logged in from multiple actors (%s and %s)\n", pEntry->m_pPackageName, pPackageName );
		return;
	}

	// Mesh Login Notification	
	for (Observer * pObserver = s_observerList.m_pNext; pObserver != &s_observerList; pObserver = pObserver->m_pNext)
	{
		pObserver->OnLogin(*pEntry);
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::LogoutAmbientShadowsOccluder(const AmbientShadowsOccludersInfo* pOccluderInfo,
														 const char* pPackageName)
{
	// Remove it from the entries.
	for (I32F i=0; i<m_numEntries; i++)
	{
		if (m_table[i].m_pAmbientOccludersInfo == pOccluderInfo)
		{
			Entry entryRemoved = m_table[i];

			for (U32 iVolume = 0; iVolume < pOccluderInfo->m_numVolumeOccluders; iVolume++)
			{
				AmbientShadowsVolumeOccluderTextures& tex = m_table[i].m_pAmbientVolumeOccluderTextures[iVolume];

				m_currentMemUsage -= tex.m_hTex3d.GetMemorySize();
				ANIM_ASSERT(m_currentMemUsage >= 0);

				tex.m_hTex3d.Release();
// I think this should be queued up for the render frame to process
// because the Release looks really suspect. But I have no conclusive evidence.
//				tex.m_hTex3d = pTextureMgr.EnqueueNdgiTextureRelease(tex.m_hTex3d, GetCurrentRenderFrameParams()->m_frameNumber);
			}

			NDI_DELETE m_table[i].m_pAmbientVolumeOccluderTextures;

			m_table[i].m_pAmbientOccludersInfo = nullptr;
			m_table[i] = m_table[m_numEntries - 1];
			--i;
			--m_numEntries;

			// amb occluders Logout Notification
			for (Observer * pObserver = s_observerList.m_pNext; pObserver != &s_observerList; pObserver = pObserver->m_pNext)
			{
				pObserver->OnLogout(entryRemoved);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AmbientOccludersTable::DumpToTty()
{
	MsgOut("\nLogged In amb occluder volumes:\n");
	MsgOut("%40s,%40s\n", "PACKAGE", "MESH");

	for (I32F i=0; i<m_numEntries; i++)
	{
		const char *meshName = DevKitOnly_StringIdToString( m_table[i].m_meshNameId );
		MsgOut("%40s,%40s\n", m_table[i].m_pPackageName, meshName );
	}
}
