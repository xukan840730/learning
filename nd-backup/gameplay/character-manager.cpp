/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-manager.h"

#include "ndlib/nd-frame-state.h"

#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/tools-shared/patdefs.h"

#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-track-group.h"

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterManager* CharacterManager::s_pSingleton = nullptr;
I32 g_cheapNpcCount = 0;

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterManager::CharacterManager(U32 maxCharacterCount) : m_accessLock(JlsFixedIndex::kCharacterManagerLock, SID("CharacterManagerLock"))
{
	m_maxCharacterCount = maxCharacterCount;
	m_characterCount = 0;
	m_observerList.InitHead();

	m_characterList = NDI_NEW MutableCharacterHandle[m_maxCharacterCount];

	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::Init(U32 maxCharacterCount)
{
	AllocateJanitor jj(kAllocNpcGlobals, FILE_LINE_FUNC);

	ALWAYS_ASSERT(s_pSingleton == nullptr);
	s_pSingleton = NDI_NEW CharacterManager(maxCharacterCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::Reset()
{
	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	for (I32F i = 0; i < m_characterCount; ++i)
	{
		if (Character* pCharacter = m_characterList[i].ToMutableProcess())
		{
			KillProcess(pCharacter);
		}
	}

	m_characterCount = 0;
	m_observerList.InitHead();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::Evaluation()
{
	PROFILE(AI, CharacterManager_Evaluation);

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	// remove stale handles
	for (U32F i = 0; i < m_characterCount; ++i)
	{
		if (!m_characterList[i].HandleValid())
		{
			m_characterList[i] = m_characterList[--m_characterCount];
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::DebugDraw(WindowContext* pDebugWindowContext, float textScale) const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	for (I32F i = 0; i < m_characterCount; ++i)
	{
		if (const Character* pChar = m_characterList[i].ToProcess())
		{
			const Point aboveNpcPosition = pChar->GetTranslation() + Vector(0.0f, 1.8f, 0.0f);
			DebugPrimTime pTime = kPrimDuration1Frame;
			ScreenSpaceTextPrinter printer(aboveNpcPosition, ScreenSpaceTextPrinter::kPrintNextLineAbovePrevious, pTime, textScale);

			pChar->DebugDraw(pDebugWindowContext, &printer);
		}
	}

#if !SUBMISSION_BUILD
	if (g_cheapNpcCount != 0)
		MsgCon("%d cheap NPCs spawned\n", g_cheapNpcCount);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::Register(Character* pCharacter)
{
	ASSERT(pCharacter);
	if (!pCharacter) // || pCharacter->IsCheap()) // @CHEAP no, we should register all characters so they can be debug-drawn etc.
		return;

	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(m_characterCount < m_maxCharacterCount);
	//<unknown msg function>("CharacterManager::Register(%s)\n", pCharacter->GetName());

	// Validate that this character doesn't already exist or another character with the same userId exists
	for (U32F i = 0; i < m_characterCount; ++i)
	{
		if (const Character* pCheckCharacter = m_characterList[i].ToProcess())
		{
			ALWAYS_ASSERT(pCheckCharacter != pCharacter);
			GAMEPLAY_ASSERTF((pCheckCharacter->IsKindOf(SID("NdProcessRagdoll"))
							  && pCharacter->IsKindOf(SID("NdProcessRagdoll")))
								 || pCheckCharacter->GetUserId() != pCharacter->GetUserId(),
							 ("Multiple characters with the same name: %s. This is not allowed. You probably want to use multispawner. Spawner %s",
							  DevKitOnly_StringIdToString(pCharacter->GetUserId()),
							  DevKitOnly_StringIdToString(pCharacter->GetSpawnerId())));
		}
	}

	if (m_characterCount < m_maxCharacterCount)
	{
		m_characterList[m_characterCount++] = pCharacter;
	}

	for (Observer* pObserver = m_observerList.m_pNext; pObserver != &m_observerList; pObserver = pObserver->m_pNext)
	{
		pObserver->CharacterRegistered(pCharacter);
	}

	ALWAYS_ASSERT(m_characterCount <= m_maxCharacterCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterManager::Unregister(Character* pCharacter)
{
	ASSERT(pCharacter);
	if (!pCharacter) // || pCharacter->IsCheap()) // @CHEAP no, we should register all characters so they can be debug-drawn etc.
		return false;

	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	bool foundCharacter = false;
	for (I32F i = m_characterCount - 1; i >= 0; --i)
	{
		const Character* pCurChar = m_characterList[i].ToProcess();
		if (pCurChar == pCharacter)
		{
			ASSERT(i < m_characterCount);
			ALWAYS_ASSERT(m_characterCount > 0);
			//<unknown msg function>("CharacterManager::Unregister(%s)\n", pCharacter->GetName());
			m_characterList[i] = m_characterList[--m_characterCount];
			foundCharacter = true;
		}
	}

	if (foundCharacter)
	{
		for (Observer* pObserver = m_observerList.m_pNext; pObserver != &m_observerList; pObserver = pObserver->m_pNext)
		{
			pObserver->CharacterUnRegistered(pCharacter);
		}

		// Make sure we don't leak job counter
		LegRaycaster* pLegRaycaster = pCharacter->GetLegRaycaster();
		if (pLegRaycaster)
			pLegRaycaster->CollectCollisionProbeResults();

	}
	//else
	//	<unknown msg function>("CharacterManager::Unregister(%s) -- NOT FOUND!\n", pCharacter->GetName());

	ALWAYS_ASSERT(m_characterCount >= 0);
	return foundCharacter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character* CharacterManager::FindCharacterByUserId(StringId64 sid) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	if (sid == SID("self"))
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		if (pGroupInst)
		{
			Process* pProc = pGroupInst->ResolveSelf();
			if (pProc && pProc->IsKindOf(g_type_Character))
				return static_cast<Character*>(pProc);
		}
		return nullptr;
	}
	else if (sid == SID("player")) // yes, go ahead and work properly in single-player too
	{
		return Character::FromProcess(EngineComponents::GetNdGameInfo()->GetPlayerGameObject());
	}

	for (U32F i = 0; i < m_characterCount; ++i)
	{
		if (Character* pChar = m_characterList[i].ToMutableProcess())
		{
			if (pChar->GetUserId() == sid)
			{
				return pChar;
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character* CharacterManager::FindCharacterByProcessType(StringId64 processTypeNameId) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	for (I32F i = 0; i < m_characterCount; ++i)
	{
		if (Character* pChar = m_characterList[i].ToMutableProcess())
		{
			if (pChar->IsKindOf(processTypeNameId))
			{
				return pChar;
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character* CharacterManager::GetCharacter(U32F index) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(index < m_maxCharacterCount);
	return m_characterList[index].ToMutableProcess();
}

/// -------------------------------------------------------------------------------------------------------------------
MutableCharacterHandle CharacterManager::GetCharacterHandle(U32F index) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(index < m_maxCharacterCount);
	return m_characterList[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F CharacterManager::GetCharactersFriendlyToFaction(const FactionId factionId, const Character* apFriend[], U32F capacity) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	const U32F numCharacters = GetNumCharacters();
	U32F numCharactersFriendly = 0;

	for (U32F i = 0; i < numCharacters; ++i)
	{
		const Character* pCharacter = m_characterList[i].ToProcess();
		if (pCharacter && !pCharacter->IsDead() && g_factionMgr.IsFriend(pCharacter->GetFactionId(), factionId) && numCharactersFriendly < capacity)
		{
			apFriend[numCharactersFriendly] = pCharacter;
			++numCharactersFriendly;
		}
	}

	return numCharactersFriendly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::SetupLegIkCollision(U32F bucket)
{
	PROFILE(AI, CharacterManager_SetupLegIk);

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	const Point cameraPos = GetRenderCamera().GetPosition();

	for (U32F i = 0; i < m_characterCount; ++i)
	{
		MutableCharacterHandle hChar = GetCharacterHandle(i);

		if (!hChar.HandleValid())
		{
			continue;
		}

		Character *const pChar = hChar.ToMutableProcess();
		if (!pChar)
		{
			continue;
		}

		if (GetBucketForCharacter(*pChar) != bucket)
		{
			continue;
		}

		const bool isPlayer = EngineComponents::GetNdGameInfo()->IsPlayerHandle(hChar);
		if (!isPlayer &&	// Players go every frame
			!g_ndConfig.m_pNetInfo->ShouldRunGeneralSystemAtDistance(pChar->GetLocator().GetPosition(), cameraPos))
		{	// LOD out character leg raycasts at a certain distance
			continue;
		}

		LegRaycaster *const pLegRaycaster = pChar->GetLegRaycaster();
		if (!pLegRaycaster)
		{
			continue;
		}

		pLegRaycaster->KickCollisionProbe();

		if (!isPlayer)
		{	// Player kicks their own mesh raycasts in character-leg-ik-controller.cpp / CharacterLegIkController::Update()
			const bool noMeshIk = pChar->GetCurrentPat().GetPlayerNoMeshIk();
			pLegRaycaster->KickMeshRaycasts(pChar, noMeshIk);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::CollectLegIkCollision(U32F bucket)
{
	PROFILE(AI, CharacterManager_ColLegIk);

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	if (m_characterCount == 0)
		return;

	for (U32F i = 0; i < m_characterCount; ++i)
	{
		Character* pChar = m_characterList[i].ToMutableProcess();
		if (!pChar)
			continue;

		if (GetBucketForCharacter(*pChar) != bucket)
			continue;

		LegRaycaster* pLegRaycaster = pChar->GetLegRaycaster();
		if (!pLegRaycaster)
			continue;

		pLegRaycaster->CollectCollisionProbeResults();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::RegisterObserver(Observer* pObserver)
{
	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	pObserver->InitHead();
	pObserver->InsertAfter(&m_observerList);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterManager::UnRegisterObserver(Observer* pObserver)
{
}

U32F CharacterManager::GetBucketForCharacter(const Character& character) const
{
	const Process* pDefaultTree = EngineComponents::GetProcessMgr()->m_pDefaultTree;

	const Process* pParentProcess = character.GetParentProcess();

	if (pDefaultTree == pParentProcess)
	{
		return ProcessBucket::kProcessBucketDefault;
	}

	// right now we assume characters are either in the character or default buckets
	return ProcessBucket::kProcessBucketCharacter;
}