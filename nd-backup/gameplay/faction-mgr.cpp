/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/faction-mgr.h"

#include "corelib/containers/static-map.h"
#include "gamelib/level/entitydb.h"

/// ----------------------------------------------------------------------------
FactionMgr g_factionMgr;

/// ----------------------------------------------------------------------------
///
///
/// FactionId
///
///
/// ----------------------------------------------------------------------------
bool FactionId::IsValid() const
{
	return g_factionMgr.IsValid(*this);
}

const char* FactionId::GetName() const
{
	return g_factionMgr.GetFactionName(*this);
}

StringId64 FactionId::GetNameId() const
{
	return g_factionMgr.GetFactionNameId(*this);
}

FactionId FactionIdInvalid()
{
	return FactionId();
}

/// ----------------------------------------------------------------------------
///
///
/// FactionMgr
///
///
/// ----------------------------------------------------------------------------
FactionMgr::FactionMgr()
{
	m_factionDefs = nullptr;
	m_pEnemyTable = nullptr;
	m_pFriendTable = nullptr;
	m_factionCount = 0;
}

void FactionMgr::Init(U32 factionCount, const char** factionNames, const char** factDictionaryNames)
{
	U32F tableByteCount = (factionCount * factionCount + 7) / 8;
	m_factionDefs = NDI_NEW (kAllocNpcGlobals) FactionDef[factionCount];
	m_pEnemyTable = NDI_NEW (kAllocNpcGlobals) U8[tableByteCount];
	m_pFriendTable = NDI_NEW (kAllocNpcGlobals) U8[tableByteCount];

	for (U32F iFaction = 0; iFaction < factionCount; ++iFaction)
	{
		FactionDef& def = m_factionDefs[iFaction];
		def.strName = factionNames[iFaction];
		def.nameId = StringToStringId64(def.strName);
		def.factDictId = INVALID_STRING_ID_64;

		if (factDictionaryNames)
		{
			const char* factDictName = factDictionaryNames[iFaction];
			if (factDictName && *factDictName != '\0')
				def.factDictId = StringToStringId64(factDictName);
		}
	}
	memset(m_pEnemyTable, 0, tableByteCount);
	memset(m_pFriendTable, 0, tableByteCount);
	
	m_factionCount = factionCount;
}


void FactionMgr::Shutdown()
{
	NDI_DELETE[] m_factionDefs;
	NDI_DELETE[] m_pEnemyTable;
	NDI_DELETE[] m_pFriendTable;
	m_factionDefs = nullptr;
	m_pEnemyTable = nullptr;
	m_pFriendTable = nullptr;
	m_factionCount = 0;
}

void FactionMgr::SetBit(U8* bits, U32F index) const
{
	ALWAYS_ASSERT(index < m_factionCount*m_factionCount);

	U32F iByte = index >> 3;
	U32F iBit = index & 7U;
	bits[iByte] |= (1U << iBit);
}

void FactionMgr::ClearBit(U8* bits, U32F index) const
{
	ALWAYS_ASSERT(index < m_factionCount*m_factionCount);

	U32F iByte = index >> 3;
	U32F iBit = index & 7U;
	bits[iByte] &= ~(1U << iBit);
}

bool FactionMgr::IsBitSet(const U8* bits, U32F index) const
{
	ALWAYS_ASSERT(index < m_factionCount*m_factionCount);

	U32F iByte = index >> 3;
	U32F iBit = index & 7U;
	return bits[iByte] & (1U << iBit);
}

FactionMgr::Status FactionMgr::GetFactionStatus(FactionId lhs, FactionId rhs) const
{
	bool isFriend = IsFriend(lhs, rhs);
	bool isEnemy = IsEnemy(lhs, rhs);
	ASSERT(!(isFriend && isEnemy)); // can't be both
	if (isEnemy) return kStatusEnemy;
	if (isFriend) return kStatusFriend;
	return kStatusNeutral;
}

void FactionMgr::SetFactionStatus(FactionId lhs, FactionId rhs, Status status)
{
	U32F i1 = lhs.GetRawFactionIndex() + rhs.GetRawFactionIndex() * m_factionCount;
	U32F i2 = rhs.GetRawFactionIndex() + lhs.GetRawFactionIndex() * m_factionCount;
	switch(status)
	{
	case kStatusNeutral:
		{
			ClearBit(m_pFriendTable, i1);
			ClearBit(m_pFriendTable, i2);
			ClearBit(m_pEnemyTable, i1);
			ClearBit(m_pEnemyTable, i2);
		}
		break;
	case kStatusFriend:
		{
			SetBit(m_pFriendTable, i1);
			SetBit(m_pFriendTable, i2);
			ClearBit(m_pEnemyTable, i1);
			ClearBit(m_pEnemyTable, i2);
		}
		break;
	case kStatusEnemy:
		{
			ClearBit(m_pFriendTable, i1);
			ClearBit(m_pFriendTable, i2);
			SetBit(m_pEnemyTable, i1);
			SetBit(m_pEnemyTable, i2);
		}
		break;
	default:
		ASSERT(false); // invalid status
		break;
	}
}

