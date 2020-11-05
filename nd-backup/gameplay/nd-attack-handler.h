/*
 * Copyright (c)2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDLIB_ATTACK_HANDLER_H
#define NDLIB_ATTACK_HANDLER_H

class Event;
class NdGameObject;
class ProcessSpawnInfo;
class RigidBody;
struct HavokImpulseData;
struct NdAttackInfo;
struct ReceiverDamageInfo; // to NDLIB this is an opaque data type -- the game must define it

class NdAttackHandler
{
public:
	NdAttackHandler();
	virtual ~NdAttackHandler();

	virtual Err PostInit(NdGameObject& self, const ProcessSpawnInfo& spawn) = 0;
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	virtual void EventHandler(NdGameObject& self, Event& event) = 0;
	virtual void OnKillProcess(NdGameObject& self) = 0;

	virtual void GetReceiverDamageInfo(const NdGameObject& self, ReceiverDamageInfo* pInfo) const = 0;
	virtual StringId64 GetDamageReceiverClass(const NdGameObject& self, const RigidBody* pBody = nullptr) const = 0;

	virtual void PhysicsResponseToAttack(NdGameObject& self, const NdAttackInfo* pAttackInfo, HavokImpulseData* pExtraImpulseData = nullptr) = 0;
};

#endif	// NDLIB_ATTACK_HANDLER_H
