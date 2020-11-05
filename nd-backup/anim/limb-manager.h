/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef LIMB_MANAGER_H
#define LIMB_MANAGER_H

/* LIMB MANAGER
 *
 * The limb manager gives out permissions to control one or more of the character's limbs.
 *
 * It decides which subsystem takes priority over another and will deny the permission to the lower-priority subsystem.
 *
 * For example, if wall touch is using the right arm and then we need to flinch, the wall touch will have its right arm permission
 * revoked, and will need to fade itself out immediately.
 *
 * The subsystem first GETS the lock, and then must continue to HOLD the lock each frame.
 * If the subsystem fails to HOLD the lock, it has lost permission to control the limb and must fade out.
 */

class NdGameObject;

typedef U32 LimbLockBits;
enum
{
	kLockArmL = 0x1,
	kLockArmR = 0x2,

	kNumLimbLockBits = 2,
};

LimbLockBits FlipLimbLockBits(const LimbLockBits bits);

struct LimbLock
{
	I32 m_lockIndex = -1;
	U32 m_lockCounter = 0;
	LimbLockBits m_limbs = static_cast<LimbLockBits>(0x0);

	bool IsValid() const { return m_lockIndex >= 0; }

	bool operator==(const LimbLock& other) const;
};

typedef I32 LimbLockPriority;

struct LimbLockRequest
{
	LimbLockBits m_limbs;

	StringId64 m_subsystem;

	static const U32 kNumSubsystems = 3;

	// If this request names a subsystem in this list, it will stomp any locks held by that subsystem regardless of those locks' priorities.
	StringId64 m_subsystemsToOverrule[kNumSubsystems];

	// If two requests name each other's subsystems in this list, their locks on the same limbs hold simultaneously.
	StringId64 m_subsystemsToCoexistWith[kNumSubsystems];

	bool m_dryRun; // if true, don't actually get the lock

	LimbLockRequest()
		: m_limbs(0)
		, m_subsystem(INVALID_STRING_ID_64)
		, m_dryRun(false)
	{
		memset(m_subsystemsToOverrule, 0, sizeof(m_subsystemsToOverrule));
		memset(m_subsystemsToCoexistWith, 0, sizeof(m_subsystemsToCoexistWith));
	}
};

struct LimbLockDebugInfo
{
	StringId64 m_overrulingSubsystem = INVALID_STRING_ID_64;
};

struct LimbManagerConfig
{
	virtual LimbLockPriority GetPriorityFor(const LimbLockRequest& request) const = 0;

	virtual ~LimbManagerConfig() {}
};

class ILimbManager
{
public:
	virtual void Init(LimbManagerConfig* pConfig) = 0;

	virtual void Relocate(const ptrdiff_t offset_bytes, const uintptr_t lowerBound, const uintptr_t upperBound) = 0;

	virtual bool WouldGetLock(const LimbLockRequest& request, LimbLockDebugInfo* pOutDebugInfo = nullptr) = 0;

	virtual LimbLock GetLock(const LimbLockRequest& request, LimbLockDebugInfo* pOutDebugInfo = nullptr) = 0;

	virtual bool HoldLock(const LimbLock& lock, LimbLockDebugInfo* pOutDebugInfo = nullptr) = 0;

	// It's not necessary to call ReleaseLock. Only call if you really want to give up your lock immediately.
	virtual void ReleaseLock(const LimbLock& lock) = 0;

	virtual void DebugDraw(const NdGameObject& owner) = 0;

	// used for unit tests only
	virtual ptrdiff_t GetSize() const = 0;

	virtual ~ILimbManager() {}
};

ILimbManager* CreateLimbManager();

class ArtItemAnim;
LimbLockBits GetLimbLockBitsForAnim(const ArtItemAnim* pAnim, const NdGameObject* pObject);

#endif
