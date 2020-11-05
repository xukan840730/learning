/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

#include "corelib/memory/relocate.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionController::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	m_pCharacter  = pCharacter;
	m_pNavControl = pNavControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	m_pCharacter  = pNavChar;
	m_pNavControl = pNavControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionController::Shutdown()
{
	m_pCharacter  = nullptr;
	m_pNavControl = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionController::Interrupt()
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pCharacter, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pNavControl, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCharacter* AnimActionController::GetCharacter() const
{
	AI_ASSERT(m_pCharacter);
	return NavCharacter::FromProcess(m_pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleNavCharacter* AnimActionController::GetSimpleNavCharacter() const
{
	AI_ASSERT(m_pCharacter);
	return SimpleNavCharacter::FromProcess(m_pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* AnimActionController::GetCharacterGameObject() const
{
	AI_ASSERT(m_pCharacter);
	return m_pCharacter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCharacterAdapter AnimActionController::GetCharacterAdapter() const
{
	return NavCharacterAdapter::FromProcess(m_pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleNavControl* AnimActionController::GetSimpleNavControl() const
{
	AI_ASSERT(m_pNavControl);
	return m_pNavControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavControl* AnimActionController::GetNavControl() const
{
	AI_ASSERT(m_pNavControl);
	return (m_pNavControl && !m_pNavControl->IsSimple()) ? reinterpret_cast<const NavControl*>(m_pNavControl) : nullptr;
}
