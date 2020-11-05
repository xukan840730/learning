/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"

#include "corelib/memory/relocate.h"

#include "ndlib/util/bitarray128.h"

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NdAnimationControllers::NdAnimationControllers(U32 numControllers)
{
	m_controllerList = nullptr;
	m_numControllers = 0;

	m_numControllers = numControllers;
	m_controllerList = NDI_NEW AnimActionController*[m_numControllers];
	memset(m_controllerList, 0, sizeof(AnimActionController*) * m_numControllers);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAnimationControllers::~NdAnimationControllers()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			NDI_DELETE m_controllerList[i];
			m_controllerList[i] = nullptr;
		}
	}

	NDI_DELETE[] m_controllerList;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->Init(pNavChar, pNavControl);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->Init(pCharacter, pNavControl);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::Destroy()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->Shutdown();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::Reset()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->Reset();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::ResetExcluding(const BitArray128& bitArrayExclude)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (bitArrayExclude.GetBit(i))
			continue;

		if (AnimActionController* pController = m_controllerList[i])
		{
			pController->Reset();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::InterruptNavControllers(const INavAnimController* pIgnore /* = nullptr */)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		AnimActionController* pAnimActionController = m_controllerList[i];
		if (!pAnimActionController || !pAnimActionController->IsNavAnimController())
			continue;

		if (pAnimActionController == pIgnore)
			continue;

		INavAnimController* pNavAnimController = (INavAnimController*)pAnimActionController;
		pNavAnimController->Interrupt();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::ResetNavigation(const INavAnimController* pNewActiveNavAnimController, bool playIdle/* = true*/)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		AnimActionController* pAnimActionController = m_controllerList[i];
		if (!pAnimActionController)
			continue;

		if (pAnimActionController->IsNavAnimController())
		{
			INavAnimController* pNavAnimController = (INavAnimController*)pAnimActionController;

			if (pNavAnimController == pNewActiveNavAnimController)
			{
				pNavAnimController->ForceDefaultIdleState(playIdle);
			}
			else
			{
				pNavAnimController->Interrupt();
			}
		}
		else
		{
			pAnimActionController->Reset();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		DeepRelocatePointer(m_controllerList[i], deltaPos, lowerBound, upperBound);
	}

	RelocatePointer(m_controllerList, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::RequestAnimations()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->RequestAnimations();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::UpdateStatus()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->UpdateStatus();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::UpdateProcedural()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->UpdateProcedural();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::PostRenderUpdate()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->PostRenderUpdate();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->DebugDraw(pPrinter);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimationControllers::IsBusy() const
{
	BitArray128 controllerBits;
	controllerBits.SetAllBits();

	return IsBusy(controllerBits);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimationControllers::IsBusy(const BitArray128& bitArrayInclude) const
{
	for (U32F controllerNum = 0; controllerNum < m_numControllers; ++controllerNum)
	{
		if (!bitArrayInclude.GetBit(controllerNum))
			continue;

		if (const AnimActionController* pControl = m_controllerList[controllerNum])
		{
			if (pControl->IsBusy())
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimationControllers::IsBusyExcluding(const BitArray128& bitArrayExclude) const
{
	for (U32F controllerNum = 0; controllerNum < m_numControllers; ++controllerNum)
	{
		if (bitArrayExclude.GetBit(controllerNum))
			continue;

		if (const AnimActionController* pControl = m_controllerList[controllerNum])
		{
			if (pControl->IsBusy())
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdAnimationControllers::GetIsBusyForEach(BitArray128& bitArrayOut) const
{
	ANIM_ASSERT(m_numControllers <= 128);

	bitArrayOut.Clear();
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		bool isBusy = false;
		if (const AnimActionController* pControl = m_controllerList[i])
		{
			if (pControl->IsBusy())
			{
				bitArrayOut.SetBit(i);
			}
		}
	}

	return m_numControllers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::SetController(U32F index, AnimActionController* pController)
{
	if (index < m_numControllers)
	{
		ANIM_ASSERT(m_controllerList[index] == nullptr);
		m_controllerList[index] = pController;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackController* NdAnimationControllers::GetControllerForActionPack(const ActionPack* pAp) const
{
	const ActionPackController* pApCtrl = nullptr;
	if (pAp)
	{
		pApCtrl = GetControllerForActionPackType(pAp->GetType());
	}
	return pApCtrl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackController* NdAnimationControllers::GetControllerForActionPack(const ActionPack* pAp)
{
	ActionPackController* pApCtrl = nullptr;
	if (pAp)
	{
		pApCtrl = GetControllerForActionPackType(pAp->GetType());
	}
	return pApCtrl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 NdAnimationControllers::CollectHitReactionStateFlags() const
{
	U64 flags = 0;

	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			const U64 thisFlags = m_controllerList[i]->CollectHitReactionStateFlags();
			flags |= thisFlags;
		}
	}

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::PostRootLocatorUpdate()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->PostRootLocatorUpdate();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::OnHitReactionPlayed()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->OnHitReactionPlayed();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::ConfigureCharacter(Demeanor demeanor,
												const DC::NpcDemeanorDef* pDemeanorDef,
												const NdAiAnimationConfig* pAnimConfig)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->ConfigureCharacter(demeanor, pDemeanorDef, pAnimConfig);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::EnterNewParentSpace(const Transform& matOldToNew,
												 const Locator& oldParentSpace,
												 const Locator& newParentSpace)
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimationControllers::GetNavAnimControllerIndices(BitArray128& bits) const
{
	bits.Clear();

	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i] && m_controllerList[i]->IsNavAnimController())
		{
			bits.SetBit(i);
		}
	}
}
