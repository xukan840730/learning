/*
* Copyright (c) 2009 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef _ND_MULTI_ATTACHABLE_H_
#define _ND_MULTI_ATTACHABLE_H_

#include "gamelib/gameplay/nd-attachable-object.h"

class NdGameObject;
class ProcessSpawnInfo;
class JointTree;

PROCESS_DECLARE(NdMultiAttachable);
class NdMultiAttachable : public NdAttachableObject
{
	typedef NdAttachableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(NdMultiAttachable);

	NdMultiAttachable();
	virtual ~NdMultiAttachable() override;
	virtual Err	Init(const ProcessSpawnInfo& info) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void PostAnimBlending_Async() override;
	virtual void EventHandler(Event& event) override;

	virtual U32 GetParentJointSegmentMask() {return m_parentJointSegmentMask;}

	// only copies pointer. Fine for now since all offsets are pointers to DC data
	// which does not change or relocate. If we want to support data with a different
	// lifetime we will have to allocate the offsets in this class.
	void SetJointOffsets(const DC::HashTable* pJointOffsets, bool applyImmediately = false);

protected:
	virtual void CopyJointsFromParent();
	bool BuildSourceJointIndexTable(const NdGameObject* pSourceObject, const char* suffix);
	void ApplyGoreHeadPropOffsets();

	I16* m_aSourceJointIndex;
	I16* m_aSourceOutputControlIndex;
	U32 m_parentJointSegmentMask;

	JointTree* m_pJointTree;

	bool m_forceCopyParentJoints	: 1;
	bool m_isGoreHeadProp			: 1;

	// data for applying offsets for each joint
	DC::HashTable const * m_pJointOffsets;
	//I32 m_numOffsets; // Sids and Locators are matched by the index into their array
	//const StringId64* m_pOffsetJointIds; // the sid of the joints that are offset
	//const Locator* m_pJointOffsets;  // the Locator offsets for each joint sid


};

#endif // _ND_MULTI_ATTACHABLE_H_