const FactionId FactionMgr::LookupFactionByNameId(StringId64 nameId) const
{
	U32F factionCount = m_factionCount;
	for (U32F iFaction = 0; iFaction < factionCount; ++iFaction)
	{
		const FactionDef& def = m_factionDefs[iFaction];
		if (def.nameId == nameId)
		{
			return FactionId(iFaction);
		}
	}
	// faction not found
	return FactionId::kInvalid;
}

bool FactionMgr::IsValid(FactionId faction) const
{
	return faction.GetRawFactionIndex() < m_factionCount;
}

bool FactionMgr::IsFriend(FactionId lhs, FactionId rhs) const
{
	if (lhs == FactionId::kInvalid)
		return false;

	if (rhs == FactionId::kInvalid)
		return false;

	return IsBitSet(m_pFriendTable, lhs.GetRawFactionIndex() + rhs.GetRawFactionIndex() * m_factionCount);
}

bool FactionMgr::IsEnemy(FactionId lhs, FactionId rhs) const
{
	if (lhs == FactionId::kInvalid)
		return false;

	if (rhs == FactionId::kInvalid)
		return false;

	return IsBitSet(m_pEnemyTable, lhs.GetRawFactionIndex() + rhs.GetRawFactionIndex() * m_factionCount);
}

const char* FactionMgr::GetFactionName(FactionId fac) const
{
	const char* strName = "<unknown>";
	U32F iFac = fac.GetRawFactionIndex();
	if (iFac < m_factionCount)
	{
		strName = m_factionDefs[iFac].strName;
	}
	return strName;
}

StringId64 FactionMgr::GetFactionNameId(FactionId fac) const
{
	StringId64 nameId = INVALID_STRING_ID_64;
	U32F iFac = fac.GetRawFactionIndex();
	ASSERT(iFac < m_factionCount);
	if (iFac < m_factionCount)
	{
		nameId = m_factionDefs[iFac].nameId;
	}
	return nameId;
}

StringId64 FactionMgr::GetFactionFactDictionaryId(FactionId fac) const
{
	StringId64 dictId = INVALID_STRING_ID_64;
	U32F iFac = fac.GetRawFactionIndex();
	if (iFac < m_factionCount)
	{
		dictId = m_factionDefs[iFac].factDictId;
	}
	return dictId;
}

static void RepeatStringToStream(DoutBase* pStream, const char* str, I32F count)
{
	for (I32F i = 0; i < count; ++i)
	{
		pStream->Print(str);
	}
}

void FactionMgr::DumpFactionMatrixToStream(DoutBase* pStream) const
{
	U32F factionCount = m_factionCount;
	// determine how much to indent based on longest faction name
	U32F indentAmount = 8;
	for (U32F iFaction = 0; iFaction < factionCount; ++iFaction)
	{
		const FactionDef& def = m_factionDefs[iFaction];
		U32F strLen = strlen(def.strName);
		indentAmount = Max(indentAmount, strLen + 1);
	}
	// first line, indention, then 2 character abbreviations of factions
	RepeatStringToStream(pStream, " ", indentAmount+1);
	for (U32F iCol = 0; iCol < factionCount; ++iCol)
	{
		const char* strFaction = GetFactionName(FactionId(iCol));
		pStream->Printf("%c%c", strFaction[0], strFaction[1]);
	}
	// second line, a bunch of dashes
	pStream->Print("\n");
	RepeatStringToStream(pStream, "-", indentAmount + 2*factionCount + 1);
	pStream->Print("\n");
	// for each faction, a line
	for (U32F iRow = 0; iRow < factionCount; ++iRow)
	{
		FactionId facRow = FactionId(iRow);
		const char* strFaction = GetFactionName(facRow);
		U32F strLen = strlen(strFaction);
		ASSERT(strLen < indentAmount);
		pStream->Print(strFaction);
		RepeatStringToStream(pStream, " ", indentAmount - strLen);
		for (U32F iCol = 0; iCol < factionCount; ++iCol)
		{
			FactionId facCol = FactionId(iCol);
			const char* str = " ?";
			switch (GetFactionStatus(facRow, facCol))
			{
			case kStatusNeutral:   str = " ."; break;
			case kStatusFriend:    str = " +"; break;
			case kStatusEnemy:     str = " -"; break;
			default:
				ASSERT(false);
				break;
			}
			pStream->Print(str);
		}
		pStream->Print("\n");
	}
	// final line
	pStream->Print("\n   (. = neutral, + = friendly, - = enemy)\n");
}

// ----------------------------------------------------------------------------------------------------------
// Dev Menu Stuff (could move to another .cpp file)
// ----------------------------------------------------------------------------------------------------------

