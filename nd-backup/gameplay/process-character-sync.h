/*
* Copyright (c) 2012 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef PROCESS_CHARACTER_SYNC_H
#define PROCESS_CHARACTER_SYNC_H

#include "corelib/system/recursive-atomic-lock.h"
#include "ndlib/process/process.h"
#include "gamelib/gameplay/character.h"

enum CharacterSyncRemovedReason
{
	kCharacterSyncEnd,		// Character is ending this synced action, probably going to a new one
	kCharacterSyncExit,		// Character is exiting the synced action state entirely
	kCharacterSyncAbort		// Character has been aborted from this synced action
};

class ProcessCharacterSync : public Process
{
public:
	ProcessCharacterSync();
	virtual Err Init(const ProcessSpawnInfo &info) override;
	virtual Err PostInit(const ProcessSpawnInfo& info) override;

	void AddCharacter(const NdGameObject* pChar);
	void RemoveCharacter(NdGameObject* pChar, CharacterSyncRemovedReason reason, StringId64 reasonString = INVALID_STRING_ID_64);
	U32F GetNumCharacters() const { return m_numChars; }
	const NdGameObject* GetCharacter(U32 i) const { return m_hChars[i].ToProcess(); }

	virtual void BucketEventNotify(U32F bucket, U32F updateType) override;

	virtual U32						GetNumSyncCharacters();
	virtual NdGameObject*				GetSyncCharacters(int index);

	STATE_DECLARE_OVERRIDE(Active);

protected:
	typedef Process ParentClass;

	// Called when all characters' animation systems get updated
	// These only happen during the Character bucket update
	virtual void PreAnimUpdate() { }		// All processes in the Character bucket have been updated (including us!).
	virtual void PostAnimUpdate() { }		// All objects have been animated.
	virtual void PostAnimBlending() { }		// All animations have been blended, objects are at their final locations in the world. Do IK here.
	virtual void PostJointUpdate() { }		// All objects' joint caches have been updated with final world and local space positions.

	// Callbacks for derived classes
	virtual void CharacterAdded(const NdGameObject* pChar) { }
	virtual void CharacterRemoved(NdGameObject* pChar, CharacterSyncRemovedReason reason, StringId64 reasonString = INVALID_STRING_ID_64) { }

	virtual void DebugPrintInfo() {}
private:

	I32F GetCharacterIndex(const NdGameObject* pChar) const;
	bool CharacterExists(const NdGameObject* pChar) const { return GetCharacterIndex(pChar) >= 0; }

	U32 m_numChars;
	static const U32 kMaxCharacters = 8;
	MutableNdGameObjectHandle m_hChars[kMaxCharacters];
	U32 m_numRemovedChars;
	MutableNdGameObjectHandle m_hRemovedChars[kMaxCharacters];
	NdRecursiveAtomicLock64 m_lock;
protected:
	U32F m_bucketEventMask;
};

class ProcessCharacterSync::Active : public Process::State
{
	typedef Process::State ParentClass;
	BIND_TO_PROCESS(ProcessCharacterSync);

public:

	virtual void Update() override;
};

#endif //PROCESS_CHARACTER_SYNC_H
