/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/process/bound-frame.h"
#include "ndlib/process/process.h"

#include "gamelib/gameplay/nav/nav-location.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class EntitySpawner;
class NdGameObject;
class ProcessSpawnInfo;
class RigidBody;

/// --------------------------------------------------------------------------------------------------------------- ///
class NdLocatableObject : public Process
{
private:
	typedef Process ParentClass;

public:
	FROM_PROCESS_DECLARE(NdLocatableObject);

	NdLocatableObject();
	virtual ~NdLocatableObject() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void InitLocator(const ProcessSpawnInfo& spawn);
	virtual U32F GetMaxStateAllocSize() override;

	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Positioning and Orientation
	/// --------------------------------------------------------------------------------------------------------------- ///
	const BoundFrame& GetBoundFrame() const { return m_boundFrame; }
	const Locator& GetParentSpace() const { return m_boundFrame.GetParentSpace(); }
	const Locator GetLocator() const { return m_boundFrame.GetLocator(); }
	const Locator& GetLocatorPs() const { return m_boundFrame.GetLocatorPs(); }
	const Point GetTranslation() const { return m_boundFrame.GetTranslation(); }
	const Point GetTranslationPs() const { return m_boundFrame.GetLocatorPs().Pos(); }
	const Quat GetRotation() const { return m_boundFrame.GetRotation(); }
	const Quat GetRotationPs() const { return m_boundFrame.GetLocatorPs().Rot(); }

	void SetBoundFrame(const BoundFrame& frame);
	void SetLocator(const Locator& loc);
	void SetLocatorPs(const Locator& loc);
	void SetTranslation(Point_arg trans);
	void SetTranslationPs(Point_arg pt);
	void AdjustTranslation(Vector_arg move);
	void SetRotation(Quat_arg rot);
	void SetRotationPs(Quat_arg rot);
	virtual void OnScriptSetLocator() {}

	virtual NavLocation GetNavLocation() const
	{
		NavLocation ret;
		ret.SetWs(GetTranslation());
		return ret;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Spawner
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual StringId64 GetSpawnerId() const;
	virtual const EntitySpawner* GetSpawner() const override { return m_pSpawner; }
	virtual void SetSpawner(const EntitySpawner* pSpawner) override { m_pSpawner = pSpawner; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Binding (BoundFrame Attachments)
	/// --------------------------------------------------------------------------------------------------------------- ///

	/// Returns the spawner id of the parent object to which this object is CURRENTLY bound.
	/// (except at certain times while this object is spawning)
	StringId64 GetBindSpawnerId() const { return m_bindSpawnerNameId; }

	/// Binds this object to the given target.
	// DB: This is deprecated, but not yet marked as so because SetBinding() actually uses it
	// internally at the moment!
	virtual void BindToRigidBody(const RigidBody* pRigidBody);

	/// Returns the target to which this object is bound.  (Same as GetBoundGameObject()->GetDefaultBindTarget()
	/// when the object is bound to the parent's default target.)
	const RigidBody* GetBoundRigidBody() const;
	RigidBody* GetBoundRigidBody();

	/// Returns the object to which this object is bound.
	NdGameObject* GetBoundGameObject() const;

	/// Returns true if this object is bound to the given target's coordinate space.
	bool IsSameBindSpace(const RigidBody* pBindTarget) const;

	/// Binds this object to the given binding.
	virtual void SetBinding(const Binding& binding);

	/// Returns the binding that this object currently has.  This could be a process and
	/// a joint or a rigid body or whatever else you may be able to bind to.
	Binding GetBinding() const; // New hotness

	virtual void AssociateWithLevel(const Level* pLevel) override;
	virtual bool SpawnerAssociateWithLevel() const override { return m_pSpawner != nullptr; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Camera
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void UpdateFocusTargetTravelDist(float& travelDist, BoundFrame& lastFrameFocusBf) const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Debugging
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const;
	virtual bool AllowDebugRotate() const { return false; }

protected:
	virtual void OnRemoveParentRigidBody(RigidBody* pOldParentBody, const RigidBody* pNewParentBody);
	virtual void OnAddParentRigidBody(const RigidBody* pOldParentBody,
									  Locator oldParentSpace,
									  const RigidBody* pNewParentBody);
	virtual void OnLocationUpdated() {}

	BoundFrame						m_boundFrame;
	const EntitySpawner*			m_pSpawner;
	StringId64						m_bindSpawnerNameId;

	PROCESS_IS_RAW_TYPE_DEFINE(NdLocatableObject);
};

PROCESS_DECLARE(NdLocatableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
class NdLocatableSnapshot : public ProcessSnapshot
{
public:
	PROCESS_SNAPSHOT_DECLARE(NdLocatableSnapshot, ProcessSnapshot);

	explicit NdLocatableSnapshot(const Process* pOwner, const StringId64 typeId)
		: ParentClass(pOwner, typeId)
		, m_boundFrame(kIdentity)
		, m_pSpawner(nullptr)
		, m_bindSpawnerNameId(INVALID_STRING_ID_64)
	{
	}

	const EntitySpawner* GetSpawner() const { return m_pSpawner; }

	const BoundFrame& GetBoundFrame() const		{ return m_boundFrame; }
	const NavLocation& GetNavLocation() const	{ return m_navLocation; }

	const Locator&	GetParentSpace() const		{ return m_boundFrame.GetParentSpace(); }
	const Locator	GetLocator() const			{ return m_boundFrame.GetLocator(); }
	const Locator&	GetLocatorPs() const		{ return m_boundFrame.GetLocatorPs(); }
	const Point		GetTranslation() const		{ return m_boundFrame.GetTranslation(); }
	const Point		GetTranslationPs() const	{ return m_boundFrame.GetLocatorPs().Pos(); }
	const Quat		GetRotation() const			{ return m_boundFrame.GetRotation(); }
	const Quat		GetRotationPs() const		{ return m_boundFrame.GetLocatorPs().Rot(); }

	NavLocation				m_navLocation;
	BoundFrame				m_boundFrame;
	const EntitySpawner*	m_pSpawner;
	StringId64				m_bindSpawnerNameId;
};