#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"

enum SpecialFaction
{
	kSpecialFactionFriends = -1,
	kSpecialFactionBuddies = -2,
	kSpecialFactionEnemies = -3,
};

static bool WalkSelectAllByFaction(Process* pProcess, void* pData)
{
	const NdGameObject* pGo = NdGameObject::FromProcess(pProcess);
	if (pGo)
	{
		intptr_t iFaction = reinterpret_cast<intptr_t>(pData);
		if (iFaction >= 0)
		{
			FactionId factionId((uintptr_t)iFaction);
			if (pGo->GetFactionId() == factionId)
			{
				DebugSelection::Get().SelectProcess(pGo);
			}
		}
		else if (iFaction == kSpecialFactionFriends)
		{
			if (g_factionMgr.IsFriend(EngineComponents::GetNdGameInfo()->GetPlayerFaction(), pGo->GetFactionId()))
			{
				DebugSelection::Get().SelectProcess(pGo);
			}
		}
		else if (iFaction == kSpecialFactionBuddies)
		{
			if (g_factionMgr.IsFriend(EngineComponents::GetNdGameInfo()->GetPlayerFaction(), pGo->GetFactionId())
			&&  !EngineComponents::GetNdGameInfo()->IsPlayerHandle(pGo))
			{
				DebugSelection::Get().SelectProcess(pGo);
			}
		}
		else if (iFaction == kSpecialFactionEnemies)
		{
			if (g_factionMgr.IsEnemy(EngineComponents::GetNdGameInfo()->GetPlayerFaction(), pGo->GetFactionId()))
			{
				DebugSelection::Get().SelectProcess(pGo);
			}
		}
	}

	return true;
}

static bool OnSelectAllByFaction(DMENU::Item& item, DMENU::Message message)
{
	if (message == DMENU::kExecute)
	{
		//DebugSelection::Get().DeselectAll(); // no, allow the user to build up a selection from multiple factions
		EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pRootTree, WalkSelectAllByFaction, item.m_pBlindData);
		return true;
	}
	return false;
}

//static
DMENU::Item* FactionMgr::CreateSelectAllByFactionMenuItem(const char* itemName, const char* menuTitle)
{
	DMENU::Menu* pSubMenu = NDI_NEW DMENU::Menu(menuTitle);

	U32F factionCount = g_factionMgr.m_factionCount;
	for (U32F iFaction = 0; iFaction < factionCount; ++iFaction)
	{
		const FactionDef& def = g_factionMgr.m_factionDefs[iFaction];
		const char* strFaction = def.strName;

		pSubMenu->PushSortedItem(NDI_NEW DMENU::ItemFunction(strFaction, OnSelectAllByFaction, (void*)(uintptr_t)iFaction));
	}

	pSubMenu->PushFrontItem(NDI_NEW DMENU::ItemDivider());
	pSubMenu->PushFrontItem(DebugSelection::CreateDeselectAllObjectsMenuItem("Deselect All Objects"));
	pSubMenu->PushFrontItem(NDI_NEW DMENU::ItemDivider());
	pSubMenu->PushFrontItem(NDI_NEW DMENU::ItemFunction("Enemies", OnSelectAllByFaction, (void*)kSpecialFactionEnemies));
	pSubMenu->PushFrontItem(NDI_NEW DMENU::ItemFunction("Just Friends", OnSelectAllByFaction, (void*)kSpecialFactionBuddies));
	pSubMenu->PushFrontItem(NDI_NEW DMENU::ItemFunction("Player and Friends", OnSelectAllByFaction, (void*)kSpecialFactionFriends));

	return NDI_NEW DMENU::ItemSubmenu(itemName, pSubMenu);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 BuildFactionMask(FactionId faction)
{
	// current assumption is that invalid faction means all factions are allowed
	if (faction == FactionId::kInvalid)
		return ~0U;

	return (1U << faction.GetRawFactionIndex());
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 BuildFactionMask(const EntityDB* pDb)
{
	if (!pDb)
		return ~0U;

	U32F factionIdMask = 0;

	// Gather all faction tags
	for (EntityDB::RecordMap::const_iterator it = pDb->GetFirst(SID("faction"));
		 it != pDb->End() && it->first == SID("faction");
		 ++it)
	{
		const EntityDB::Record* pRec = it->second;
		if (!pRec)
			continue;

		const StringId64 factionId = pRec->GetData<StringId64>(INVALID_STRING_ID_64);
		const FactionId faction  = g_factionMgr.LookupFactionByNameId(factionId);

		if (faction != FactionId::kInvalid)
		{
			factionIdMask |= (1U << faction.GetRawFactionIndex());
		}
	}

	// If no faction tags are specified then allow all factions
	if (factionIdMask == 0)
	{
		factionIdMask = ~0U;
	}

	return factionIdMask;
}
