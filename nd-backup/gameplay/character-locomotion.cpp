/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/character-locomotion.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER_ABSTRACT(ICharacterLocomotion, NdSubsystemAnimController);
TYPE_FACTORY_REGISTER_ABSTRACT(CharacterLocomotionInterface, NdSubsystem);

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
ICharacterLocomotion::LocomotionState ICharacterLocomotion::CreateLocomotionState(const NdGameObject* pOwner)
{
	LocomotionState s;

	s.m_alignPs = kIdentity;
	s.m_time = Seconds(0.0f);
	s.m_velPs = kZero;

	if (pOwner)
	{
		s.m_time = pOwner->GetClock()->GetCurTime();

		if (pOwner->IsKindOf(SID("PlayerBase")))
		{
			Locator alignWs = pOwner->GetLocator();
			Quat rotWs = pOwner->GetRotation();
			rotWs.SetX(0.0f);
			rotWs.SetZ(0.0f);
			rotWs = Normalize(rotWs);
			alignWs.SetRot(rotWs);

			s.m_alignPs = pOwner->GetParentSpace().UntransformLocator(alignWs);
			s.m_velPs = pOwner->GetBaseVelocityPs();
		}
		else
		{
			s.m_alignPs = pOwner->GetLocatorPs();
			s.m_velPs = pOwner->GetVelocityPs();
		}

		if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pOwner))
		{
			const Locator& prevLocPs = pNavChar->GetPrevLocatorPs();

			const Vector prevDirPs = GetLocalZ(prevLocPs);
			const Vector curDirPs = GetLocalZ(s.m_alignPs);

			const Angle prevAngle = Angle::FromRadians(Atan2(prevDirPs.X(), prevDirPs.Z()));
			const Angle nextAngle = Angle::FromRadians(Atan2(curDirPs.X(), curDirPs.Z()));
			const float diffRad = DEGREES_TO_RADIANS(nextAngle.AngleDiff(prevAngle));

			const float dt = pOwner->GetClock()->GetDeltaTimeInSeconds();

			if (dt > NDI_FLT_EPSILON)
			{
				s.m_yawSpeed = diffRad / dt;
			}
			else
			{
				s.m_yawSpeed = 0.0f;
			}
		}
		else
		{
			const Vector angularVelWs = pOwner->GetAngularVelocityWs();
			const Vector upWs = GetLocalY(pOwner->GetLocator());

			s.m_yawSpeed = Length(angularVelWs) * Sign(Dot(angularVelWs, upWs));

			//g_prim.Draw(DebugArrow(pOwner->GetTranslation(), angularVelWs, kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha));
			//MsgCon("[%s] %0.3frad/s\n", pOwner->GetName(), s.m_yawSpeed);
		}
	}

	return s;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err CharacterLocomotionInterface::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	return Err::kOK;
}
