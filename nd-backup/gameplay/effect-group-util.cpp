/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/effect-group-util.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/mesh-table.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/util/bit-reader.h"
#include "ndlib/util/common.h"

/************************************************************************/
/* Debug Loading of EFF Files                                           */
/************************************************************************/

#if !FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
struct EffectGroupHeader
{
	union
	{
		struct
		{
			U32 m_magic;
			U32 m_version;
			I32 m_tableOffset;
			U32 m_pad;
			U8 m_data[];
			//[...]
			//PatchTable m_table;
		};
		U8 m_u8;
		uintptr_t m_uintPtr;
	};

public:
	U32 GetPatchMaskSize() const 
	{ 
		U32* pSize = (U32*)(&m_u8 + m_tableOffset);
		return *pSize;
	}
	const U8* GetPatchMask() const
	{ 
		return (&m_u8 + m_tableOffset + sizeof(U32));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class DebugLoadedEffectFiles
{
public:
	DebugEffectFile* Load(const char* groupName)
	{
		if (nullptr == groupName)
			return nullptr;

		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

		char filename[FileIO::MAX_PATH_SIZE];
		sprintf(filename, "%s/effect%d/%s.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_dataDir, g_ndConfig.m_effectFolder, groupName);
		
		FileSystem::FileHandle fh;
		FileSystem::Stat stats;

		Err err = EngineComponents::GetFileSystem()->OpenSync(filename, &fh, FS_O_RDONLY);
		if (err.Failed())
			return nullptr;

		EngineComponents::GetFileSystem()->FstatSync(fh, &stats);

		if (stats.m_uSize <= 50)	// It is unclear how small the smallest valid file has to be. I've seen 118 bytes.
		{
			MsgErr("Failed to reload debug effect file '%s'. It was too small. It is empty?\n", filename);
			EngineComponents::GetFileSystem()->CloseSync(fh);
			return nullptr;
		}

		U8* pBuf = NDI_NEW U8[stats.m_uSize];
		EngineComponents::GetFileSystem()->ReadSync(fh, pBuf, stats.m_uSize);
		EngineComponents::GetFileSystem()->CloseSync(fh);

		const StringId64 fileNameId = StringToStringId64(filename);
		Entry* pEntry = GetEntry(fileNameId);

		if (pEntry && pEntry->m_pData)
		{
			delete [] pEntry->m_pData;
			pEntry->m_pData = nullptr;
		}

		const void* pLoggedInData = EffectGroupLogin(pBuf);
		if (nullptr == pLoggedInData)
		{
			MsgErr("Login failed for debug effect file '%s'\n",filename);
			delete [] pBuf;
			return nullptr;
		}

		EffectGroupHeader* pLoadable = (EffectGroupHeader*)pBuf;
		DebugEffectFile* pEffectGroup = PunPtr<DebugEffectFile*>(pLoadable->m_data);

		if (pEntry)
		{
			pEntry->m_fileNameId = fileNameId;
			pEntry->m_pData = pBuf;
		}

		return pEffectGroup;
	}

private:
	struct Entry
	{
		Entry() : m_fileNameId(INVALID_STRING_ID_64), m_pData(nullptr) { }
		StringId64 m_fileNameId;
		U8* m_pData;
	};

	static CONST_EXPR size_t kMaxEntries = 32;
	static CONST_EXPR U32 kMagicNumber = GET_CODE('E','F','F','4');
	
	Entry* GetEntry(const StringId64 fileNameId)
	{
		for (U32F i = 0; i < kMaxEntries; ++i)
		{
			if (fileNameId == m_entries[i].m_fileNameId)
				return &m_entries[i];
		}
		
		for (U32F i = 0; i < kMaxEntries; ++i)
		{
			if (INVALID_STRING_ID_64 == m_entries[i].m_fileNameId)
				return &m_entries[i];
		}

		return nullptr;
	}

	const void* EffectGroupLogin(U8* pLoadedData)
	{
		EffectGroupHeader* pHeader = (EffectGroupHeader*)pLoadedData;

		// check magic number
		if (pHeader->m_magic != kMagicNumber)
		{
			MsgErr("Effect group login had wrong magic value : 0x%.8x (expected 0x%.8x)\n", pHeader->m_magic, kMagicNumber);
			return nullptr;
		}

		// walk the patch table and patch all pointers
		uintptr_t* pDataPtr = &pHeader->m_uintPtr;
		ptrdiff_t dataAddr = &pHeader->m_u8 - (U8*)0;
		BitReader breader(pHeader->GetPatchMask(), pHeader->GetPatchMaskSize());
		U32 ptrCount = pHeader->GetPatchMaskSize()*8;
		for (U32 ptr = 0; ptr < ptrCount; ++ptr)
		{
			if (breader.ReadBit())
			{
				uintptr_t* pLocation = &pDataPtr[ptr];
				*pLocation += dataAddr;
			}
		}

		return pHeader->m_data;
	}

	Entry m_entries[kMaxEntries];
};

DebugLoadedEffectFiles g_debugEffFiles;

/// --------------------------------------------------------------------------------------------------------------- ///
DebugEffectFile* LoadEffectGroupFile(const char* filename, U32 version)
{
	DebugEffectFile* pEffectFile = g_debugEffFiles.Load(filename);

	if (!pEffectFile)
	{
		MsgAnimErr("Could not load effect file %s!\n", filename);
		return nullptr;
	}

	const char* pArtGroupName = pEffectFile->m_artGroupName;
	StringId64 artGroupSid = pEffectFile->m_artGroupId;

	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(StringToStringId64(pArtGroupName)).ToArtItem();
	if (pSkel)
	{
		ResourceTable::DevKitOnly_UpdateEffectAnimPointersForSkel(pSkel->m_skelId, pEffectFile);
	}
	else
	{
		const ArtItemGeo* pGeo = ResourceTable::LookupGeometry(StringToStringId64(pArtGroupName)).ToArtItem();
		if (pGeo)
		{
			MsgConErr("Skeleton (%s) for effect reload was not found, but a mesh was found with that name.\n", pArtGroupName);
			
			pSkel = ResourceTable::LookupSkel(pGeo->m_skelId).ToArtItem();
			if (pSkel)
			{
				MsgConErr("Did you perhaps intend to put the EFFs under artgroup '%s'?\n", ResourceTable::LookupSkelName(pSkel->m_skelId));
			}
		}
		else
		{
			MsgConErr("No skeleton or mesh to update the effect anim data for '%s'!\n", pArtGroupName);
		}
		return nullptr;
	}

	return pEffectFile;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnim* DebugEffectFile::GetEffectAnim(StringId64 animId) const
{
	for (I32F iAnim = 0; iAnim < m_numAnims; iAnim++)
	{
		const EffectAnim* pAnim = &m_anims[iAnim];

		if (pAnim->m_nameId == animId)
		{
			return pAnim;
		}
	}

	return nullptr;
}

#endif // FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkEffectGroupUtil()
{
	// don't dead strip this translation unit!
}
