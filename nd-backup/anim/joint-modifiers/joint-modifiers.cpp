/*
* Copyright (c) 2003 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/anim/joint-modifiers/joint-modifiers.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/joint-modifiers/joint-modifier.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
JointModifiers::JointModifiers()
{
	memset(&m_modifierList[0], 0, sizeof(IJointModifier*) * kJointModifierTypeCount);
	m_pJointModifierData = NDI_NEW (kAlign16) JointModifierData;

	memset(m_pJointModifierData, 0, sizeof(JointModifierData));

	m_pJointTree = nullptr;
	m_numEndEffectors = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::Init(NdGameObject* pOwner)
{
	StringId64 newEffectors[kMaxEndEffectors];

	m_numEndEffectors = 0;

	{
		const U32F numAdded = pOwner->CollectEndEffectorsForPluginJointSet(newEffectors, kMaxEndEffectors);

		for (U32F j = 0; (j < numAdded) && (m_numEndEffectors < kMaxEndEffectors); ++j)
		{
			bool alreadyAdded = false;

			for (U32F iEffector = 0; iEffector < m_numEndEffectors; ++iEffector)
			{
				if (m_endEffectors[iEffector] == newEffectors[j])
				{
					alreadyAdded = true;
					break;
				}
			}

			if (!alreadyAdded)
			{
				m_endEffectors[m_numEndEffectors] = newEffectors[j];
				++m_numEndEffectors;
			}
		}
	}

	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pModifier = m_modifierList[i];

		if (!pModifier)
			continue;

		pModifier->Init();

		const U32F numAdded = pModifier->CollectRequiredEndEffectors(newEffectors, kMaxEndEffectors);

		for (U32F j = 0; (j < numAdded) && (m_numEndEffectors < kMaxEndEffectors); ++j)
		{
			bool alreadyAdded = false;

			for (U32F iEffector = 0; iEffector < m_numEndEffectors; ++iEffector)
			{
				if (m_endEffectors[iEffector] == newEffectors[j])
				{
					alreadyAdded = true;
					break;
				}
			}

			if (!alreadyAdded)
			{
				m_endEffectors[m_numEndEffectors] = newEffectors[j];
				++m_numEndEffectors;
			}
		}
	}

	if (m_numEndEffectors > 0)
	{
		m_pJointTree = NDI_NEW JointTree;
		const bool success = m_pJointTree->Init(pOwner, INVALID_STRING_ID_64, false, m_numEndEffectors, m_endEffectors);

		FgAnimData* pAnimData = pOwner->GetAnimData();
		if (success && pAnimData)
		{
			pAnimData->m_pPluginJointSet = m_pJointTree;
		}
	}

	for (U32F i = 0; i < kJointModifierTypeCount; i++)
	{
		IJointModifier *pModifier = m_modifierList[i];
		if (pModifier == nullptr)
			continue;

		pModifier->PostInit();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::Destroy()
{
	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		if (m_modifierList[i])
		{
			m_modifierList[i]->Shutdown();
			delete m_modifierList[i];
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::RegisterModifier(JointModifierType type, IJointModifier* pModifier)
{
	if (type < kJointModifierTypeCount)
	{
		ANIM_ASSERT(m_modifierList[type] == nullptr);
		m_modifierList[type] = pModifier;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
IJointModifier* JointModifiers::GetModifier(JointModifierType type)
{
	ANIM_ASSERT(type < kJointModifierTypeCount);
	return m_modifierList[type];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IJointModifier* JointModifiers::GetModifier(JointModifierType type) const
{
	ANIM_ASSERT(type < kJointModifierTypeCount);
	return m_modifierList[type];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		RelocateObject(m_modifierList[i], deltaPos, lowerBound, upperBound);
		RelocatePointer(m_modifierList[i], deltaPos, lowerBound, upperBound);
	}

	DeepRelocatePointer(m_pJointModifierData, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pJointTree, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::PreAnimBlending()
{
	PROFILE(AI, JointModifiers_PreAnimBlending);

	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pJm = m_modifierList[i];
		if (!pJm)
			continue;

		pJm->PreAnimBlending();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::PostAnimBlending()
{
	PROFILE(AI, JointModifiers_PostAnimBlending);

	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pJm = m_modifierList[i];
		if (!pJm)
			continue;

		pJm->PostAnimBlending();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::PostJointUpdate()
{
	PROFILE(AI, JointModifiers_PostJointUpdate);

	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pJm = m_modifierList[i];
		if (!pJm)
			continue;

		pJm->PostJointUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier *const pJointModifier = m_modifierList[i];
		if (pJointModifier)
		{
			// For debugging purposes I want to call DebugDraw() even when the modifier is disabled, then 
			// it is the modifier's responsibility on what to draw in this case.
			pJointModifier->DebugDraw();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::OnTeleport()
{
	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pJm = m_modifierList[i];
		if (!pJm)
			continue;

		pJm->OnTeleport();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointModifiers::EnterNewParentSpace(const Transform& matOldToNew,
										 const Locator& oldParentSpace,
										 const Locator& newParentSpace)
{
	for (U32F i = 0; i < kJointModifierTypeCount; ++i)
	{
		IJointModifier* pJm = m_modifierList[i];
		if (!pJm)
			continue;

		pJm->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}
}
