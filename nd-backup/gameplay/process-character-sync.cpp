/*
* Copyright (c) 2012 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/process-character-sync.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"

ProcessCharacterSync::ProcessCharacterSync()
	: m_numChars(0), m_numRemovedChars(0)
{
	m_pipeId = ProcessUpdate::kClassic;
	SetForceClassic(true);
}

Err ProcessCharacterSync::Init(const ProcessSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	m_bucketEventMask = 0; // you need to set it for the types of events you want to receive
	return result;
}

Err ProcessCharacterSync::PostInit(const ProcessSpawnInfo& info)
{
	Err result = ParentClass::PostInit(info);

	if (result.Succeeded())
	{
		EngineComponents::GetAnimMgr()->RegisterBucketObserver(this, m_bucketEventMask);
	}

	return result;
}

I32F ProcessCharacterSync::GetCharacterIndex(const NdGameObject* pChar) const
{
	for (I32F i = 0; i < m_numChars; ++i)
	{
		if (m_hChars[i].ToProcess() == pChar)
		{
			return i;
		}
	}

	return -1;
}

void ProcessCharacterSync::AddCharacter(const NdGameObject* pChar)
{
	// Don't bother adding a character twice.
	if (CharacterExists(pChar))
		return;

	// We always keep the character array tightly packed.
	if (m_numChars >= kMaxCharacters)
	{
		GoError("Too many characters added, max %d", kMaxCharacters);
		return;
	}

	// bad const cast here
	m_hChars[m_numChars++] = (NdGameObject*)pChar;
	CharacterAdded(pChar);
}

// this can be called by multiple characters at the same time.
void ProcessCharacterSync::RemoveCharacter(NdGameObject* pChar, CharacterSyncRemovedReason reason, StringId64 reasonName)
{
	RecursiveAtomicLockJanitor64 jj(&m_lock, FILE_LINE_FUNC);

	I32F index = GetCharacterIndex(pChar);
	if (index < 0)
	{
		// Character not in process
		return;
	}

	// Pack the array
	--m_numChars;
	for (I32F i = index; i < m_numChars; ++i)
	{
		m_hChars[i] = m_hChars[i + 1];
	}
	m_hRemovedChars[m_numRemovedChars++] = pChar;
	m_hChars[m_numChars] = nullptr;
	CharacterRemoved(pChar, reason, reasonName);

	if (!EnteredOnKill() && m_numChars == 0)
	{
		KillProcess(this);
	}
}

void ProcessCharacterSync::BucketEventNotify(U32F bucket, U32F updateType)
{
	if (bucket == kProcessBucketCharacter)
	{
		// verify all my observed objects are in the same pipe as me
		{
			for (U32 iChar = 0; iChar < m_numChars; iChar++)
			{
				const NdGameObject* pChar = m_hChars[iChar].ToProcess();
				if (pChar)
				{
					if (pChar->GetPipelineId() != GetPipelineId())
					{
						DebugPrintInfo();
					}
					GAMEPLAY_ASSERT(pChar->GetPipelineId() == GetPipelineId());
				}
			}
		}

		switch (updateType)
		{
		case AnimMgr::kPreAnimUpdate:
			PreAnimUpdate();
			break;
		case AnimMgr::kPostAnimUpdate:
			PostAnimUpdate();
			break;
		case AnimMgr::kPostAnimBlending:
			PostAnimBlending();
			break;
		case AnimMgr::kPostJointUpdate:
			PostJointUpdate();
			break;
		}
	}
}

U32 ProcessCharacterSync::GetNumSyncCharacters()
{ 
	return m_numChars + m_numRemovedChars; 
}

NdGameObject*	ProcessCharacterSync::GetSyncCharacters(int index)
{ 
	if (index < m_numChars)
		return m_hChars[index].ToMutableProcess();
	else
		return m_hRemovedChars[index - m_numChars].ToMutableProcess();
}

PROCESS_REGISTER(ProcessCharacterSync, Process);


void ProcessCharacterSync::Active::Update()
{
	ParentClass::Update();

	ProcessCharacterSync &pp = Self();

	// If any of our characters suddenly up and disappear,
	// we abort everything. We don't need to force derived
	// classes to be able to handle this bizarre case.
	for (U32F i = 0; i < pp.m_numChars; ++i)
	{
		if (!pp.m_hChars[i].HandleValid())
		{
			MsgErr("ProcessCharacterSync: A character has up and disappeared! Killing process.\n");
			KillProcess(pp);
			return;
		}
	}

	pp.m_numRemovedChars = 0;

	// only kill this process if we're not aborting because we've been killed
	// a little heavy handed, but better than having to pass through that we're in onkill
	// from the source
	if (!pp.EnteredOnKill() && pp.m_numChars == 0)
	{
		KillProcess(&pp);
	}
	else
	{
		// We must re-register our observer every frame
		EngineComponents::GetAnimMgr()->RegisterBucketObserver(&pp, pp.m_bucketEventMask);
	}
}

STATE_REGISTER(ProcessCharacterSync, Active, kPriorityMediumLow);

