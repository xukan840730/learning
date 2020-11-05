/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_OBJECT_H
#define FLOCKING_OBJECT_H

#include "gamelib/gameplay/nd-simple-object.h"

class ProcessSpawnInfo;
class Event;

namespace DC
{
	struct FlockingVars;
	struct AnimalBehaviorSettings;
}

class FlockingInterface
{
public:
	virtual void AddBoid(NdGameObject *pBoid, StringId64 groupOrSplineId, bool isSpline) = 0;
	virtual void RemoveBoid(NdGameObject *pBoid) = 0;
	virtual void UpdatePosOnSpline(const NdGameObject* pBoid, Point_arg newPos, Vector_arg newVel) = 0;
	virtual Vector GetAccelForPosition(const NdGameObject *self, Point_arg pos, Vector_arg vel, const DC::FlockingVars *pVars, const DC::AnimalBehaviorSettings* pAnimalSettings) = 0;
	virtual Vector CalcTakeoffDirection(const NdGameObject* pSelf, Point_arg selfPos) const = 0;
};

//-----------------------------------------------------------------------//
// Flocking Controller
//-----------------------------------------------------------------------//
class FlockingCtrl
{
protected:
	Vector m_velocity;
	Vector m_accel;
	bool m_flocking;
	ScriptPointer<DC::FlockingVars> m_flockingVars;

public:
	bool Init(StringId64 settingsId);
	void CalculateAccelAndVelocity(NdGameObject* pSelf, const DC::AnimalBehaviorSettings* pAnimalSettings = nullptr);
	Vector CalcTakeoffDirection(const NdGameObject* pSelf, Point_arg selfPos) const;
	void UpdatePosOnSpline(const NdGameObject* pSelf, Point_arg newPos, Vector_arg newVel);

	Vector GetVelocity() const { return m_velocity; }	
	Vector GetAccel() const { return m_accel; }
	void SetVelocity(Vector_arg vel) { ALWAYS_ASSERT(IsFinite(vel)); m_velocity = vel; }
	void SetAccel(Vector_arg accel) { ALWAYS_ASSERT(IsFinite(accel)); m_accel = accel; }

	float GetMaxSpeed() const;

	void Enable() { m_flocking = true; }
	void Disable() { m_flocking = false; }
	bool IsEnabled() const { return m_flocking; }
};

class FlockingObject : public NdSimpleObject
{
	typedef NdSimpleObject ParentClass;

public:
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void OnKillProcess() override;
	virtual void EventHandler(Event& event) override;
	virtual void PostAnimUpdate_Async() override;

protected:	
	FlockingCtrl m_flockingCtrl;

	virtual void StartFlocking();
	void UpdatePosOnSpline(Point_arg newPos, Vector_arg newVel);
	void UpdateFlocking();
};

void AddFlockingBoid(NdGameObject *pBoid, StringId64 refPoint);
void RemoveFlockingBoid(NdGameObject *pBoid);
Vector GetBoidAccel(NdGameObject *pBoid);
void RegisterRepulsor(Process* pRepulsor);
void UnregisterRepulsor(Process* pRepulsor);

#endif // FLOCKING_OBJECT_H


