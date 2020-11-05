/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <orbisanim/structs.h>

class IJointModifier;
class JointModifierData;
class JointSet;
class JointTree;
class NdGameObject;

enum JointModifierType
{
	kEyeModifier,
	kLegModifier,
	kWeaponGripModifier,
	kArmModifier,
	kStrideModifier,
	kNetStrafe,
	kWeaponModModifier,
	kJointOverrideModifier,
	kGroundNormalIk,
	kClimbModifier,

	kJointModifierTypeCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JointModifiers
{
public:

	JointModifiers();

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void Init(NdGameObject* pOwner);
	void Destroy();

	void PreAnimBlending();
	void PostAnimBlending();
	void PostJointUpdate();
	void DebugDraw() const;

	void OnTeleport();

	void RegisterModifier(JointModifierType type, IJointModifier* pModifier);

	IJointModifier* GetModifier(JointModifierType type);
	const IJointModifier* GetModifier(JointModifierType type) const;

	template <class T>
	T* GetModifier(JointModifierType type)	{ return static_cast<T*>(GetModifier(type)); }

	template <class T>
	const T* GetModifier(JointModifierType type) const	{ return static_cast<const T*>(GetModifier(type)); }

	JointModifierData* GetJointModifierData()				{ return m_pJointModifierData; }
	const JointModifierData* GetJointModifierData() const	{ return m_pJointModifierData; }

	JointTree* GetJointTree()				{ return m_pJointTree; }
	const JointTree* GetJointTree() const	{ return m_pJointTree; }

	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace);

private:

	// bump up to 256 for complex bow.
	static CONST_EXPR size_t kMaxEndEffectors = 256; // for weapon-modifier, we need joint-set to include all joints

	IJointModifier* m_modifierList[kJointModifierTypeCount];

	// Moderate hack as this is shared data for all joint modifiers
	JointModifierData* m_pJointModifierData;
	JointTree* m_pJointTree;
	StringId64 m_endEffectors[kMaxEndEffectors];
	U32 m_numEndEffectors;
};

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status JointModifierAnimPluginCallback(OrbisAnim::SegmentContext* pSegmentContext,
												  JointModifierData* pData,
												  const Transform* pObjXform,
												  U16 ikType,
												  JointSet* pPluginJointSet);
