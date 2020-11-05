/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/linkednode.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/faction-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
/// Class CharacterManager: Manage the entire list of Characters (includes both npcs and players)
/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterManager
{
public:
	static CharacterManager* s_pSingleton;
	static CharacterManager& GetManager() { return *s_pSingleton; };
	NdRwAtomicLock64_Jls* GetLock() { return &m_accessLock; };
	static void Init(U32 maxCharacterCount);

	CharacterManager(U32 maxCharacterCount);

	void Reset();
	void Evaluation();
	void DebugDraw(WindowContext* pDebugWindowContext, float textScale) const;

	void Register(Character* pCharacter);
	bool Unregister(Character* pCharacter);

	I32F GetNumCharacters() const { return m_characterCount; }
	// You need to check the return value for nullptr because there may be stale handles in the list!
	Character* GetCharacter(U32F index) const;
	MutableCharacterHandle GetCharacterHandle(U32F index) const;
	Character* FindCharacterByUserId(StringId64 sid) const;
	Character* FindCharacterByProcessType(StringId64 processTypeNameId) const;

	U32F GetCharactersFriendlyToFaction(const FactionId factionId, const Character* apFriend[], U32F capacity) const;

	// Now that horses are characters it is NOT true that all characters always update in the character bucket
	void SetupLegIkCollision(U32F bucket);
	void CollectLegIkCollision(U32F bucket);

	struct Observer : public LinkedNode<Observer>
	{
		virtual void CharacterRegistered(Character* pChar) {}
		virtual void CharacterUnRegistered(Character* pChar) {}
	};

	void RegisterObserver(Observer* pObserver);
	void UnRegisterObserver(Observer* pObserver);

private:
	U32F GetBucketForCharacter(const Character& character) const;

	mutable NdRwAtomicLock64_Jls m_accessLock;

	MutableCharacterHandle*	m_characterList;
	U32					m_maxCharacterCount;
	I32					m_characterCount;

	Observer			m_observerList;
};

extern I32 g_cheapNpcCount;
