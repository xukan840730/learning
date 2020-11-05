/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-motion-match-locomotion.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/scriptx/h/joypad-defines.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/anim/motion-matching/motion-matching-manager.h"
#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/character-motion-match-locomotion-debugger.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nd-player-joypad.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/logger/logger.h"
#include "gamelib/scriptx/h/anim-character-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(CharacterMotionMatchLocomotion, ICharacterLocomotion);
TYPE_FACTORY_REGISTER(CharacterMotionMatchLocomotionAction, NdSubsystemAnimAction);
TYPE_FACTORY_REGISTER(MotionMatchIkAdjustments, NdSubsystem);

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlendPair
{
	BlendPair(const DC::MotionMatchingSettings* pSettings, float def)
	{
		m_animFadeTime	 = pSettings ? pSettings->m_blendTimeSec : def;
		m_motionFadeTime = pSettings ? pSettings->m_motionBlendTimeSec : def;
		m_curve = DC::kAnimCurveTypeUniformS;

		if (m_motionFadeTime < 0.0f)
		{
			m_motionFadeTime = m_animFadeTime;
		}
	}

	BlendPair(float a, float m) : m_animFadeTime(a), m_motionFadeTime(m), m_curve(DC::kAnimCurveTypeUniformS) {}

	BlendPair(const DC::BlendParams& blend)
	{
		m_animFadeTime	 = blend.m_animFadeTime;
		m_motionFadeTime = blend.m_motionFadeTime;
		m_curve = blend.m_curve;
	}

	float m_animFadeTime;
	float m_motionFadeTime;
	DC::AnimCurveType m_curve;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionModelInputFunctor
{
public:
	MotionModelInputFunctor(const NdGameObject* pSelf,
							const ICharacterLocomotion::InputData& input,
							const MotionModelPathInput* pPathInput,
							const CharacterLocomotionInterface* pInterface,
							const MotionMatchingSet* pArtItemSet,
							const DC::MotionMatchingSettings* pSettings,
							bool debug)
		: m_pOwner(Character::FromProcess(pSelf))
		, m_pInput(&input)
		, m_pPathInput(pPathInput)
		, m_pInterface(pInterface)
		, m_pArtItemSet(pArtItemSet)
		, m_pSettings(pSettings)
		, m_debug(debug)
	{
		m_navLoc = pSelf->GetNavLocation();
		
		m_obeyedStaticBlockage = Nav::kStaticBlockageMaskNone;
		m_obeyedBlockers.ClearAllBits();

		const NavCharacter* pNavChar = NavCharacter::FromProcess(pSelf);
		const NavControl* pNavCon	 = pNavChar ? pNavChar->GetNavControl() : nullptr;
		if (pNavCon)
		{
			m_obeyedBlockers	   = pNavCon->GetCachedObeyedNavBlockers();
			m_obeyedStaticBlockage = pNavCon->GetObeyedStaticBlockers();
		}
	}

	MotionModelInput operator()(const MotionModelIntegrator& integratorPs, TimeFrame delta, bool finalize)
	{
		MotionModelInput ret = MotionModelInput(m_pOwner->GetLocatorPs(),
												m_pInput->m_desiredVelocityDirPs,
												m_pInput->m_desiredFacingPs);

		if (m_pInput->m_pStickFunc)
		{
			ret = m_pInput->m_pStickFunc(m_pOwner,
										 m_pInput,
										 m_pArtItemSet,
										 m_pSettings,
										 integratorPs,
										 delta,
										 finalize,
										 m_debug);
		}
		else
		{
			ret.m_pPathInput = m_pPathInput;
		}

		ret.m_charLocPs		 = m_pOwner->GetLocatorPs();
		ret.m_groundNormalPs = m_pInput->m_groundNormalPs;
		ret.m_baseYawSpeed	 = m_pInput->m_baseYawSpeed;
		ret.m_charNavLoc	 = m_navLoc;
		ret.m_obeyedStaticBlockers = m_obeyedStaticBlockage;
		ret.m_obeyedBlockers	   = m_obeyedBlockers;
		ret.m_pInterface = m_pInterface;

		// NB: Explicitly *not* setting this to m_input.m_translationSkew because we don't want skewing
		// to affect animation selection logic (by way of making our trajectory shorter or longer)
		ret.m_translationSkew = 1.0f;

		return ret;
	}

private:
	const Character* m_pOwner;
	const MotionMatchingSet* m_pArtItemSet;
	const DC::MotionMatchingSettings* m_pSettings;
	const MotionModelPathInput* m_pPathInput;
	const CharacterLocomotionInterface* m_pInterface;
	const ICharacterLocomotion::InputData* m_pInput;
	NavLocation m_navLoc;
	NavBlockerBits m_obeyedBlockers;
	Nav::StaticBlockageMask m_obeyedStaticBlockage;
	bool m_debug;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static DC::MotionModelSettings LerpSettings(const DC::MotionModelSettings& a,
											const DC::MotionModelSettings& b,
											float tt);

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::MotionMatchTransitionEntry* LookupTransition(const DC::MotionMatchTransitionTable& table,
															  const StringId64 prevSetName,
															  const StringId64 nextSetName)
{
	for (const DC::MotionMatchTransitionEntry* pEntry : table.m_transitions)
	{
		if (pEntry->m_startMode == prevSetName && pEntry->m_endMode == nextSetName)
		{
			return pEntry;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BlendPair GetMotionMatchBlendSpeeds(const DC::MotionMatchTransitionTable* pTable,
										   const StringId64 prevSetName,
										   const StringId64 nextSetName,
										   const BlendPair& defaultBlendTime)
{
	if (!pTable)
	{
		return defaultBlendTime;
	}

	if (const DC::MotionMatchTransitionEntry* pEntry = LookupTransition(*pTable, prevSetName, nextSetName))
	{
		return BlendPair(pEntry->m_blend);
	}

	if (const DC::MotionMatchTransitionEntry* pEntry = LookupTransition(*pTable, SID("any"), nextSetName))
	{
		return BlendPair(pEntry->m_blend);
	}

	if (const DC::MotionMatchTransitionEntry* pEntry = LookupTransition(*pTable, prevSetName, SID("any")))
	{
		return BlendPair(pEntry->m_blend);
	}

	return defaultBlendTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::BlendParams* GetMotionMatchBlendSettings(const AnimStateInstance* pCurInstance,
														  const DC::MotionMatchingSettings* pSettings,
														  const StringId64 newAnimId64,
														  bool switchingSet)
{
	if (!pCurInstance || !pSettings)
		return nullptr;

	const StringId64 curAnimId = pCurInstance->GetPhaseAnim();
	if (curAnimId == INVALID_STRING_ID_64)
		return nullptr;

	return LookupAnimBlendTableEntry(pSettings->m_blendTable, curAnimId, newAnimId64, switchingSet);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Maybe<AnimSample> GetExtraDebugSample()
{
	Maybe<AnimSample> ret = MAYBE::kNothing;

	ArtItemAnimHandle hAnim = ResourceTable::LookupAnim(g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId,
														g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId);

	if (const ArtItemAnim* pAnim = hAnim.ToArtItem())
	{
		const float phase = GetClipPhaseForMayaFrame(pAnim->m_pClipData,
													 g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame);

		ret = AnimSample(hAnim, phase, g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimMirror);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class BlendedMotionModelSettingsSupplier : public IMotionModelSettingsSupplier
{
public:
	BlendedMotionModelSettingsSupplier(const MotionSettingsBlendQueue& queue, TimeFrame curTime)
		: m_pQueue(&queue), m_curTime(curTime)
	{
	}

	virtual DC::MotionModelSettings GetSettings(TimeFrame timeInFuture) const override
	{
		return m_pQueue->Get(m_curTime + timeInFuture);
	}

private:
	const MotionSettingsBlendQueue* m_pQueue = nullptr;
	TimeFrame m_curTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator GetFutureLocatorPs(const Locator& locPs,
								  const SkeletonId skelId,
								  Maybe<AnimSample>& maybeCurSample,
								  float dt)
{
	Locator futureLocPs = locPs;

	if (maybeCurSample.Valid() && (dt > 0.0f))
	{
		AnimSample curSample = maybeCurSample.Get();

		const ArtItemAnim* pAnim = curSample.Anim().ToArtItem();
		const bool mirroredSample = curSample.Mirror();

		const float prevSamplePhase = curSample.Phase();

		curSample.Advance(Seconds(dt));

		const float nextSamplePhase = curSample.Phase();

		if (Abs(nextSamplePhase - prevSamplePhase) < NDI_FLT_EPSILON)
		{
		}
		else if (nextSamplePhase > prevSamplePhase)
		{
			bool valid = true;
			Locator prevLocOs, nextLocOs;

			valid = valid && EvaluateChannelInAnim(skelId, pAnim, SID("align"), prevSamplePhase, &prevLocOs, mirroredSample);
			valid = valid && EvaluateChannelInAnim(skelId, pAnim, SID("align"), nextSamplePhase, &nextLocOs, mirroredSample);

			if (valid)
			{
				const Locator deltaLoc = prevLocOs.UntransformLocator(nextLocOs);
				//ANIM_ASSERT(Dist(deltaLoc.Pos(), kOrigin) < 2.0f);
				futureLocPs = futureLocPs.TransformLocator(deltaLoc);
			}
		}
		else
		{
			bool valid = true;
			Locator prevLocOs, endLocOs, nextLocOs;

			valid = valid && EvaluateChannelInAnim(skelId, pAnim, SID("align"), prevSamplePhase, &prevLocOs, mirroredSample);
			valid = valid && EvaluateChannelInAnim(skelId, pAnim, SID("align"), 1.0f, &endLocOs, mirroredSample);
			valid = valid && EvaluateChannelInAnim(skelId, pAnim, SID("align"), nextSamplePhase, &nextLocOs, mirroredSample);

			if (valid)
			{
				const Locator deltaLoc0 = prevLocOs.UntransformLocator(endLocOs);
				const Locator deltaLoc1 = nextLocOs;

				//ANIM_ASSERT(Dist(deltaLoc0.Pos(), kOrigin) < 2.0f);
				//ANIM_ASSERT(Dist(deltaLoc1.Pos(), kOrigin) < 2.0f);

				futureLocPs = futureLocPs.TransformLocator(deltaLoc0);
				futureLocPs = futureLocPs.TransformLocator(deltaLoc1);
			}
		}

		maybeCurSample = curSample;
	}

	return futureLocPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err CharacterMotionMatchLocomotionAction::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	NdSubsystemAnimController* pController = GetParentSubsystemController();

	if (!pController || (pController->GetType() != SID("CharacterMotionMatchLocomotion")))
	{
		MsgAnimErr("Trying to create a motion matching action for wrong parent controller type (%s)\n",
				   pController ? DevKitOnly_StringIdToString(pController->GetType()) : "null");
		return Err::kErrAbort;
	}

	m_hCharacterLocomotion = static_cast<CharacterMotionMatchLocomotion*>(pController);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotionAction::InstanceCreate(AnimStateInstance* pInst)
{
	ParentClass::InstanceCreate(pInst);

	CharacterMotionMatchLocomotion* pMmController = m_hCharacterLocomotion.ToSubsystem();

	const DC::AnimCharacterInstanceInfo* pInstInfo = static_cast<const DC::AnimCharacterInstanceInfo*>(pInst->GetAnimInstanceInfo());

	if (pMmController && (pMmController->m_set.GetId() == pInstInfo->m_locomotion.m_motionMatchSetId))
	{
		m_set = pMmController->m_set;
	}
	else
	{
		m_set = pInstInfo->m_locomotion.m_motionMatchSetId;
	}

	m_playerMoveMode = pInstInfo->m_locomotion.m_playerMoveMode;

	if (const DC::MotionMatchingSettings* pSettings = m_set.GetSettings())
	{
		DC::AnimStateFlag& stateFlags = pInst->GetMutableStateFlags();

		if (pSettings->m_disableNavMeshAdjust || FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmNavMeshAdjust))
		{
			stateFlags |= DC::kAnimStateFlagNoAdjustToNavMesh;
		}

		if (!pSettings->m_disableNpcLegIk && !FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmNpcLegIk))
		{
			stateFlags |= DC::kAnimStateFlagLegIkMoving;
		}

		if (pSettings->m_disableGroundAdjust || FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmGroundAdjust))
		{
			stateFlags |= DC::kAnimStateFlagNoAdjustToGround;
		}
	}

	if (CharacterLocomotionInterface* pInterface = pMmController ? pMmController->GetInterface() : nullptr)
	{
		pInterface->InstaceCreate(pInst);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotionAction::InstanceAlignFunc(const AnimStateInstance* pInst,
															 const BoundFrame& prevAlign,
															 const BoundFrame& currAlign,
															 const Locator& apAlignDelta,
															 BoundFrame* pAlignOut,
															 bool debugDraw)
{
	CharacterMotionMatchLocomotion* pMmController = m_hCharacterLocomotion.ToSubsystem();

	if (!pMmController)
	{
		return false;
	}

	const StringId64 setId = m_set.GetId();
	const StringId64 controllerSetId = pMmController->m_set.GetId();

	if ((setId != controllerSetId) && !g_motionMatchingOptions.m_allowProceduralAlignsFromForeignSets)
	{
		return false;
	}

	BoundFrame desAlign = currAlign;

	if (Abs(pMmController->m_input.m_translationSkew - 1.0f) > NDI_FLT_EPSILON)
	{
		const Vector deltaPs = currAlign.GetTranslationPs() - prevAlign.GetTranslationPs();
		const Vector scaledDeltaPs = deltaPs * pMmController->m_input.m_translationSkew;

		desAlign.SetTranslationPs(prevAlign.GetTranslationPs() + scaledDeltaPs);
	}

	Maybe<AnimSample> sample;
	ArtItemAnimHandle anim = pInst->GetPhaseAnimArtItem();
	if (anim.ToArtItem())
	{
		sample = AnimSample(anim, pInst->Phase(), pInst->IsFlipped());
	}

	//g_prim.Draw(DebugCross(desAlign.GetTranslationWs(), 0.15f, kColorOrangeTrans));
	const MotionMatchingSet* pSet = m_set.GetSetArtItem();

	const Locator finalAlignPs = ApplyProceduralMotionPs(pInst, sample, desAlign, setId, pSet, debugDraw);

	//g_prim.Draw(DebugCross(finalAlignPs.Pos(), 0.12f, kColorGreenTrans));

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Locator& parentSpace = desAlign.GetParentSpace();
		const Vector procDiffPs = finalAlignPs.Pos() - desAlign.GetTranslationPs();
		const Vector procDiffWs = parentSpace.TransformVector(procDiffPs);
		const Point modelPosPs = pMmController->GetMotionModelPs().GetPos();
		const Point modelPosWs = parentSpace.TransformPoint(modelPosPs);

		g_prim.Draw(DebugArrow(desAlign.GetTranslationWs(), procDiffWs, kColorOrangeTrans, 0.2f));
		g_prim.Draw(DebugCross(modelPosWs, 0.1f, kColorBlueTrans, kPrimEnableHiddenLineAlpha));
	}

	ANIM_ASSERT(IsReasonable(finalAlignPs));

	if (pAlignOut)
	{
		*pAlignOut = desAlign;
		pAlignOut->SetLocatorPs(finalAlignPs);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 CharacterMotionMatchLocomotionAction::GetSetId() const
{
	return m_set.GetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::MotionMatchingSettings* CharacterMotionMatchLocomotionAction::GetSettings() const
{
	return m_set.GetSettings();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotionAction::GetAnimControlDebugText(const AnimStateInstance* pInstance,
																   IStringBuilder* pText) const
{
	pText->append_format("%s (Blend %.3f)", GetName(), GetBlend());
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator CharacterMotionMatchLocomotionAction::ApplyProceduralMotionPs(const AnimStateInstance* pInst,
																	  const Maybe<AnimSample>& sample,
																	  const BoundFrame& animDesiredLoc,
																	  const StringId64 setId,
																	  const MotionMatchingSet* pSet,
																	  bool debugDraw) const
{
	const Locator animDesiredLocPs = animDesiredLoc.GetLocatorPs();

	ANIM_ASSERT(IsReasonable(animDesiredLocPs));

	Locator finalAlignPs = animDesiredLocPs;

	CharacterMotionMatchLocomotion* pMotionMatchController = m_hCharacterLocomotion.ToSubsystem();
	if (!pMotionMatchController)
	{
		return finalAlignPs;
	}

	if (g_motionMatchingOptions.m_proceduralOptions.m_enableProceduralAlignTranslation)
	{
		finalAlignPs = pMotionMatchController->ApplyProceduralTranslationPs(finalAlignPs);
	}

	ANIM_ASSERT(IsReasonable(finalAlignPs));

	const DC::AnimCharacterTopInfo* pTopInfo = (const DC::AnimCharacterTopInfo*)pInst->GetAnimTopInfo();
	Quat desRotPs = Normalize(pTopInfo->m_motionMatchLocomotion.m_procRotDelta * finalAlignPs.Rot());

	ANIM_ASSERT(IsNormal(pTopInfo->m_motionMatchLocomotion.m_procRotDelta));
	ANIM_ASSERT(IsNormal(desRotPs));

	const float strafeLockAngleDeg = pMotionMatchController->GetStrafeLockAngleDeg();

	if (strafeLockAngleDeg >= 0.0f && (strafeLockAngleDeg < 180.0f)
		&& pMotionMatchController->m_smoothedUserFacingPs.Valid()
		&& !FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_disableStrafeLockAngle))
	{
		const Vector desiredFacingPs = pMotionMatchController->m_smoothedUserFacingPs.Get();
		const Vector animFacingPs = GetLocalZ(desRotPs);

		ANIM_ASSERT(IsReasonable(desiredFacingPs));
		ANIM_ASSERT(IsReasonable(animFacingPs));

		Quat errRot = kIdentity;
		if (pMotionMatchController->m_facingBiasPs.Valid())
		{
			const Vector facingBiasPs = pMotionMatchController->m_facingBiasPs.Get();
			ANIM_ASSERT(IsReasonable(facingBiasPs));

			errRot = QuatFromVectorsBiased(desiredFacingPs, animFacingPs, facingBiasPs);

			ANIM_ASSERTF(IsNormal(errRot),
						 ("%s -> %s [%s] = %s",
						  PrettyPrint(desiredFacingPs),
						  PrettyPrint(animFacingPs),
						  PrettyPrint(facingBiasPs),
						  PrettyPrint(errRot)));
		}
		else
		{
			errRot = QuatFromVectors(desiredFacingPs, animFacingPs);

			ANIM_ASSERTF(IsNormal(errRot),
						 ("%s -> %s = %s", PrettyPrint(desiredFacingPs), PrettyPrint(animFacingPs), PrettyPrint(errRot)));
		}

		const Quat deltaRot = LimitQuatAngle(errRot, DEGREES_TO_RADIANS(strafeLockAngleDeg));
		const Vector lockedFacingPs = Rotate(deltaRot, desiredFacingPs);

		const Vector upPs = GetLocalY(animDesiredLocPs);
		const Quat lockedRotPs = QuatFromLookAt(lockedFacingPs, upPs);

		ANIM_ASSERT(IsNormal(deltaRot));
		ANIM_ASSERT(IsNormal(lockedRotPs));

		Vec4 axis;
		float errRad = 0.0f;
		errRot.GetAxisAndAngle(axis, errRad);
		const float errDeg = Abs(NormalizeAngle_deg(RADIANS_TO_DEGREES(errRad)));
		const float angleTT = LerpScaleClamp(5.0f, 35.0f, 1.0f, 0.0f, errDeg);

		const DC::MotionMatchingSettings* pSettings = pMotionMatchController->GetSettings();
		const MotionModel& motionModelPs = pMotionMatchController->GetMotionModelPs();

		const float speed = Min(Length(motionModelPs.GetVel()), motionModelPs.GetSprungSpeed());
		const float maxSpeed = GetMaxSpeedForDirection(&pSettings->m_motionSettings,
														motionModelPs.GetDesiredVel(),
														desiredFacingPs);
		const float speedPercent = Limit01(speed / maxSpeed);
		const float speedTT = LerpScale(0.5f, 0.9f, 0.0f, 1.0f, speedPercent);

		const float tt = Max(angleTT, speedTT);

		desRotPs = Normalize(Slerp(desRotPs, lockedRotPs, tt));

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
									&& DebugSelection::Get().IsProcessOrNoneSelected(pMotionMatchController->GetOwnerGameObject())))
		{
			MsgCon("-----------------------------------\n");
			MsgCon("%s : %0.1fdeg\n", sample.Valid() ? DevKitOnly_StringIdToString(sample.Get().GetAnimNameId()) : "<null>", strafeLockAngleDeg);
			MsgCon("  angleTT: %f (%0.1fdeg)\n", angleTT, errDeg);
			MsgCon("  speedTT: %f (%0.1fm/s / %0.1fm/s)\n", speedTT, speed, maxSpeed);
			MsgCon("  tt: %f (%f * %f)\n", tt, Max(angleTT, speedTT), pInst->MotionFade());

			const Locator& parentSpace = pMotionMatchController->GetOwnerGameObject()->GetParentSpace();
			const Point pos = parentSpace.TransformPoint(finalAlignPs.Pos() + kUnitYAxis);

			const Quat maxLockRot = QuatFromAxisAngle(kUnitYAxis, DEGREES_TO_RADIANS(strafeLockAngleDeg));

			const Vector leg0 = parentSpace.TransformVector(Rotate(Conjugate(maxLockRot), desiredFacingPs));
			const Vector leg1 = parentSpace.TransformVector(Rotate(maxLockRot, desiredFacingPs));

			const Vector animFacingWs = parentSpace.TransformVector(animFacingPs);
			const Vector desiredFacingWs = parentSpace.TransformVector(desiredFacingPs);

			g_prim.Draw(DebugArrow(pos, animFacingWs, kColorOrange, 0.5f, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugString(pos + animFacingWs, "anim", kColorOrangeTrans, 0.5f));

			const AnimStateLayer* pLayer = pInst->GetLayer();
			const bool isTop = pLayer ? (pLayer->CurrentStateInstance() == pInst) : false;

			if (isTop)
			{
				if (strafeLockAngleDeg >= 90.0f)
				{
					g_prim.Draw(DebugArc(pos, leg0, desiredFacingWs, kColorGreenTrans, 1.0f, kPrimEnableHiddenLineAlpha, true));
					g_prim.Draw(DebugArc(pos, desiredFacingWs, leg1, kColorGreenTrans, 1.0f, kPrimEnableHiddenLineAlpha, true));
				}
				else
				{
					g_prim.Draw(DebugArc(pos, leg0, leg1, kColorGreenTrans, 1.0f, kPrimEnableHiddenLineAlpha, true));
				}

				g_prim.Draw(DebugArrow(pos, desiredFacingWs, kColorGreen, 0.5f, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugString(pos + desiredFacingWs, "des", kColorGreenTrans, 0.5f));
			}

			g_prim.Draw(DebugArrow(pos, parentSpace.TransformVector(GetLocalZ(desRotPs)), kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugString(pos + parentSpace.TransformVector(GetLocalZ(desRotPs)), "locked", kColorCyanTrans, 0.5f));
		}
	}

	const Quat finalRotPs = pMotionMatchController->AdjustRotationToUprightPs(desRotPs);

	ANIM_ASSERT(IsNormal(finalRotPs));

	finalAlignPs.SetRotation(finalRotPs);

	if (const CharacterLocomotionInterface* pInterface = pMotionMatchController->GetInterface())
	{
		finalAlignPs = pInterface->ApplyProceduralMotionPs(pInst, finalAlignPs, setId, pSet);
	}

	return finalAlignPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchIkAdjustments::IkData::IkData() : m_ikRotation(kIdentity)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchIkAdjustments::IkData::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_joints.Relocate(deltaPos, lowerBound, upperBound);
	m_jacobian.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err MotionMatchIkAdjustments::Init(const SubsystemSpawnInfo& info)
{
	Err result = NdSubsystem::Init(info);

	if (result.Failed())
	{
		return result;
	}

	NdGameObject* pGo = GetOwnerGameObject();

	if (!pGo)
	{
		return Err::kErrAbort;
	}

	m_ikData.m_joints.Init(pGo, SID("spined"), SID("spinea"));
	m_ikData.m_joints.InitIkData(SID("*player-strafe-ik*"));

	JacobianMap::EndEffectorDef spineEffs[] =
	{
		JacobianMap::EndEffectorDef(SID("spinea"), IkGoal::kRotation),
	};
	m_ikData.m_jacobian.Init(&m_ikData.m_joints, SID("spined"), ARRAY_COUNT(spineEffs), spineEffs);
	m_ikData.m_jacobian.m_rotationGoalFactor = 1.0f;

	m_ikData.m_chestJointOffset = m_ikData.m_joints.FindJointOffset(SID("spined"));
	m_ikData.m_headOffset = m_ikData.m_joints.FindJointOffset(SID("spinea"));

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchIkAdjustments::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	m_ikData.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchIkAdjustments::RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::RelocateOwner(deltaPos, lowerBound, upperBound);

	m_ikData.m_joints.RelocateOwner(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchIkAdjustments::DoIk(Quat_arg ikRotDesired, float blend)
{
	if (!g_motionMatchingOptions.m_proceduralOptions.m_enableStrafingIk)
		return;

	if (m_ikData.m_joints.GetNumJoints() == 0)
		return;

	NdGameObject* pGo = GetOwnerGameObject();
	if (!pGo)
		return;

	//MsgCon("Motion Match Ik\n");
	m_ikData.m_ikRotation = m_ikData.m_ikRotationSpring.Track(m_ikData.m_ikRotation,
															  ikRotDesired,
															  GetProcessDeltaTime(),
															  6.0f);
	{
		ScopedTempAllocator jan(FILE_LINE_FUNC);
		m_ikData.m_joints.ReadJointCache();
		JacobianIkInstance ik;
		Locator chestLoc = m_ikData.m_joints.GetJointLocWs(m_ikData.m_chestJointOffset);
		Locator headLoc = m_ikData.m_joints.GetJointLocWs(m_ikData.m_headOffset);
		ik.m_pJoints = &m_ikData.m_joints;
		ik.m_pJacobianMap = &m_ikData.m_jacobian;
		ik.m_pConstraints = m_ikData.m_joints.GetJointConstraints();
		ik.m_blend = 1.0f;
		//ik.m_errTolerance = 0.001f;
		ik.m_maxIterations = 10;
		ik.m_restoreFactor = 0.75f;
		ik.m_goal[0].SetGoalRotation(m_ikData.m_ikRotation * Normalize(headLoc.GetRotation()));
		//g_prim.Draw(DebugCoordAxesLabeled(chestLoc, "chest"), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugCoordAxesLabeled(Locator(chestLoc.GetTranslation(), ik.m_goal[0].GetGoalRotation()), "goal"), kPrimDuration1FramePauseable);
		JacobianIkResult result = SolveJacobianIK(&ik);
		//g_prim.Draw(DebugCoordAxesLabeled(m_ikData.m_joints.GetJointLocWs(m_ikData.m_chestJointOffset), "result"), kPrimDuration1FramePauseable);
		m_ikData.m_joints.WriteJointCacheBlend(blend);
	}

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_drawOptions.m_drawLocomotion) && false)
	{
		const Vector facing = GetLocalZ(pGo->GetRotation());
		const Vector desiredFacing = Rotate(ikRotDesired, facing);
		const Vector smoothedFacing = Rotate(m_ikData.m_ikRotation, facing);

		Vector drawOffset = Vector(0.0f, 0.5f, 0.0f);
		g_prim.Draw(DebugArrow(pGo->GetTranslation() + drawOffset, facing, kColorGreen), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugArrow(pGo->GetTranslation() + drawOffset*0.9, desiredFacing, kColorMagenta), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugArrow(pGo->GetTranslation() + drawOffset*0.8, smoothedFacing, kColorCyan), kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err CharacterMotionMatchLocomotion::Init(const SubsystemSpawnInfo& info)
{
	LoggerScopeJanitor logJanitor;
	LoggerNewLog("[[tag character-motion-match]][[yellow]]CharacterMotionMatchLocomotion::Init");

	m_hasUpdated = false;

	Err result = ParentClass::Init(info);
	if (result.Failed())
	{
		return result;
	}

	const SpawnInfo& spawnInfo = static_cast<const SpawnInfo&>(info);

	NdGameObject* pOwner = spawnInfo.m_pOwner;
	if (!pOwner)
	{
		return Err::kErrGeneral;
	}

	m_hLocomotionInterface = spawnInfo.m_hLocomotionInterface;

	ChangeInterface(spawnInfo.m_locomotionInterfaceType);

	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();

	if (!pLocomotionInterface)
	{
		return Err::kErrGeneral;
	}

	const Vector velPs = pOwner->IsKindOf(SID("PlayerBase")) ? pOwner->GetBaseVelocityPs() : pOwner->GetVelocityPs();

	const Locator& locPs = pOwner->GetLocatorPs();

	m_motionModelPs = MotionModel(locPs.Pos(), GetLocalZ(locPs), VectorXz(velPs), nullptr);

	m_speedScale = 1.0f;
	m_timeScale = 1.0f;

	m_initialBlendTime = spawnInfo.m_initialBlendTime;
	m_initialBlendCurve = spawnInfo.m_initialBlendCurve;
	m_initialMotionBlendTime = spawnInfo.m_initialMotionBlendTime;
	m_disableBlendLimiter = spawnInfo.m_disableBlendLimiter;

	Character* pChar = GetOwnerCharacter();

	pLocomotionInterface->GetInput(&m_input);

	m_set = m_input.m_setId;
	m_playerMoveMode = m_input.m_playerMoveMode;

	if (!m_set.IsValid())
	{
		if (FALSE_IN_FINAL_BUILD(EngineComponents::GetNdGameInfo()->m_devCaptureMode == DevCaptureMode::kNone))
		{
			if (m_set.MotionMatchSetArtItemId() != INVALID_STRING_ID_64)
			{
				MsgConErr("Motion matching set '%s' is not valid, is the motion matching actor '%s' loaded?\n",
						  DevKitOnly_StringIdToString(m_input.m_setId),
						  DevKitOnly_StringIdToString(m_set.MotionMatchSetArtItemId()));
			}
			else
			{
				MsgConErr("Motion matching set '%s' is not valid, does it exist in DC?\n",
						  DevKitOnly_StringIdToString(m_input.m_setId));
			}
		}
		return Err::kErrNotFound;
	}

	m_transitions = TransitionPtr(m_input.m_transitionsId, SID("motion-matching"));
	m_blendedSettings.Push(m_set, Seconds(0.0f), pChar->GetClock());

	if (!m_blendedSettings.Valid())
	{
		MsgErr("No valid motion matching model settings for set '%s'\n", DevKitOnly_StringIdToString(m_input.m_setId));
		return Err::kErrGeneral;
	}

	const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();

	if (!pMotionSet)
	{
		MsgErr("MotionMatchingSet '%s' isn't loaded\n", DevKitOnly_StringIdToString(m_set.MotionMatchSetArtItemId()));
		return Err::kErrNotFound;
	}

	if (!pMotionSet->HasRequiredJointsForSet(pOwner))
	{
		MsgErr("MotionMatchingSet '%s' requires joints that '%s' (type: %s) doesn't have\n",
			   DevKitOnly_StringIdToString(m_set.MotionMatchSetArtItemId()),
			   DevKitOnly_StringIdToString(pOwner->GetUserId()),
			   DevKitOnly_StringIdToString(pOwner->GetTypeNameId()));

		MsgErr("Missing Joints:\n");

		for (U32F i = 0; i < pMotionSet->m_pSettings->m_pose.m_numBodies; ++i)
		{
			if (pMotionSet->m_pSettings->m_pose.m_aBodies[i].m_isCenterOfMass)
				continue;

			const StringId64 jointId = pMotionSet->m_pSettings->m_pose.m_aBodies[i].m_jointId;
			if (pOwner->FindJointIndex(jointId) >= 0)
				continue;

			MsgErr("  pose-%d : %s\n", i, DevKitOnly_StringIdToString(jointId));
		}

		if ((pMotionSet->m_pSettings->m_pose.m_facingJointId != INVALID_STRING_ID_64) &&
			(pOwner->FindJointIndex(pMotionSet->m_pSettings->m_pose.m_facingJointId) < 0))
		{
			MsgErr("  facing : %s\n", DevKitOnly_StringIdToString(pMotionSet->m_pSettings->m_pose.m_facingJointId));
		}

		return Err::kErrGeneral;
	}

	m_prevSetId = SID("none");
	m_previousMatchTime = TimeFrameNegInfinity();
	m_hIk = static_cast<MotionMatchIkAdjustments*>(NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																	   SubsystemSpawnInfo(SID("MotionMatchIkAdjustments"),
																						  pOwner),
																	   FILE_LINE_FUNC));

	SetActionState(ActionState::kUnattached);

	m_matchRotationPs = pOwner->GetRotationPs();
	m_prevRotationPs = m_matchRotationPs;

	m_disableExternalTransitions = spawnInfo.m_disableExternalTransitions;

	if (spawnInfo.m_pHistorySeed)
	{
		m_locomotionHistory.Reset();

		for (const LocomotionState& seed : *spawnInfo.m_pHistorySeed)
		{
			if (m_locomotionHistory.IsFull())
				break;

			m_locomotionHistory.Enqueue(seed);
		}
	}

	g_motionMatchLocomotionDebugger->RegisterController(this);

	AnimStateLayer* pBaseLayer = pChar->GetAnimControl()->GetBaseStateLayer();
	pBaseLayer->SetActiveSubsystemControllerId(GetSubsystemId());

	m_retargetScale = 1.0f;

	const FgAnimData* pAnimData = pOwner->GetAnimData();
	const ArtItemSkeleton* pAnimateSkel = pAnimData ? pAnimData->m_animateSkelHandle.ToArtItem() : nullptr;
	if (pAnimateSkel && pAnimateSkel->m_skelId != pMotionSet->m_skelId)
	{
		const SkelTable::RetargetEntry* pRetarget = SkelTable::LookupRetarget(pMotionSet->m_skelId, pAnimateSkel->m_skelId);

		if (pRetarget)
		{
			m_retargetScale = pRetarget->m_scale;
		}
	}

	m_smoothedUserFacingPs = MAYBE::kNothing;
	m_facingBiasPs = MAYBE::kNothing;

	m_proceduralRotationDeltaPs = kIdentity;
	m_strafeRotationDeltaPs		= kIdentity;

	const U32F numTrajSamples = m_set.GetNumTrajectorySamples();

	m_animTrajectoryOs = AnimTrajectory(m_locomotionHistory.GetMaxSize() + numTrajSamples + 1);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::ChangeInterface(StringId64 newInterfaceTypeId)
{
	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();
	if (pLocomotionInterface)
	{
		if (pLocomotionInterface->GetType() == newInterfaceTypeId)
		{
			pLocomotionInterface->SetParent(this);
			return;
		}

		pLocomotionInterface->Kill();
		m_hLocomotionInterface = nullptr;
	}

	GAMEPLAY_ASSERT(newInterfaceTypeId != INVALID_STRING_ID_64);
	SubsystemSpawnInfo interfaceSpawnInfo(newInterfaceTypeId, this);
	pLocomotionInterface = (CharacterLocomotionInterface*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																			  interfaceSpawnInfo,
																			  FILE_LINE_FUNC);

	GAMEPLAY_ASSERTF(pLocomotionInterface,
					 ("Failed to create CharacterLocomotionInterface '%s'",
					  DevKitOnly_StringIdToString(newInterfaceTypeId)));

	m_hLocomotionInterface = pLocomotionInterface;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::ChangeInterface(CharacterLocomotionInterface* pNewInterface)
{
	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();
	if (pLocomotionInterface && (pNewInterface == pLocomotionInterface))
	{
		return;
	}

	if (pLocomotionInterface)
	{
		pLocomotionInterface->Kill();
		m_hLocomotionInterface = nullptr;
	}

	m_hLocomotionInterface = pNewInterface;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::EnterNewParentSpace(const Transform& matOldToNew,
														 const Locator& oldParentSpace,
														 const Locator& newParentSpace)
{
	if (CharacterLocomotionInterface* pInterface = GetInterface())
	{
		pInterface->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}

	for (LocomotionState& locoState : m_locomotionHistory)
	{
		locoState.m_alignPs = Locator(locoState.m_alignPs.AsTransform() * matOldToNew);
		locoState.m_velPs	= locoState.m_velPs * matOldToNew;
	}

	m_motionModelPs.ApplyTransform(matOldToNew);

	if (m_smoothedUserFacingPs.Valid())
	{
		m_smoothedUserFacingPs = m_smoothedUserFacingPs.Get() * matOldToNew;
	}

	if (m_facingBiasPs.Valid())
	{
		m_facingBiasPs = m_facingBiasPs.Get() * matOldToNew;
	}

	const Quat rotTransform = Locator(matOldToNew).Rot();

	// (JDB) These are deltas in parent space, shouldn't need to transform them?
	// m_proceduralRotationDeltaPs = rotTransform * m_proceduralRotationDeltaPs;
	// m_strafeRotationDeltaPs	  = rotTransform * m_strafeRotationDeltaPs;

	m_matchRotationPs = rotTransform * m_matchRotationPs;
	m_prevRotationPs  = rotTransform * m_prevRotationPs;

	m_input.ApplyTransform(matOldToNew);

	m_proceduralTransDeltaPs = m_proceduralTransDeltaPs * matOldToNew;

	m_lastMatchLocPs = Locator(m_lastMatchLocPs.AsTransform() * matOldToNew);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::OnKilled()
{
	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		const Character* pSelf = GetOwnerCharacter();
		if (pSelf && pSelf->IsKindOf(SID("Player")))
		{
			Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::OnKilled: Player\n");
		}
		else if (!pSelf)
		{
			Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::OnKilled: Unknown Character\n");
		}
	}

	g_motionMatchLocomotionDebugger->UnregisterController(this);

	ParentClass::OnKilled();

	if (CharacterLocomotionInterface* pInterface = GetInterface())
	{
		pInterface->Kill();
	}

	if (MotionMatchIkAdjustments* pIk = m_hIk.ToSubsystem())
	{
		pIk->Kill();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::IsValidToUpdate() const
{
	bool validToUpdate = false;

	const ActionState as = GetActionState();

	switch (as)
	{
	case ActionState::kUnattached:
	case ActionState::kTop:
	case ActionState::kExiting:
		validToUpdate = true;
		break;
	}

	return validToUpdate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(CharacterMotionMatchLocomotion, Update)
{
	LoggerScopeJanitor logJanitor;
	LoggerNewLog("[[tag character-motion-match]][[yellow]]CharacterMotionMatchLocomotionAction::Update");

	const bool validToUpdate = IsValidToUpdate();

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		const Character* pSelf = GetOwnerCharacter();
		if (pSelf && pSelf->IsKindOf(SID("Player")))
		{
			{
				char debugText[1024];
				snprintf(debugText, sizeof(debugText), "CharacterMotionMatchLocomotion::Update, GetActionState():%d\n", (int)GetActionState());
				Character::AppendDebugTextInFinal(debugText);
			}

			if (!validToUpdate)
			{
				const NdSubsystemAnimController* pActiveCtrl = pSelf->GetActiveSubsystemController();
				if (pActiveCtrl != nullptr)
				{
					char debugText[1024];
					snprintf(debugText, sizeof(debugText), "GetActiveSubsystemController: %s\n", DevKitOnly_StringIdToString(pActiveCtrl->GetType()));
					Character::AppendDebugTextInFinal(debugText);
				}
				else
				{
					Character::AppendDebugTextInFinal("GetActiveSubsystemController: none\n");
				}
			}
		}
	}

	if (validToUpdate)
	{
		RequestAnims();
	}
	else
	{
		RecordedState record;
		record.m_valid = false;
		g_motionMatchLocomotionDebugger->RecordData(this, record);
		
		if (IsUnattached())
		{
			LoggerNewLog("Kill Motion Matching (Unattached)");
			Kill();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::TryChangeSetId(StringId64 newSetId)
{
	if (newSetId == INVALID_STRING_ID_64)
		return false;

	if (m_set.GetId() == newSetId)
		return true;

	PROFILE(Animation, MML_TryChangeSetId);

	Character& self = *GetOwnerCharacter();

	const DC::MotionMatchingSettings* pPrevSettings = m_set.GetSettings();

	m_input.m_setId = newSetId;
	m_set = m_input.m_setId;

	if (!m_set.IsValid())
	{
		const StringId64 mmArtItemId = m_set.MotionMatchSetArtItemId();
		if (mmArtItemId != INVALID_STRING_ID_64)
		{
			MsgErr("[%s] MotionMatching asked for invalid input set '%s', is the motion matching actor '%s' loaded?\n",
				   self.GetName(),
				   DevKitOnly_StringIdToString(newSetId),
				   DevKitOnly_StringIdToString(mmArtItemId));

			MsgConScriptError("[%s] MotionMatching asked for invalid input set '%s', is the motion matching actor '%s' loaded?\n",
							  self.GetName(),
							  DevKitOnly_StringIdToString(newSetId),
							  DevKitOnly_StringIdToString(mmArtItemId));

			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (self.IsKindOf(SID("Player")))
				{
					char debugText[1024];
					snprintf(debugText, sizeof(debugText), "[%s] MotionMatching asked for invalid input set '%s', is the motion matching actor '%s' loaded?\n",
						self.GetName(),
						DevKitOnly_StringIdToString(newSetId),
						DevKitOnly_StringIdToString(mmArtItemId));
					Character::AppendDebugTextInFinal(debugText);
				}
			}
		}
		else
		{
			MsgErr("[%s] MotionMatching asked for invalid input set '%s'\n",
				   self.GetName(),
				   DevKitOnly_StringIdToString(newSetId));

			MsgConScriptError("[%s] MotionMatching asked for invalid input set '%s'\n",
							  self.GetName(),
							  DevKitOnly_StringIdToString(newSetId));

			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (self.IsKindOf(SID("Player")))
				{
					char debugText[1024];
					snprintf(debugText, sizeof(debugText), "[%s] MotionMatching asked for invalid input set '%s'\n",
						self.GetName(),
						DevKitOnly_StringIdToString(newSetId));
					Character::AppendDebugTextInFinal(debugText);
				}
			}
		}

		Kill();
		return false;
	}

	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	const Clock* pClock = self.GetClock();

	float blendTime = pSettings ? pSettings->m_settingsBlendTime : -1.0f;

	if (pSettings->m_disableSettingsBlend || pPrevSettings->m_disableSettingsBlend)
	{
		blendTime = 0.0f;
	}
	else if (blendTime < 0.0f)
	{
		const BlendPair defBlendTime = BlendPair(pSettings, 0.5f);
		const BlendPair blendPair = GetMotionMatchBlendSpeeds(m_transitions, m_prevSetId, m_input.m_setId, defBlendTime);
		blendTime = blendPair.m_motionFadeTime;
	}

	m_blendedSettings.Push(m_set, Seconds(blendTime), pClock);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::RequestAnims()
{
	PROFILE(Animation, MML_RequestAnims);

	m_hasUpdated = true;

	Character* pSelf = GetOwnerCharacter();
	if (!pSelf)
		return;

	Character& self = *pSelf;
	const Clock* pClock = self.GetClock();
	const TimeFrame curTime = pClock->GetCurTime();

	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();

	const bool fullyOwnedByMe = !IsExiting() && IsActiveController();

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		if (self.IsKindOf(SID("Player")))
		{
			char debugText[1024];
			snprintf(debugText, sizeof(debugText), "CharacterMotionMatchLocomotion::RequestAnims, fullyOwnedByMe:%d\n", fullyOwnedByMe);
			Character::AppendDebugTextInFinal(debugText);
		}
	}

	if (fullyOwnedByMe)
	{
		m_input = ICharacterLocomotion::InputData();

		if (pLocomotionInterface)
		{
			PROFILE(Animation, MML_GetInput);

			pLocomotionInterface->GetInput(&m_input);
		}

		m_playerMoveMode = m_input.m_playerMoveMode;

		if (m_input.m_transitionsId != INVALID_STRING_ID_64 && m_transitions.GetId() != m_input.m_transitionsId)
		{
			m_transitions = TransitionPtr(m_input.m_transitionsId, SID("motion-matching"));
		}

		if (!TryChangeSetId(m_input.m_setId))
		{
			return;
		}
	}

	pSettings = m_set.GetSettings();

	if (!pSettings)
	{
		return;
	}

	const bool inIdle = IsCharInIdle();
	m_motionModelPs.SetCharInIdle(inIdle);

	if (!pSettings->m_supportsStrafe && (Length(m_input.m_desiredVelocityDirPs) > NDI_FLT_EPSILON))
	{
		m_input.m_desiredFacingPs = MAYBE::kNothing;
	}

	const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();

	m_blendedSettings.Update(curTime);

	m_timeScale = m_input.m_speedScale;
	GAMEPLAY_ASSERT(IsReasonable(m_timeScale));

	if (m_prevSetId != m_input.m_setId)
	{
		DC::AnimCharacterTopInfo* pTopInfo = self.GetAnimControl()->TopInfo<DC::AnimCharacterTopInfo>();

		if (pTopInfo)
		{
			pTopInfo->m_motionMatchLocomotion.m_speedScale = m_speedScale * m_timeScale;
			pTopInfo->m_motionMatchLocomotion.m_procRotDelta = m_proceduralRotationDeltaPs;
		}
	}

	const float deltaTime = pClock->GetDeltaTimeInSeconds() * m_timeScale;

	const Locator& parentSpace = self.GetParentSpace();
	const Locator matchLocPs = GetMatchLocatorPs(self);

	const DC::MotionModelSettings motionSettings = m_blendedSettings.Get(curTime);

	BlendedMotionModelSettingsSupplier blendedSettings(m_blendedSettings, curTime);

	MotionModelInput modelInputPs;
	modelInputPs.m_velDirPs		= m_input.m_desiredVelocityDirPs;
	modelInputPs.m_facingPs		= m_input.m_desiredFacingPs;
	modelInputPs.m_baseYawSpeed = m_input.m_baseYawSpeed;

	MotionModelPathInput pathInput;
	if (m_input.m_pPathFunc)
	{
		PROFILE(Animation, MML_PathFunc);
		pathInput = m_input.m_pPathFunc(&self, pMotionSet, false);
		modelInputPs.m_pPathInput = &pathInput;
	}

	if (m_input.m_pStickFunc)
	{
		PROFILE(Animation, MML_StickFunc);
		modelInputPs = m_input.m_pStickFunc(Character::FromProcess(&self),
											&m_input,
											pMotionSet,
											pSettings,
											MotionModelIntegrator(m_motionModelPs, &blendedSettings),
											Seconds(0.0f),
											false,
											false);
	}

	modelInputPs.m_charLocPs	   = self.GetLocatorPs();
	modelInputPs.m_charNavLoc	   = pSelf->GetNavLocation();
	modelInputPs.m_groundNormalPs  = m_input.m_groundNormalPs;
	modelInputPs.m_translationSkew = m_input.m_translationSkew;
	modelInputPs.m_pInterface	   = pLocomotionInterface;

	const NavCharacter* pNavChar = NavCharacter::FromProcess(&self);
	const NavControl* pNavControl = pNavChar ? pNavChar->GetNavControl() : nullptr;

	if (pNavControl)
	{
		modelInputPs.m_obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();
		modelInputPs.m_obeyedBlockers		= pNavControl->GetCachedObeyedNavBlockers();
	}

	if (TRUE_IN_FINAL_BUILD(!g_motionMatchingOptions.m_disableRetargetScaling))
	{
		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_retargetScaleOverride > 0.0f))
		{
			modelInputPs.m_translationSkew *= g_motionMatchingOptions.m_retargetScaleOverride;
		}
		else if (m_input.m_retargetScaleOverride > 0.0f)
		{
			modelInputPs.m_translationSkew *= m_input.m_retargetScaleOverride;
		}
		else
		{
			modelInputPs.m_translationSkew *= m_retargetScale;
		}
	}

	m_facingBiasPs = modelInputPs.m_facingBiasPs;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	MatchParams params = CreateMatchParams(m_input, deltaTime, &m_animTrajectoryOs, false);

	m_lastMatchLocPs = params.m_charLocatorPs;

	Maybe<AnimSample> maybeMatch = MAYBE::kNothing;

	if (fullyOwnedByMe)
	{
		maybeMatch = MatchFromInput(params);
	}

	RecordedState record;

	if (pMotionSet)
	{
		record.m_input = m_input;
		record.m_input.m_pStickFunc = nullptr;
		record.m_input.m_desiredVelocityDirPs = modelInputPs.m_velDirPs;
		record.m_input.m_desiredFacingPs	  = modelInputPs.m_facingPs;
		record.m_input.m_mmParams = params.m_searchParams; // yuck (2)

		record.m_previousMatchTime = m_previousMatchTime;
		record.m_set = m_set;
		record.m_maybeCurSample = params.m_maybeCurSample;
		record.m_charLocatorPs	= matchLocPs;
		record.m_clock = *pClock;
		record.m_allowExternalTransitions = params.m_allowExternalTransitionAnims;
		record.m_animChangeMode = m_input.m_animChangeMode;
		record.m_desiredGroupId = params.m_desiredGroupId;
		record.m_valid = true;

		if (params.m_pCharPose)
		{
			record.m_pose = RecordedMotionPose(*params.m_pCharPose, pMotionSet->m_pSettings->m_pose);
		}

		if (params.m_pDesiredTrajectory)
		{
			const size_t numSamples = record.m_traj.Capacity();

			const float minTime = params.m_pDesiredTrajectory->GetMinTime();
			const float maxTime = params.m_pDesiredTrajectory->GetMaxTime();

			for (I32F iSample = 0; iSample < numSamples; ++iSample)
			{
				const float tt = (numSamples > 1) ? (float(iSample) / float(numSamples - 1)) : 0.0f;
				const float sampleTime = Lerp(minTime, maxTime, tt);

				AnimTrajectorySample sample = params.m_pDesiredTrajectory->Get(sampleTime);
				record.m_traj.PushBack(sample);
			}
		}
	}

	g_motionMatchLocomotionDebugger->RecordData(this, record);

	if (maybeMatch.Valid())
	{
		PROFILE(Animation, MML_ApplyBestSample);

		if (ApplyBestSample(self, maybeMatch.Get(), pSettings, params.m_animChangeMode))
		{
			m_prevSetId = m_set.GetId();
			m_previousMatchTime = curTime;
		}
		else if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (self.IsKindOf(SID("Player")))
			{
				Character::AppendDebugTextInFinal("ApplyBestSample returns false\n");
			}
		}
	}
	else if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		if (self.IsKindOf(SID("Player")))
		{
			Character::AppendDebugTextInFinal("maybeMatch.Valid() is false\n");
		}
	}

	const Point prevModelPosPs = m_motionModelPs.GetPos();

	// Internally updates desired facing and strafe state
	// Do this after matching so that our debug draw will reflect the animation
	// chosen on the *next* frame step
	{
		PROFILE(Animation, MML_Step);
		m_motionModelPs.Step(parentSpace, modelInputPs, &motionSettings, deltaTime, false);
	}

	if (FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessSelected(pSelf) && g_motionMatchingOptions.m_pauseWhenOffPath
							 && m_motionModelPs.IsFollowingPath() & !m_motionModelPs.IsOnPath()))
	{
		g_ndConfig.m_pDMenuMgr->SetProgPause(true);
	}

	const float curClampDist = m_motionModelPs.GetProceduralClampDist();

	if ((curClampDist > 0.0f) && !pSettings->m_disableNavMeshAdjust && m_motionModelPs.IsFollowingPath()
		&& (Length(m_motionModelPs.GetDesiredVel()) > NDI_FLT_EPSILON) && fullyOwnedByMe)
	{
		if (pNavControl)
		{
			PROFILE(Animation, MML_NavClearance);

			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			NavLocation navLoc = self.GetNavLocation();

			if (const NavPoly* pPoly = navLoc.ToNavPoly())
			{
				const NavMesh* pMesh = pPoly->GetNavMesh();

				NavMesh::ClearanceParams ccParams;

				const Point myPosPs = pSelf->GetTranslationPs();
				const Point modelPosPs = m_motionModelPs.GetPos();
				const float modelOffset = DistXz(myPosPs, modelPosPs);
				const float clampDist = m_motionModelPs.GetProceduralClampDist();
				const float modelOverhang = Max(clampDist - modelOffset, 0.0f);

#if 0
				ccParams.m_point  = pMesh->ParentToLocal(self.GetTranslationPs());
				ccParams.m_radius = (pNavControl->GetCurrentNavAdjustRadius() * 1.5f) + modelOverhang;
#else
				ccParams.m_point = pMesh->ParentToLocal(modelPosPs);
				ccParams.m_radius = modelOffset + (pNavControl->GetCurrentNavAdjustRadius() * 1.15f);
#endif
				ccParams.m_obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();
				ccParams.m_obeyedBlockers = pNavControl->GetCachedObeyedNavBlockers();
				ccParams.m_dynamicProbe	  = !ccParams.m_obeyedBlockers.AreAllBitsClear();

				const NavMesh::ProbeResult res = pMesh->CheckClearanceLs(&ccParams);

				if (res == NavMesh::ProbeResult::kHitEdge)
				{
					const Point impactPosPs = pMesh->LocalToParent(ccParams.m_impactPoint);
					const float dotTest = DotXz(impactPosPs - modelPosPs, myPosPs - modelPosPs) > 0.0f;
					if (dotTest >= 0.0f)
					{
						if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_drawOptions.m_drawMotionModel))
						{
							g_prim.Draw(DebugCircle(pMesh->LocalToWorld(ccParams.m_point), kUnitYAxis, ccParams.m_radius, kColorRed));
							g_prim.Draw(DebugCross(pMesh->LocalToWorld(ccParams.m_impactPoint), 0.1f, kColorRed));
						}

						const float impactDist = DistXz(ccParams.m_impactPoint, ccParams.m_point);

						const float alpha = g_motionMatchingOptions.m_proceduralOptions.m_clampDistReductionAlpha;
						const float scale = g_motionMatchingOptions.m_proceduralOptions.m_clampDistReductionScale;

						const float tt = Limit01(deltaTime * alpha);
						const float reductionAmount = (Max(ccParams.m_radius - impactDist, 0.0f) * scale * tt) + modelOverhang;
						ANIM_ASSERTF(reductionAmount >= -NDI_FLT_EPSILON, ("Negative clamp reduction %f", reductionAmount));

						const float newClampDist = Max(0.0f, curClampDist - reductionAmount);

						if (newClampDist > NDI_FLT_EPSILON)
						{
							m_motionModelPs.ReduceProceduralClampDist(reductionAmount);
						}
						else
						{
							// keeps the clamp distance from resetting the frame we don't shrink it
							m_motionModelPs.ReduceProceduralClampDist(0.0f);
						}
					}
				}
			}
		}
	}

	if (pLocomotionInterface)
	{
		PROFILE(Animation, MML_UpdateLocomotionInterface);
		pLocomotionInterface->Update(&self, m_motionModelPs);
	}

	// Some other system must have taken over
	if (GetActionState() == ActionState::kUnattached)
	{
		LoggerNewLog("Kill Motion Matching: Failed to start an anim");
		MsgAnim("Killing motion matching controller because it failed to start an anim\n");
		Kill();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::CreateTrajectory(AnimTrajectory* pTrajectory,
													  const InputData& input,
													  const Locator& matchLocPs,
													  bool debug) const
{
	PROFILE(Animation, MML_CreateTrajectory);

	ANIM_ASSERT(pTrajectory);

	const Character& self = *GetOwnerCharacter();
	const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
	const Clock* pClock = self.GetClock();
	const TimeFrame curTime = pClock->GetCurTime();

	pTrajectory->Clear();

	const U32F numTrajSamples = m_set.GetNumTrajectorySamples();
	const U32F numPrevTrajSamples = m_set.GetNumPrevTrajectorySamples();
	const U32F maxTrajSamples = pTrajectory->GetCapacity();
	const I32F numAvailableForPrev = Max(maxTrajSamples - numTrajSamples, 0LL);

	if ((numPrevTrajSamples > 0) && (numAvailableForPrev > 0))
	{
		const float maxPrevTime = m_set.GetMaxPrevTrajectoryTime();
		const float sampleTimeStep = maxPrevTime / float(numAvailableForPrev);

		/*
		const LocomotionState& beginState = *m_locomotionHistory.Begin();
		const LocomotionState& endState = *m_locomotionHistory.RBegin();
		MsgCon("LocoHistory: %0.1fs : %0.1fs [max: %0.1fs] [%d / %d samples for prev]\n",
			   (beginState.m_time - curTime).ToSeconds(),
			   (endState.m_time - curTime).ToSeconds(),
			   -maxPrevTime,
			   numAvailableForPrev,
			   maxTrajSamples);
		*/


		float curSampleTime = -maxPrevTime;
		for (const LocomotionState& locoState : m_locomotionHistory)
		{
			AnimTrajectorySample g;

			const float sampleTime = (locoState.m_time - curTime).ToSeconds();
			if ((sampleTime < curSampleTime) || (sampleTime >= 0.0f))
				continue;

			curSampleTime += sampleTimeStep;

			const bool flatten = pSettings
								 && (pSettings->m_motionSettings.m_projectionMode == DC::kProjectionModeCharacterPlane);

			const Point posPs = flatten ? PointFromXzAndY(locoState.m_alignPs.Pos(), m_motionModelPs.GetPos())
										: locoState.m_alignPs.Pos();

			g.SetTime(sampleTime);
			g.SetPosition(matchLocPs.UntransformPoint(posPs));
			g.SetFacingDir(matchLocPs.UntransformVector(GetLocalZ(locoState.m_alignPs)));
			g.SetVelocity(matchLocPs.UntransformVector(locoState.m_velPs));
			g.SetYawSpeed(locoState.m_yawSpeed);

			pTrajectory->Add(g);
		}
	}

	BlendedMotionModelSettingsSupplier blendedSettings(m_blendedSettings, curTime);

	MotionModelPathInput pathInput;
	if (input.m_pPathFunc)
	{
		pathInput = input.m_pPathFunc(&self, pMotionSet, debug);
	}

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	CharacterLocomotionInterface* pInterface = GetInterface();

	MotionModelInputFunctor inputFunctor = MotionModelInputFunctor(&self,
																   input,
																   &pathInput,
																   pInterface,
																   pMotionSet,
																   pSettings,
																   debug);

	const Locator& parentSpace	  = self.GetParentSpace();
	const float maxSetTime		  = m_set.GetMaxTrajectoryTime();
	const float rotationTime	  = pSettings->m_proceduralRotationTime;
	const float maxTrajectoryTime = Max(maxSetTime, rotationTime);

	GetDesiredTrajectory(matchLocPs,
						 parentSpace,
						 m_motionModelPs,
						 &blendedSettings,
						 numTrajSamples,
						 maxTrajectoryTime,
						 inputFunctor,
						 pTrajectory,
						 debug);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterMotionMatchLocomotion::MatchParams CharacterMotionMatchLocomotion::CreateMatchParams(const InputData& input,
																							  float dt,
																							  AnimTrajectory* pTrajectoryOut,
																							  bool debug) const
{
	PROFILE(Animation, MML_CreateMatchParams);

	const Character& self	   = *GetOwnerCharacter();
	const Clock* pClock		   = self.GetClock();
	const TimeFrame curTime	   = pClock->GetCurTime();
	const Locator& parentSpace = self.GetParentSpace();

	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();

	Maybe<AnimSample> maybeCurSample = GetCurrentAnimSample();

	const Locator curMatchLocPs	   = GetMatchLocatorPs(self);
	const Locator futureMatchLocPs = GetFutureLocatorPs(curMatchLocPs,
														self.GetSkeletonId(),
														maybeCurSample,
														dt * m_timeScale);

	CreateTrajectory(pTrajectoryOut, input, futureMatchLocPs, debug);

	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();

	MatchParams params	 = MatchParams(input);
	params.m_parentSpace = parentSpace;
	params.m_pClock		 = pClock;
	params.m_previousMatchTime = m_previousMatchTime;
	params.m_set	  = m_set;
	params.m_maybeCurSample = maybeCurSample;
	params.m_charLocatorPs	= futureMatchLocPs;
	params.m_pSelf = &self;
	params.m_allowExternalTransitionAnims = AreExternalTransitionAnimsAllowed();
	params.m_desiredGroupId		= 0;
	params.m_debug	   = debug;
	params.m_pCharPose = pLocomotionInterface->GetPose(pMotionSet, debug);
	params.m_forceExternalPose = m_input.m_forceExternalPose;
	params.m_animChangeMode = m_input.m_animChangeMode;
	params.m_pDesiredTrajectory = pTrajectoryOut;
	params.m_transitionalInterval = Max(Seconds(pSettings->m_transitionIntervalSec), input.m_transitionInterval);

	if (m_requestRefreshAnimState)
	{
		params.m_animChangeMode = AnimChangeMode::kForced;
	}

	params.m_searchParams = input.m_mmParams;

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_overrideMirrorMode != (I64)MMMirrorMode::kInvalid))
	{
		params.m_searchParams.m_mirrorMode = MMMirrorMode(g_motionMatchingOptions.m_overrideMirrorMode);
	}
	else if (input.m_mmParams.m_mirrorMode == MMMirrorMode::kInvalid)
	{
		if (pSettings->m_allowMirror)
		{
			params.m_searchParams.m_mirrorMode = MMMirrorMode::kAllow;
		}
		else
		{
			params.m_searchParams.m_mirrorMode = MMMirrorMode::kNone;
		}
	}

	/************************************************************************/
	/* Desired Group Index                                                  */
	/************************************************************************/
	if (input.m_pGroupFunc)
	{
		BlendedMotionModelSettingsSupplier blendedSettings(m_blendedSettings, curTime);

		MotionModelIntegrator integratorPs = MotionModelIntegrator(m_motionModelPs, &blendedSettings);

		AnimTrajectorySample tailSampleOs = m_animTrajectoryOs.GetTail();
		Point trajectoryEndPosOs = kOrigin;

		if (tailSampleOs.IsPositionValid())
		{
			trajectoryEndPosOs = tailSampleOs.GetPosition();
		}

		params.m_desiredGroupId = input.m_pGroupFunc(&self,
													 pMotionSet,
													 pSettings,
													 integratorPs,
													 trajectoryEndPosOs,
													 debug);
	}

	AnimSampleBiased extraBias;
	if (pLocomotionInterface->GetExtraSample(extraBias))
	{
		params.m_extraBias = extraBias;
	}

	if (FALSE_IN_FINAL_BUILD(debug && g_motionMatchingOptions.m_drawOptions.m_drawTrajEndPos && DebugSelection::Get().IsOnlyProcessSelected(&self)))
	{
		AnimTrajectorySample tailSampleOs = m_animTrajectoryOs.GetTail();

		const Point posPs = params.m_charLocatorPs.TransformPoint(tailSampleOs.GetPosition());
		const Vector facingPs = params.m_charLocatorPs.TransformVector(tailSampleOs.GetFacingDir());

		const Point posWs = params.m_parentSpace.TransformPoint(posPs);
		const Vector facingWs = params.m_parentSpace.TransformVector(facingPs);

		g_prim.Draw(DebugSphere(posWs, 0.1f, kColorBlue, PrimAttrib(kPrimDisableDepthTest, kPrimEnableWireframe)));
		g_prim.Draw(DebugArrow(posWs, facingWs * 0.3f, kColorBlue, 0.5f, kPrimDisableDepthTest));
	}

	return params;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
Maybe<AnimSample> CharacterMotionMatchLocomotion::MatchFromInput(const MatchParams& params)
{
	PROFILE_ACCUM(Mm_MatchFromInput);

	const Character* pSelf = params.m_pSelf;

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		if (pSelf && pSelf->IsKindOf(SID("Player")))
		{
			Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::MatchFromInput\n");
		}
	}

	if (!params.m_set.IsValid())
	{
		return MAYBE::kNothing;
	}

	const DC::MotionMatchingSettings* pSettings = params.m_set.GetSettings();
	const MotionMatchingSet* pMotionSet = params.m_set.GetSetArtItem();

	if (!pMotionSet || !pSettings)
	{
		return MAYBE::kNothing;
	}

	if (!params.m_pDesiredTrajectory)
	{
		return MAYBE::kNothing;
	}

	const AnimTrajectory& trajectory = *params.m_pDesiredTrajectory;

	const Clock* pClock = params.m_pClock;

	if (!pClock->TimePassed(TimeDelta(params.m_transitionalInterval), params.m_previousMatchTime)
		&& TRUE_IN_FINAL_BUILD(!params.m_debug))
	{
		return MAYBE::kNothing;
	}

	AnimSample curSample;
	const ArtItemAnim* pCurAnim = nullptr;
	if (params.m_maybeCurSample.Valid())
	{
		curSample = params.m_maybeCurSample.Get();
		pCurAnim = curSample.Anim().ToArtItem();
	}

	Maybe<AnimSample> result = MAYBE::kNothing;

	bool nextSampleInSet = false;
	const bool animInSet = params.m_maybeCurSample.Valid() && pMotionSet->IsAnimInSet(curSample, &nextSampleInSet);
	const bool forceAnimChange = params.m_animChangeMode == AnimChangeMode::kForced;

	MMSearchParams searchParams = params.m_searchParams;
	searchParams.AddActiveLayer(SID("default"), 0.0f);

	// hook for adding active layers
	if (params.m_input.m_pLayerFunc)
	{
		params.m_input.m_pLayerFunc(pSelf, searchParams, params.m_debug);
	}

	for (int i = 0; i < searchParams.m_numGoalLocators; ++i)
	{
		searchParams.m_goalLocs[i].m_loc = params.m_charLocatorPs.UntransformLocator(searchParams.m_goalLocs[i].m_loc);
	}

	if (animInSet && !params.m_forceExternalPose && !g_motionMatchingOptions.m_forceExternalPoseRating)
	{
		float naturalBias = 0.0f;

		if (nextSampleInSet)
		{
			naturalBias = pSettings->m_naturalBias;
		}

		AnimSampleBiased curSampleBiased = AnimSampleBiased(curSample, naturalBias);
		if (FALSE_IN_FINAL_BUILD(params.m_debug))
		{
			const Locator charLocatorWs = params.m_parentSpace.TransformLocator(params.m_charLocatorPs);

			Maybe<AnimSample> extraDebugSample = GetExtraDebugSample();

			pMotionSet->DebugClosestSamplesExisting(searchParams,
													curSampleBiased,
													trajectory,
													params.m_desiredGroupId,
													params.m_extraBias,
													g_motionMatchingOptions.m_drawOptions.m_numSamplesToDebug,
													g_motionMatchingOptions.m_drawOptions.m_debugIndex,
													extraDebugSample,
													charLocatorWs,
													kColorRed);
		}

		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (pSelf && pSelf->IsKindOf(SID("Player")))
			{
				Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::MatchFromInput branch 1\n");
			}
		}

		result = pMotionSet->FindClosestSampleExisting(searchParams,
													   curSampleBiased,
													   trajectory,
													   params.m_desiredGroupId,
													   params.m_extraBias);

		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (pSelf && pSelf->IsKindOf(SID("Player")))
			{
				if (result.Valid())
				{
					char debugText[1024];
					snprintf(debugText, sizeof(debugText), "branch 1 result:%s\n", DevKitOnly_StringIdToString(result.Get().GetDbgNameId()));
					Character::AppendDebugTextInFinal(debugText);
				}
				else
				{
					Character::AppendDebugTextInFinal("branch 1 result invalid\n");
				}				
			}
		}
	}
	else if (params.m_pCharPose && IsPoseValidForPoseDef(*params.m_pCharPose, pMotionSet->m_pSettings->m_pose))
	{
		const bool transitionAnimValid = params.m_allowExternalTransitionAnims && params.m_maybeCurSample.Valid()
										 && !curSample.Anim().ToArtItem()->IsLooping();

		const bool curLooping = pCurAnim ? pCurAnim->IsLooping() : false;
		const float animDuration = GetDuration(pCurAnim);
		const float minBlendTime = pSettings->m_blendTimeSec;
		const float curTime = pCurAnim ? (curSample.Phase() * animDuration) : animDuration;
		const bool forceChange = !pCurAnim || (!curLooping && ((curTime + minBlendTime) >= animDuration)) || forceAnimChange;


		I32F poseBias = 0; // might want to hook this up in some nice extensible way (eventually)?
		I32F poseGroupId = params.m_desiredGroupId;

		if (FALSE_IN_FINAL_BUILD(params.m_debug))
		{
			const Locator charLocatorWs = params.m_parentSpace.TransformLocator(params.m_charLocatorPs);

			Maybe<AnimSample> extraDebugSample = GetExtraDebugSample();

			pMotionSet->DebugClosestSamplesExternal(searchParams,
													*params.m_pCharPose,
													params.m_maybeCurSample,
													trajectory,
													poseGroupId,
													poseBias,
													params.m_extraBias,
													g_motionMatchingOptions.m_drawOptions.m_numSamplesToDebug,
													g_motionMatchingOptions.m_drawOptions.m_debugIndex,
													extraDebugSample,
													forceChange ? pSettings->m_externalFallbacks : nullptr,
													charLocatorWs,
													kColorRed);
		}

		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (pSelf && pSelf->IsKindOf(SID("Player")))
			{
				Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::MatchFromInput branch 2\n");
			}
		}

		const Maybe<AnimSample> toolResult = pMotionSet->FindClosestSampleExternal(searchParams,
																				   *params.m_pCharPose,
																				   trajectory,
																				   poseGroupId,
																				   poseBias,
																				   params.m_extraBias);

		// NB: We can potentially do several comparison modes here, (pure goal vs. goal, goal with extras such as bias and grouping,
		// a wholesale cost comparison) that we switch on programatically, potentially as a user control
		// for now all our known cases of concern (ellie tense idle -> crouch idle, melee chokehold kickoff -> run, and climb up -> run)
		// work best with goal + extras comparison which most accurately emulates the behavior as if the external animation sample
		// already existed inside the motion matching set. Other potential improvements include controlling the implied bias
		// and grouping of the external animation (poaseBias, poseGroupId), as well as adding a external cost bias modifier

		const float curGoalCost = transitionAnimValid ? pMotionSet->ComputeGoalCostWithExtras(searchParams,
																							  curSample,
																							  poseBias,
																							  poseGroupId,
																							  trajectory)
													  : kLargeFloat;

		// note that passing in poseBias and poseGroupId here should be effectively useless, since internally this should grab
		// the real bias and group values from the sample table entry
		const float toolGoalCost = toolResult.Valid() ? pMotionSet->ComputeGoalCostWithExtras(searchParams,
																							  toolResult.Get(),
																							  false,
																							  0,
																							  trajectory)
													  : kLargeFloat;

		float resultCost = 0.0f;

		if ((curGoalCost < toolGoalCost) && !forceChange)
		{
			result	   = params.m_maybeCurSample.Get();
			resultCost = curGoalCost;

			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (pSelf && pSelf->IsKindOf(SID("Player")))
				{
					char debugText[1024];
					const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
					snprintf(debugText, sizeof(debugText), "[FN:%lld], curGoalCost < toolGoalCost, %s\n", currFN, DevKitOnly_StringIdToString(params.m_maybeCurSample.Get().GetDbgNameId()));
					Character::AppendDebugTextInFinal(debugText);
				}
			}
		}
		else if (toolResult.Valid())
		{
			result	   = toolResult;
			resultCost = toolGoalCost;

			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (pSelf && pSelf->IsKindOf(SID("Player")))
				{
					char debugText[1024];
					const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
					snprintf(debugText, sizeof(debugText), "[FN:%lld], toolResult.Valid(), %s\n", currFN, DevKitOnly_StringIdToString(toolResult.Get().GetDbgNameId()));
					Character::AppendDebugTextInFinal(debugText);
				}
			}
		}
		else
		{
			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (pSelf && pSelf->IsKindOf(SID("Player")))
				{
					Character::AppendDebugTextInFinal("branch 2 result invalid\n");
				}
			}
		}

		if (pSettings && pSettings->m_externalFallbacks && forceChange)
		{
			for (const DC::MotionMatchExternalFallback* pFallback : *pSettings->m_externalFallbacks)
			{
				const ArtItemAnimHandle hAnim = AnimMasterTable::LookupAnim(pMotionSet->m_skelId,
																			pMotionSet->m_hierarchyId,
																			pFallback->m_animName);

				AnimSample fallbackSample = AnimSample(hAnim, 0.0f);

				const float fallbackCost = pMotionSet->ComputeGoalCostWithExtras(searchParams,
																				 fallbackSample,
																				 poseBias,
																				 poseGroupId,
																				 trajectory);

				if (fallbackCost < (resultCost + pFallback->m_costBias))
				{
					result	   = fallbackSample;
					resultCost = fallbackCost;
				}
			}
		}
	}
	else 
	{
		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (pSelf && pSelf->IsKindOf(SID("Player")))
			{
				Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::MatchFromInput branch 3\n");
			}
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(CharacterMotionMatchLocomotion, PostRootLocatorUpdate)
{
	PROFILE(Animation, MML_SubsystemPostRootLocatorUpdate);

	NdGameObject* pGo = GetOwnerGameObject();
	if (!pGo)
	{
		return;
	}

	CalculateProceduralMotion(*pGo);

	// Override saved align with the current align, because saved align for MM actions can drift while the instance is blending out
	if (IsTop())
	{
		BoundFrame currAlign = pGo->GetBoundFrame();

		InstanceIterator itInst = GetInstanceStart();
		while (itInst)
		{
			if (itInst->HasSavedAlign())
				itInst->SetSavedAlign(currAlign);
			itInst++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector GetAccelerationFromLocoHistoryPs(const ICharacterLocomotion::LocomotionHistory& locoHist)
{
	if (locoHist.GetCount() < 2)
		return kZero;

	ICharacterLocomotion::LocomotionHistory::ConstReverseIterator rItr = locoHist.RBegin();

	const ICharacterLocomotion::LocomotionState& s1 = *rItr;
	const Vector v1Ps = s1.m_velPs;
	const TimeFrame t1 = s1.m_time;

	while ((rItr->m_time == t1) && (rItr != locoHist.REnd()))
	{
		rItr++;
	}

	if (rItr == locoHist.REnd())
		return kZero;

	const ICharacterLocomotion::LocomotionState& s0 = *rItr;

	const Vector v0Ps = s0.m_velPs;
	const TimeFrame t0 = s0.m_time;

	const Vector dv = (v1Ps - v0Ps);
	const TimeFrame dt = t1 - t0;

	ANIM_ASSERT(dt > Seconds(0.0f));

	const Vector accelWs = dv / ToSeconds(dt);

	return accelWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(CharacterMotionMatchLocomotion, PostAnimBlending)
{
	NdGameObject* pGo = GetOwnerGameObject();
	if (!pGo)
	{
		return;
	}

	NdGameObject& self = *pGo;

	UpdateAndApplyRootRotation(self);

	const Clock* pClock = self.GetClock();
	const TimeDelta dt = self.GetClock()->GetDeltaTimeFrame();

	if (!pClock->IsPaused())
	{
		const Quat curRotationPs = self.GetRotationPs();
		const Quat delta = curRotationPs * Conjugate(m_prevRotationPs);
		// NB: m_strafeBlend might be negative (i.e. unset) here but in this case using 0.0 is fine
		const float strafeBlend = Limit01(m_strafeBlend);

		m_matchRotationPs = delta * m_strafeRotationDeltaPs * Conjugate(m_proceduralRotationDeltaPs) * m_matchRotationPs;
		m_matchRotationPs = Slerp(curRotationPs, m_matchRotationPs, strafeBlend);
		m_matchRotationPs = AdjustRotationToUprightPs(m_matchRotationPs);
		m_prevRotationPs = curRotationPs;

		if (FALSE_IN_FINAL_BUILD(self.IsKindOf(g_type_NavCharacter) && g_navCharOptions.m_disableMovement))
		{
			for (LocomotionState& locoState : m_locomotionHistory)
			{
				locoState.m_time += dt;
			}
		}
		else
		{
			if (m_locomotionHistory.IsFull())
			{
				m_locomotionHistory.Drop(1);
			}

			LocomotionState locoState = CreateLocomotionState(&self);

			if (g_motionMatchingOptions.m_proceduralOptions.m_enableProceduralAlignTranslation && !m_hackUpdateMotionModel)
			{
				locoState.m_alignPs = ApplyProceduralTranslationPs(locoState.m_alignPs);
			}

			m_locomotionHistory.Enqueue(locoState);
		}
	}

	if (!g_motionMatchingOptions.m_proceduralOptions.m_enableProceduralAlignTranslation || m_hackUpdateMotionModel)
	{
		const Point posPs = self.GetTranslationPs();
		const Vector velPs = self.IsKindOf(SID("PlayerBase")) ? self.GetBaseVelocityPs() : self.GetVelocityPs();
		const Vector accelPs = GetAccelerationFromLocoHistoryPs(m_locomotionHistory);

		m_motionModelPs.SetPos(posPs);
		m_motionModelPs.SetVel(velPs);
		m_motionModelPs.SetAccel(accelPs);

		m_hackUpdateMotionModel = false;
	}

	if (CharacterLocomotionInterface* pInterface = GetInterface())
	{
		pInterface->PostAnimBlending(pGo, m_motionModelPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_blendedSettings.Relocate(deltaPos, lowerBound, upperBound);
	m_locomotionHistory.Relocate(deltaPos, lowerBound, upperBound);

	m_animTrajectoryOs.Relocate(deltaPos, lowerBound, upperBound);

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MmStateContributionBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	virtual ~MmStateContributionBlender() override {}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		switch (pInstance->GetStateName().GetValue())
		{
		case SID_VAL("s_motion-match-locomotion"):
			*pDataOut = 1.0f;
			break;
		default:
			*pDataOut = 0.0f;
			break;
		}

		return true;
	}

	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override
	{
		const float factor = Lerp(leftData, rightData, motionFade);
		return factor;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::EventHandler(Event& event)
{
	if (IsExiting())
		return;

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("PostAlignMovement"):
		if (m_hasUpdated)
		{
			const NdGameObject* pOwner = GetOwnerGameObject();
			const Locator& parentSpace = pOwner ? pOwner->GetParentSpace() : kIdentity;

			const Point modelPosWs = parentSpace.TransformPoint(m_motionModelPs.GetPos());
			const Point finalPosWs   = event.Get(0).GetLocator().Pos();
			const Point desiredPosWs = event.Get(1).GetLocator().Pos();
			const bool forceUpdate = event.Get(2).GetBool();

			if (forceUpdate)
			{
				m_motionModelPs.SetPos(parentSpace.UntransformPoint(finalPosWs));
				return;
			}

#if 1
			Point closestWs = modelPosWs;
			if (!m_motionModelPs.IsFollowingPath())
			{
				const float distToAlignDelta = DistPointLine(modelPosWs, desiredPosWs, finalPosWs, &closestWs);
			}

			const float clampDist = m_motionModelPs.GetProceduralClampDist();
			const Vector toFinalWs = VectorXz(finalPosWs - closestWs);
			const Vector anchorOffsetWs = LimitVectorLength(toFinalWs, 0.0f, clampDist);

			const Point anchorPosWs = closestWs + anchorOffsetWs;

			const Vector deltaWs = finalPosWs - anchorPosWs;
			const Vector deltaPs = parentSpace.UntransformVector(deltaWs);

			//MsgCon("desiredPosWs: %s\n", PrettyPrint(desiredPosWs));
			//MsgCon("modelPosWs: %s %s\n", PrettyPrint(modelPosWs), PrettyPrint(m_motionModelPs.GetPos()));
			//MsgCon("deltaPs: %fm\n", float(Length(deltaPs)));

			//ANIM_ASSERTF(Length(deltaPs) < 100.0f,
			//			 ("[%s] Bad PostAlignMovement: %s [final: %s] [desired: %s] [model: %s] [anchor: %s]",
			//			  pOwner->GetName(),
			//			  PrettyPrint(deltaWs),
			//			  PrettyPrint(finalPosWs),
			//			  PrettyPrint(desiredPosWs),
			//			  PrettyPrint(modelPosWs),
			//			  PrettyPrint(anchorPosWs)));

#else
			const Vector deltaWs = finalPosWs - desiredPosWs;
			const Vector deltaPs = parentSpace.UntransformVector(deltaWs);
#endif

			if (false)
			{
				g_prim.Draw(DebugCross(desiredPosWs, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugCross(finalPosWs, 0.1f, kColorCyan, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugCross(modelPosWs, 0.1f, kColorYellow, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugCircle(modelPosWs, kUnitYAxis, clampDist, kColorYellow, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugSphere(anchorPosWs, 0.01f, kColorYellowTrans, PrimAttrib(kPrimEnableWireframe)));
			}

#if 0
			MmStateContributionBlender blender;
			const AnimStateLayer* pBaseLayer = pOwner->GetAnimControl()->GetBaseStateLayer();

			const float mmStateContribution = blender.BlendForward(pBaseLayer, 0.0f);

			const Vector scaledDeltaPs = deltaPs * Limit01(mmStateContribution);

			//g_prim.Draw(DebugCross(anchorPosWs, 0.1f, kColorOrange, kPrimEnableHiddenLineAlpha));
			//MsgCon("Anim Error: %0.3fm\n", (float)Length(scaledDeltaPs));

			m_motionModelPs.SetAnimationError(scaledDeltaPs);
#else
			m_motionModelPs.SetAnimationError(deltaPs);
#endif

			Scalar modelErrDist;
			const Vector modelErrDirWs = AsUnitVectorXz(modelPosWs - finalPosWs, kZero, modelErrDist);
			
			if (modelErrDist > NDI_FLT_EPSILON)
			{
				const Vector modelErrWs = VectorXz(modelPosWs - finalPosWs);
				const Vector constrainedErrWs = modelErrDirWs * Min(modelErrDist, clampDist * 1.1f);
				const Point constrainedModelWs = PointFromXzAndY(finalPosWs + constrainedErrWs, modelPosWs);
				const Point constrainedModelPs = parentSpace.UntransformPoint(constrainedModelWs);

				m_motionModelPs.SetPos(constrainedModelPs);
			}
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::GetPlayerMoveControl(float* pMoveCtrlPct) const
{
	*pMoveCtrlPct = 1.0f;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetNavAdjustRadius(float radius) const
{
	if (m_motionModelPs.IsFollowingPath() && (LengthSqr(m_motionModelPs.GetDesiredVel()) > 0.0f))
	{
		const bool hasSharpPath = m_motionModelPs.GetNumPathCorners() > 0;
		const bool hasLongPath = m_motionModelPs.GetPathLength() > 2.0f;

		if (hasSharpPath || hasLongPath)
		{
			static const float kMinNavAdjustRadius = 0.1f;
			const float pathOffset = Max(m_motionModelPs.GetPathOffsetDist(), kMinNavAdjustRadius);

			radius = Min(radius, pathOffset);
		}
	}

	return radius;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::RequestRefreshAnimState(const FadeToStateParams* pFadeToStateParams,
															 bool allowStompOfInitialBlend)
{
	if (!allowStompOfInitialBlend && GetActionState() != NdSubsystemAnimAction::ActionState::kTop)
	{
		return;
	}

	m_requestRefreshAnimState = true;

	if (pFadeToStateParams)
	{
		m_refreshFadeToStateParams = *pFadeToStateParams;
		m_hasRefreshParams = true;
	}
	else
	{
		m_hasRefreshParams = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::MotionMatchingSettings* CharacterMotionMatchLocomotion::GetSettings() const
{
	return m_set.GetSettings();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 CharacterMotionMatchLocomotion::GetSetId() const
{
	return m_set.GetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::MotionModelSettings CharacterMotionMatchLocomotion::GetMotionSettings() const
{
	const NdGameObject* pGo = GetOwnerGameObject();
	const TimeFrame curTime = pGo ? pGo->GetClock()->GetCurTime() : TimeFramePosInfinity();

	return m_blendedSettings.Get(curTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CharacterMotionMatchLocomotion::ApproximateStoppingPositionPs() const
{
	const NdGameObject* pGo = GetOwnerGameObject();
	const TimeFrame curTime = pGo ? pGo->GetClock()->GetCurTime() : TimeFramePosInfinity();

	BlendedMotionModelSettingsSupplier blendedSettings(m_blendedSettings, curTime);
	MotionModelIntegrator integratorPs = MotionModelIntegrator(m_motionModelPs, &blendedSettings);
	return ::ApproximateStoppingPosition(integratorPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::DebugUpdate(const RecordedState* pPlaybackData) const
{
	STRIP_IN_FINAL_BUILD;

	const Character* pChar = GetOwnerCharacter();

	const bool validToUpdate = IsValidToUpdate();

	if (!validToUpdate)
		return;

	const Locator& parentSpace = pChar->GetParentSpace();

	const bool onlyOneSelected = DebugSelection::Get().IsOnlyProcessSelected(pChar);
	const bool selected = DebugSelection::Get().IsProcessOrNoneSelected(pChar);
	const float dt = pChar->GetClock()->GetLastFrameDeltaTimeInSeconds();
	const TimeFrame curTime = pChar->GetClock()->GetCurTime();
	const bool paused = pChar->GetClock()->IsPaused();

	if (false)
	{
		const DC::MotionModelSettings motionSettings = m_blendedSettings.Get(curTime);

		const Vector desMoveDirPs = SafeNormalize(m_motionModelPs.GetDesiredVel(), kZero);
		const Maybe<Vector> desFaceDirPs = m_motionModelPs.GetUserFacing();

		MsgCon("Settings:\n");
		MsgCon("  max speed: %f\n", GetMaxSpeedForDirection(&motionSettings, desMoveDirPs, desFaceDirPs));
		MsgCon("  vel k:     %f\n", GetVelKForDirection(&motionSettings, desMoveDirPs, desFaceDirPs));
	}

	if ((m_input.m_mmParams.m_numActiveLayers > 0) && selected && g_motionMatchingOptions.m_drawOptions.m_printActiveLayers)
	{
		MsgCon("%d Active Layers:\n", m_input.m_mmParams.m_numActiveLayers);
		for (int i = 0; i < m_input.m_mmParams.m_numActiveLayers; ++i)
		{
			MsgCon("  %s @ %f\n",
				   DevKitOnly_StringIdToString(m_input.m_mmParams.m_activeLayers[i]),
				   m_input.m_mmParams.m_layerCostModifiers[i]);
		}
	}

	if (g_motionMatchingOptions.m_drawOptions.m_drawMatchLocator && selected)
	{
		const Locator matchLocPs = GetMatchLocatorPs(*pChar);
		g_prim.Draw(DebugCoordAxes(parentSpace.TransformLocator(matchLocPs), 1.0f, kPrimEnableHiddenLineAlpha, 5.0f));
	}

	if (g_motionMatchingOptions.m_drawOptions.m_drawMotionModel && selected)
	{
		m_motionModelPs.DebugDraw(parentSpace);
	}

	if (const CharacterLocomotionInterface* pLocomotionInterface = GetInterface())
	{
		pLocomotionInterface->DebugDraw(pChar, m_motionModelPs);
	}

	if (g_motionMatchingOptions.m_drawOptions.m_drawLocomotion && paused && onlyOneSelected)
	{
		if (pPlaybackData == nullptr)
		{
			const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();

			MsgConNotRecorded("-----------------------------------------\n");
			MsgConNotRecorded("%s - %s [%s]%s\n",
							  DevKitOnly_StringIdToString(m_set.GetId()),
							  DevKitOnly_StringIdToString(m_set.MotionMatchSetArtItemId()),
							  DevKitOnly_StringIdToString(pChar->GetUserId()),
							  (pMotionSet && !pMotionSet->HasValidIndices()) ? " WARNING: INVALID INDICES (Please Rebuild)" : "");

			InputData input;

			if (CharacterLocomotionInterface* pLocomotionInterface = GetInterface())
			{
				pLocomotionInterface->GetInput(&input);
			}
			else
			{
				input = m_input;
			}

			if (g_motionMatchingOptions.m_drawOptions.m_useStickForDebugInput)
			{
				U32 joypadId = DC::kJoypadPadReal1;
				NdPlayerJoypad joypad;
				joypad.Init(joypadId, 0, 0, 0.3f, 0.95f, false);
				joypad.Update();
				input.m_desiredVelocityDirPs = parentSpace.UntransformVector(joypad.GetStickWS(NdPlayerJoypad::kStickMove));
			}

			ScopedTempAllocator alloc(FILE_LINE_FUNC);
			AnimTrajectory dummyTraj(m_locomotionHistory.GetMaxSize() + m_set.GetNumTrajectorySamples() + 1);

			MatchParams params = CreateMatchParams(input, dt, &dummyTraj, true);

			if (input.m_animChangeMode != AnimChangeMode::kSuppressed)
			{
				MatchFromInput(params);
			}
			else
			{
				MsgConNotRecorded(" MATCHING SUPPRESSED\n");
			}
		}
		else if (pPlaybackData && pPlaybackData->m_valid)
		{
			MMSetPtr playbackSet(pPlaybackData->m_input.m_setId);

			const MotionMatchingSet* pMotionSet = playbackSet.GetSetArtItem();

			MsgConNotRecorded("-----------------------------------------\n");
			MsgConNotRecorded("%s - %s [%s]%s\n",
							  DevKitOnly_StringIdToString(playbackSet.GetId()),
							  DevKitOnly_StringIdToString(playbackSet.MotionMatchSetArtItemId()),
							  DevKitOnly_StringIdToString(pChar->GetUserId()),
							  (pMotionSet && !pMotionSet->HasValidIndices()) ? " WARNING: INVALID INDICES (Please Rebuild)" : "");


			MatchParams params = MatchParams(pPlaybackData->m_input);

			params.m_pSelf		 = pChar;
			params.m_parentSpace = parentSpace;

			// yuck
			params.m_searchParams = pPlaybackData->m_input.m_mmParams;

			params.m_pClock	   = &pPlaybackData->m_clock;
			params.m_pCharPose = &pPlaybackData->m_pose;
			params.m_set	   = pPlaybackData->m_set;

			params.m_previousMatchTime = pPlaybackData->m_previousMatchTime;
			params.m_charLocatorPs	   = pPlaybackData->m_charLocatorPs;
			params.m_maybeCurSample	   = pPlaybackData->m_maybeCurSample;
			params.m_desiredGroupId	   = pPlaybackData->m_desiredGroupId;

			if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_overrideMirrorMode != (I64)MMMirrorMode::kInvalid))
			{
				params.m_searchParams.m_mirrorMode = MMMirrorMode(g_motionMatchingOptions.m_overrideMirrorMode);
			}

			params.m_allowExternalTransitionAnims = pPlaybackData->m_allowExternalTransitions;

			ScopedTempAllocator alloc(FILE_LINE_FUNC);
			AnimTrajectory* pTrajectory = NDI_NEW AnimTrajectory(pPlaybackData->m_traj.Capacity());
			for (const AnimTrajectorySample& recSample : pPlaybackData->m_traj)
			{
				pTrajectory->Add(recSample);
			}
			params.m_pDesiredTrajectory = pTrajectory;

			params.m_debug = true;

			if (pPlaybackData->m_animChangeMode != AnimChangeMode::kSuppressed)
			{
				MatchFromInput(params);
			}
			else
			{
				MsgConNotRecorded(" MATCHING SUPPRESSED\n");
			}
		}
	}
	else if (selected && g_motionMatchingOptions.m_drawOptions.m_drawTrajectorySamples)
	{
		InputData input = m_input;

		if (g_motionMatchingOptions.m_drawOptions.m_useStickForDebugInput)
		{
			U32 joypadId = DC::kJoypadPadReal1;
			NdPlayerJoypad joypad;
			joypad.Init(joypadId, 0, 0.3f, 0.95f, false);
			joypad.Update();
			input.m_desiredVelocityDirPs = parentSpace.UntransformVector(joypad.GetStickWS(NdPlayerJoypad::kStickMove));
		}

		ScopedTempAllocator alloc(FILE_LINE_FUNC);
		AnimTrajectory dummyTraj(m_locomotionHistory.GetMaxSize() + m_set.GetNumTrajectorySamples() + 1);

		Maybe<AnimSample> maybeCurSample = GetCurrentAnimSample();
		const Locator baseMatchLocPs	 = GetMatchLocatorPs(*pChar);

		const Locator futureMatchLoc = GetFutureLocatorPs(baseMatchLocPs,
														  pChar->GetSkeletonId(),
														  maybeCurSample,
														  dt * m_timeScale);

		CreateTrajectory(&dummyTraj, input, futureMatchLoc, true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Color CharacterMotionMatchLocomotion::GetQuickDebugColor() const
{
	return !m_set.IsValid() ? kColorRed : ParentClass::GetQuickDebugColor();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::GetQuickDebugText(IStringBuilder* pText) const
{
	pText->append_format("[%s] Set: %s blend: %.3f",
						 GetActionStateStr(GetActionState()),
						 DevKitOnly_StringIdToString(m_set.GetId()),
						 GetBlend());

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::GetAnimControlDebugText(const AnimStateInstance* pInstance,
															 IStringBuilder* pText) const
{
	const DC::AnimInstanceInfo* pBaseInstInfo = pInstance ? pInstance->GetAnimInstanceInfo() : nullptr;
	const DC::AnimTopInfo* pBaseTopInfo = pInstance ? pInstance->GetAnimTopInfo() : nullptr;
	const DC::AnimCharacterInstanceInfo* pInstInfo = static_cast<const DC::AnimCharacterInstanceInfo*>(pBaseInstInfo);
	const DC::AnimCharacterTopInfo* pTopInfo = static_cast<const DC::AnimCharacterTopInfo*>(pBaseTopInfo);
	const StringId64 setId = pInstInfo ? pInstInfo->m_locomotion.m_motionMatchSetId : m_set.GetId();
	const float speedScale = pTopInfo ? pTopInfo->m_motionMatchLocomotion.m_speedScale : -1.0f;

	pText->append_format("%s ([%s] Set: %s blend: %.3f speed: %0.3f)",
						 GetName(),
						 GetActionStateStr(GetActionState()),
						 DevKitOnly_StringIdToString(setId),
						 GetBlend(),
						 speedScale);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<AnimSample> CharacterMotionMatchLocomotion::GetCurrentAnimSample() const
{
	PROFILE(Animation, MML_GetCurrentAnimSample);

	Maybe<AnimSample> ret = MAYBE::kNothing;
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	if (IsUnattached())
	{
		const NdGameObject* pOwner = GetOwnerGameObject();
		const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;
		const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
		const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

		if (pCurInstance && pCurInstance->GetPhaseAnimArtItem().ToArtItem())
		{
			ret = AnimSample(pCurInstance);
		}
	}
	else
	{
		InstanceIterator instItr = GetInstanceStart();
		const AnimStateInstance* pInst = instItr;
		while (instItr)
		{
			pInst = instItr;
			instItr++;
		}

		if (pInst)
		{
			if (pInst->GetAnimStateSnapshot().m_originalPhaseAnimName != INVALID_STRING_ID_64 && pSettings
				&& pSettings->m_allowOverlayRemaps)
			{
				if (pInst->GetOriginalPhaseAnimArtItem().ToArtItem())
				{
					ret = AnimSample(pInst, true);
				}
			}
			else if (pInst->GetPhaseAnimArtItem().ToArtItem())
			{
				ret = AnimSample(pInst);
			}
		}
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetProceduralLookAheadTime(const Maybe<AnimSample>& sample) const
{
	if (!sample.Valid())
	{
		return -1.0f;
	}

	const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	if (!pMotionSet || !pSettings)
	{
		return -1.0f;
	}

	if (m_animTrajectoryOs.IsEmpty())
	{
		return -1.0f;
	}

	const float rotationTime = pSettings->m_proceduralRotationTime;

	const AnimSample& s = sample.Get();
	const ArtItemAnim* pAnim = s.Anim().ToArtItem();
	const float duration = pAnim ? GetDuration(pAnim) : 0.0f;
	const float remTime = (pAnim && pAnim->IsLooping()) ? kLargeFloat : ((1.0f - s.Phase()) * duration);

	float maxTimeScale = pSettings->m_proceduralLookAheadTimeScale;  
	
	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_lookAheadTimeScale >= 0.0f))
	{
		maxTimeScale = g_motionMatchingOptions.m_proceduralOptions.m_lookAheadTimeScale;
	}

	const float desiredMaxTime = Min(pMotionSet->m_pSettings->m_goals.m_maxTrajSampleTime * maxTimeScale, remTime);
	const float baseFutureTime = Max(desiredMaxTime, rotationTime);

	float futureTime = baseFutureTime;

	if (const CharacterLocomotionInterface* pInterface = GetInterface())
	{
		futureTime = pInterface->LimitProceduralLookAheadTime(futureTime, m_animTrajectoryOs);
	}

	return futureTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetStrafeLockAngleDeg() const
{
	if (m_input.m_strafeLockAngleDeg >= 0.0f)
		return m_input.m_strafeLockAngleDeg;

	const DC::MotionMatchingSettings* pSettings = GetSettings();

	return pSettings ? pSettings->m_strafeLockAngleDeg : -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::CalculateProceduralMotion(NdGameObject& self)
{
	PROFILE(Animation, MML_ApplyProceduralMotion);

	const Locator& parentSpace = self.GetParentSpace();

	const Locator finalAlignWs = self.GetLocator();
	const Locator finalAlignPs = self.GetLocatorPs();

	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
	if (!pSettings)
	{
		return;
	}

	BlendedMotionModelSettingsSupplier blendedSettings(m_blendedSettings, self.GetClock()->GetCurTime());
	CharacterLocomotionInterface* pLocomotionInterface = GetInterface();

	Maybe<AnimSample> sample = GetCurrentAnimSample();
	const float dt = self.GetClock()->GetDeltaTimeInSeconds();

	float desiredSpeedScale = 1.0f;

	m_proceduralRotationDeltaPs = kIdentity;
	m_strafeRotationDeltaPs		= kIdentity;
	m_proceduralTransDeltaPs	= kZero;

	const float futureTime = GetProceduralLookAheadTime(sample);

	if (futureTime >= 0.0f)
	{
		const AnimTrajectory& futureTrajOs = m_animTrajectoryOs;

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
								 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
		{
			const Locator trajBaseWs = parentSpace.TransformLocator(m_lastMatchLocPs);
			g_prim.Draw(DebugCoordAxes(trajBaseWs, 1.0f, kPrimEnableHiddenLineAlpha, 4.0f));
			futureTrajOs.DebugDraw(trajBaseWs, kPrimDuration1FramePauseable);
		}

		const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();
		const float stoppingFaceDist = pMotionSet->m_pSettings->m_goals.m_stoppingFaceDist;
		Maybe<MMLocomotionState> maybeFutureState = ComputeLocomotionStateInFuture(sample.Get(), futureTime, stoppingFaceDist);
		const AnimTrajectorySample futureModelStateOs = futureTrajOs.Get(futureTime);

		if (maybeFutureState.Valid() && futureModelStateOs.IsPositionValid())
		{
			static const float kProceduralRotationSwitchDistance = 0.75f;

			const MMLocomotionState& futureAnimStateOs = maybeFutureState.Get();

			const bool modelMoves = DistXz(futureTrajOs.GetTail().GetPosition(), kOrigin)
									> kProceduralRotationSwitchDistance;

			if (modelMoves)
			{
				CalculateProceduralRotation_Moving(self,
												   finalAlignPs,
												   m_lastMatchLocPs,
												   futureAnimStateOs,
												   futureModelStateOs);
			}
			else
			{
				CalculateProceduralRotation_Standing(self,
													 finalAlignPs,
													 m_lastMatchLocPs,
													 futureAnimStateOs,
													 futureModelStateOs);
			}

			CalculateProceduralRotation_Strafing(self,
												 finalAlignPs,
												 m_lastMatchLocPs,
												 futureAnimStateOs,
												 futureModelStateOs);

			// This should be done in pre anim update
			if (futureModelStateOs.IsPositionValid())
			{
				const Vector toAnim = VectorXz(AsVector(futureAnimStateOs.m_alignOs.Pos()));
				const Vector toDesired = VectorXz(AsVector(futureModelStateOs.GetPosition()));

				float animSpeed = Length(toAnim) / futureTime;
				float desiredSpeed = Length(toDesired) / futureModelStateOs.GetTime();

				float desiredScale = 1.0f;
				if (animSpeed > 0.0f && desiredSpeed > 0.0f)
				{
					desiredScale = desiredSpeed / animSpeed;
				}
				const float animRate = sample.Get().Rate();
				float totalScale = MinMax(animRate * desiredScale,
										  pSettings->m_minAnimSpeedScale,
										  pSettings->m_maxAnimSpeedScale);

				if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
										 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
				{
					MsgConPauseable("Desired Speed      : %f\n", desiredSpeed);
					MsgConPauseable("Anim Speed         : %f\n", animSpeed);
					MsgConPauseable("Desired Time Scale : %f\n", desiredScale);
					MsgConPauseable("Anim Rate          : %f\n", animRate);
					MsgConPauseable("AnimScale          : %f [%f : %f]\n", totalScale, pSettings->m_minAnimSpeedScale, pSettings->m_maxAnimSpeedScale);
				}

				desiredSpeedScale = totalScale;
			}
		}
	}

	if (IsTop())
	{
		const Point modelPosPs = m_motionModelPs.GetPos();
		const Vector transErrPs = modelPosPs - finalAlignPs.Pos();
		const float speed = Length(m_motionModelPs.GetVel()); //m_motionModelPs.GetSprungSpeed();
		const float maxSpeed = m_motionModelPs.GetMaxSpeed();

		const float speedFactor = (maxSpeed > NDI_FLT_EPSILON)
									  ? Max(Limit01(speed / maxSpeed), m_input.m_softClampAlphaMinFactor)
									  : 1.0f;

		const float alpha = m_input.m_softClampAlphaOverride >= 0.0f ? m_input.m_softClampAlphaOverride
																	 : pSettings->m_transSoftClampAlpha;

		const float tt = Limit01(alpha * speedFactor * dt);

		m_proceduralTransDeltaPs = transErrPs * tt;

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
								 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
		{
			MsgConPauseable("Trans Soft Clamp   : %0.3fm (%0.3fm * %0.4f%s * %0.4f * %0.4f) [%0.1fm/s / %0.1fm/s]\n",
							(float)Length(m_proceduralTransDeltaPs),
							(float)Length(transErrPs),
							alpha,
							m_input.m_softClampAlphaOverride >= 0.0f ? " (o)" : "",
							speedFactor,
							dt,
							speed,
							maxSpeed);

			g_prim.Draw(DebugArrow(finalAlignPs.Pos(), m_proceduralTransDeltaPs, kColorRed, 0.25f, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugArrow(finalAlignPs.Pos(), modelPosPs, kColorRedTrans, 0.5f, kPrimEnableHiddenLineAlpha));
		}
	}

	const float strafeLockAngleDeg = GetStrafeLockAngleDeg();

	if (strafeLockAngleDeg < 0.0f || strafeLockAngleDeg >= 180.0f)
	{
		m_smoothedUserFacingPs = GetLocalZ(finalAlignPs);
	}
	else if (m_smoothedUserFacingPs.Valid() && m_motionModelPs.GetUserFacing().Valid())
	{
		const Vector curPs = m_smoothedUserFacingPs.Get();
		const Vector desPs = m_motionModelPs.GetUserFacing().Get();

		const DC::MotionModelSettings modelSettings = blendedSettings.GetSettings(Seconds(0.0f));
		const float maxTurnDeg = modelSettings.m_turnRateDps * dt;
		const Quat rotDiff = QuatFromVectors(curPs, desPs);
		const Quat rotDelta = LimitQuatAngle(rotDiff, DEGREES_TO_RADIANS(maxTurnDeg));

		const Vector smoothedPs = Rotate(rotDelta, curPs);

		m_smoothedUserFacingPs = smoothedPs;
	}
	else
	{
		m_smoothedUserFacingPs = m_motionModelPs.GetUserFacing();
	}

	if ((m_speedScale < 0.0f) || !pSettings)
	{
		m_speedScale = desiredSpeedScale;
		m_speedScaleSpring.Reset();
	}
	else
	{
		m_speedScale = m_speedScaleSpring.Track(m_speedScale, desiredSpeedScale, dt, pSettings->m_animSpeedScaleSpring);
	}

	if (!g_motionMatchingOptions.m_proceduralOptions.m_enableAnimSpeedScaling)
	{
		m_speedScale = 1.0f;
	}

	if (FALSE_IN_FINAL_BUILD(!g_motionMatchingOptions.m_proceduralOptions.m_enableProceduralAlignRotation))
	{
		m_proceduralRotationDeltaPs = kIdentity;
		m_strafeRotationDeltaPs		= kIdentity;
	}

	AnimControl* pAnimControl = self.GetAnimControl();
	DC::AnimCharacterTopInfo* pTopInfo = pAnimControl ? pAnimControl->TopInfo<DC::AnimCharacterTopInfo>() : nullptr;

	if (pTopInfo)
	{
		pTopInfo->m_motionMatchLocomotion.m_speedScale	 = m_speedScale * m_timeScale;
		pTopInfo->m_motionMatchLocomotion.m_procRotDelta = m_proceduralRotationDeltaPs;
	}

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
							 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
	{
		MsgConPauseable("Desired Scale:  %f\n", desiredSpeedScale);
		MsgConPauseable("Smoothed Scale: %f\n", m_speedScale);

		//MsgConPauseable("Proc Rot Delta: %s\n", PrettyPrint(m_proceduralRotationDeltaPs, kPrettyPrintEulerAngles));
		//MsgConPauseable("Strafe Rot Delta: %s\n", PrettyPrint(m_strafeRotationDeltaPs, kPrettyPrintEulerAngles));

		Vec4 rotAxis;
		float rotAngleRad;
		m_proceduralRotationDeltaPs.GetAxisAndAngle(rotAxis, rotAngleRad);

		MsgCon("Proc rot delta: %0.3fdeg\n", RADIANS_TO_DEGREES(rotAngleRad));
		MsgCon("   %s\n", PrettyPrint(m_proceduralRotationDeltaPs));

		m_strafeRotationDeltaPs.GetAxisAndAngle(rotAxis, rotAngleRad);

		MsgCon("Strafe rot delta: %0.3fdeg\n", RADIANS_TO_DEGREES(rotAngleRad));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::CalculateProceduralRotation_Moving(const NdGameObject& self,
																		const Locator& animBasePs,
																		const Locator& modelBasePs,
																		const MMLocomotionState& futureAnimStateOs,
																		const AnimTrajectorySample& futureModelStateOs)
{
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
	const float rotationTime = pSettings->m_proceduralRotationTime;
	const float dt = self.GetClock()->GetDeltaTimeInSeconds();

	if (futureModelStateOs.IsPositionValid())
	{
		const Vector toAnimOs = VectorXz(AsVector(futureAnimStateOs.m_pathPosOs));
		const Vector toDesiredOs = VectorXz(AsVector(futureModelStateOs.GetPosition()));

		if (LengthSqr(toAnimOs) > Sqr(0.15f) && LengthSqr(toDesiredOs) > Sqr(0.15f))
		{
			const AnimControl* pAnimControl = self.GetAnimControl();
			const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
			const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
			const float motionFade = pCurInstance ? pCurInstance->MotionFade() : 1.0f;

			const Vector toAnimPs = animBasePs.TransformVector(toAnimOs);
			const Vector toDesiredPs = modelBasePs.TransformVector(toDesiredOs);

			const Quat rot = QuatFromVectors(toAnimPs, toDesiredPs);

			if (rotationTime > NDI_FLT_EPSILON)
			{
				const float invNumSteps = dt / rotationTime;
				m_proceduralRotationDeltaPs = Slerp(kIdentity, Pow(rot, invNumSteps), motionFade);
			}
			else
			{
				m_proceduralRotationDeltaPs = rot;
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
								 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
		{
			const Locator& parentSpace	= self.GetParentSpace();
			const Locator finalAlignWs = parentSpace.TransformLocator(animBasePs);

			//const Vector toInterp = Rotate(m_proceduralRotationDeltaPs, toAnim);

			g_prim.Draw(DebugArrow(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f), finalAlignWs.TransformVector(toAnimOs), kColorBlue));
			g_prim.Draw(DebugString(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f) + finalAlignWs.TransformVector(toAnimOs), "toAnim", kColorBlueTrans, 0.5f));

			//g_prim.Draw(DebugArrow(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f), finalAlignWs.TransformVector(toInterp), kColorCyanTrans));
			//g_prim.Draw(DebugString(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f) + finalAlignWs.TransformVector(toInterp), "toInterp", kColorCyanTrans, 0.5f));

			const Vector toDesiredWs = parentSpace.TransformVector(modelBasePs.TransformVector(toDesiredOs));

			g_prim.Draw(DebugArrow(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f), toDesiredWs, kColorYellow));
			g_prim.Draw(DebugString(finalAlignWs.Pos() + Vector(0.0f, 0.25f, 0.0f) + toDesiredWs, "toDesired", kColorYellowTrans, 0.5f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::CalculateProceduralRotation_Standing(const NdGameObject& self,
																		  const Locator& animBasePs,
																		  const Locator& modelBasePs,
																		  const MMLocomotionState& futureAnimStateOs,
																		  const AnimTrajectorySample& futureModelStateOs)
{
	const float dt = self.GetClock()->GetDeltaTimeInSeconds();
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
	const float rotationTime = pSettings->m_proceduralRotationTime;

	const Vector animFacingOs = GetLocalZ(futureAnimStateOs.m_alignOs);
	Vector desiredFacingOs = animFacingOs;

	if (futureModelStateOs.IsFacingValid())
	{
		desiredFacingOs = futureModelStateOs.GetFacingDir();
	}

	const Vector animFacingPs = animBasePs.TransformVector(animFacingOs);
	const Vector desiredFacingPs = modelBasePs.TransformVector(desiredFacingOs);

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
							 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
	{
		const Locator& parentSpace = self.GetParentSpace();
		const Vector animFacingWs = parentSpace.TransformVector(animFacingPs);
		const Vector desiredFacingWs = parentSpace.TransformVector(desiredFacingPs);

		const Point posWs = parentSpace.TransformPoint(animBasePs.Pos());

		g_prim.Draw(DebugArrow(posWs + kUnitYAxis, animFacingWs, kColorRed));
		g_prim.Draw(DebugString(posWs + kUnitYAxis + animFacingWs, "anim-stnd", kColorRed, 0.5f));

		g_prim.Draw(DebugArrow(posWs + kUnitYAxis, desiredFacingWs, kColorGreen));
		g_prim.Draw(DebugString(posWs + kUnitYAxis + desiredFacingWs, "des-stnd", kColorGreen, 0.5f));
	}

	const Quat rot = QuatFromVectors(animFacingPs, desiredFacingPs);

	if (rotationTime > NDI_FLT_EPSILON)
	{
		const float invNumSteps = dt / rotationTime;
		m_proceduralRotationDeltaPs = Pow(rot, invNumSteps);
	}
	else
	{
		m_proceduralRotationDeltaPs = rot;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::CalculateProceduralRotation_Strafing(const NdGameObject& self,
																		  const Locator& animBasePs,
																		  const Locator& modelBasePs,
																		  const MMLocomotionState& futureAnimStateOs,
																		  const AnimTrajectorySample& futureModelStateOs)
{
	const float dt = self.GetClock()->GetDeltaTimeInSeconds();
	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	// Strafe Mode
	float desiredStrafeBlend = 0.0f;

	const bool wantToMove = LengthSqr(m_motionModelPs.GetDesiredVel()) > 0.01f;
	const float kFacingDiffForCorrectionDps = 15.0f;
	const float facingDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(m_motionModelPs.GetFacing(), m_motionModelPs.GetDesiredFacing())));

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
							 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
	{
		MsgCon("facingDiffDeg: %s%f%s / %f deg/s\n",
			   GetTextColorString((facingDiffDeg < kFacingDiffForCorrectionDps) ? kTextColorGreen : kTextColorRed),
			   facingDiffDeg,
			   GetTextColorString(kTextColorNormal),
			   kFacingDiffForCorrectionDps);
	}

	bool wantUserStrafing = m_motionModelPs.ShouldApplyStrafeRotation();
	
	if (wantUserStrafing)
	{
		const Vector modelDirPs = SafeNormalize(modelBasePs.TransformVector(futureModelStateOs.GetFacingDir()), kUnitZAxis);
		const Vector desiredDirPs = SafeNormalize(m_motionModelPs.GetUserFacing().Otherwise(modelDirPs), kUnitZAxis);

		if (Dot(desiredDirPs, modelDirPs) < 0.98f)
		{
			wantUserStrafing = false;
		}
	}

	const bool wantModelCorrection = wantToMove && !pSettings->m_disableMatchRotComp
									 && (facingDiffDeg < kFacingDiffForCorrectionDps);

	if (wantUserStrafing)
	{
		const Locator strafeAlignPs = Locator(animBasePs.Pos(), m_matchRotationPs);
		const Vector animDirPs = SafeNormalize(strafeAlignPs.TransformVector(futureAnimStateOs.m_strafeDirOs), kUnitZAxis);

		const Vector desiredDirPs = SafeNormalize(m_motionModelPs.GetUserFacing().Otherwise(animDirPs), kUnitZAxis);

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
								 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
		{
			const Locator& parentSpace	= self.GetParentSpace();

			const Point modelPosWs	  = parentSpace.TransformPoint(m_motionModelPs.GetPos());
			const Point drawPosWs	  = modelPosWs + Vector(0.0f, 0.5f, 0.0f);
			const Vector animDirWs	  = parentSpace.TransformVector(animDirPs);
			const Vector desiredDirWs = parentSpace.TransformVector(desiredDirPs);

			g_prim.Draw(DebugArrow(drawPosWs, animDirWs, kColorCyan), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(drawPosWs + animDirWs, "strafe-anim", kColorCyanTrans, 0.5f));
			g_prim.Draw(DebugArrow(drawPosWs, desiredDirWs, kColorMagenta), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(drawPosWs + desiredDirWs, "strafe-des", kColorMagentaTrans, 0.5f));

			const Vector modelDirPs = SafeNormalize(modelBasePs.TransformVector(futureModelStateOs.GetFacingDir()), kUnitZAxis);
			const Vector modelDirWs = parentSpace.TransformVector(modelDirPs);
			g_prim.Draw(DebugArrow(drawPosWs, modelDirWs, kColorOrange), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(drawPosWs + Vector(0.0f, 0.1f, 0.0f) + modelDirWs, "model-des", kColorOrangeTrans, 0.5f));
		}

		Quat rot = kIdentity;
		if (m_facingBiasPs.Valid())
		{
			rot = QuatFromVectorsBiased(animDirPs, desiredDirPs, m_facingBiasPs.Get());
		}
		else
		{
			rot = QuatFromVectors(animDirPs, desiredDirPs);
		}

		const float rotationTime = (pSettings->m_strafingRotationTime >= 0.0f) ? pSettings->m_strafingRotationTime
																			   : pSettings->m_proceduralRotationTime;

		if (rotationTime > NDI_FLT_EPSILON)
		{
			const float invNumSteps = dt / rotationTime;
			m_strafeRotationDeltaPs = Pow(rot, invNumSteps);
		}
		else
		{
			m_strafeRotationDeltaPs = rot;
		}

		desiredStrafeBlend = 1.0f;

		// When coming to a stop while strafing rotate the align to match the match locator.
		if (!g_motionMatchingOptions.m_disableMatchLocSpeedBlending)
		{
			const float speed = Length(m_motionModelPs.GetVel());
			if (speed < 0.75f && !wantToMove)
			{
				const float rotBlend = LerpScaleClamp(0.1f, 0.75f, 1.0f, 0.0f, speed);

				m_strafeRotationDeltaPs = Slerp(m_proceduralRotationDeltaPs, m_strafeRotationDeltaPs, rotBlend);
			}
		}
	}
	else if (wantModelCorrection)
	{
		// if we're not strafing and we want to be moving lets try and keep the orientation of the match
		// locator pointing in the same direction as our current (not future) model facing
		const Vector desiredDirPs = m_motionModelPs.GetFacing();
		const Vector animDirPs = GetLocalZ(m_matchRotationPs);

		const float rotationTime = (pSettings->m_matchCorrectionRotationTime >= 0.0f)
									   ? pSettings->m_matchCorrectionRotationTime
									   : pSettings->m_proceduralRotationTime;

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion
								 && DebugSelection::Get().IsProcessOrNoneSelected(&self)))
		{
			const Locator& parentSpace = self.GetParentSpace();

			const Point modelPosWs = parentSpace.TransformPoint(m_motionModelPs.GetPos());
			const Point drawPosWs = modelPosWs + Vector(0.0f, 0.5f, 0.0f);
			const Vector animDirWs = parentSpace.TransformVector(animDirPs);
			const Vector desiredDirWs = parentSpace.TransformVector(desiredDirPs);

			g_prim.Draw(DebugArrow(drawPosWs, animDirWs, kColorOrange), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(drawPosWs + animDirWs, StringBuilder<128>("match-dir (rot: %0.3fs)", rotationTime).c_str(), kColorOrangeTrans, 0.5f));
			g_prim.Draw(DebugArrow(drawPosWs, desiredDirWs, kColorCyan), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(drawPosWs + desiredDirWs, "model-dir", kColorCyanTrans, 0.5f));
		}

		Quat rot = kIdentity;
		if (m_facingBiasPs.Valid())
		{
			rot = QuatFromVectorsBiased(animDirPs, desiredDirPs, m_facingBiasPs.Get());
		}
		else
		{
			rot = QuatFromVectors(animDirPs, desiredDirPs);
		}

		if (rotationTime > NDI_FLT_EPSILON)
		{
			const float invNumSteps = dt / rotationTime;
			m_strafeRotationDeltaPs = Pow(rot, invNumSteps);
		}
		else
		{
			m_strafeRotationDeltaPs = rot;
		}

		desiredStrafeBlend = 1.0f;
	}
	else
	{
		m_strafeRotationDeltaPs = m_proceduralRotationDeltaPs;

		desiredStrafeBlend = 0.0f;
	}

	if (m_strafeBlend < 0.0f)
	{
		m_strafeBlend = desiredStrafeBlend;
	}
	else
	{
		Seek(m_strafeBlend, desiredStrafeBlend, FromUPS(5.0f, dt));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::ApplyBestSample(NdGameObject& self,
													 const AnimSample& bestSample,
													 const DC::MotionMatchingSettings* pSettings,
													 AnimChangeMode mode)
{
	PROFILE(Animation, MML_ApplyBestSample);

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		if (self.IsKindOf(SID("Player")))
		{
			Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::ApplyBestSample\n");
		}
	}

	AnimControl* pAnimControl = self.GetAnimControl();
	AnimStateLayer* pStateLayer = pAnimControl->GetBaseStateLayer();

	if (!pStateLayer)
		return false;

	if (!pStateLayer->HasFreeInstance() && (GetActionState() != ActionState::kUnattached))
		return false;

	const AnimStateInstance* pCurInstance = pStateLayer->CurrentStateInstance();

	const ArtItemAnim* pBestAnim = bestSample.Anim().ToArtItem();

	// Right now this can happen if the animations get unloaded before the motion matching set
	if (nullptr == pBestAnim)
	{
		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			if (self.IsKindOf(SID("Player")))
			{
				char debugText[1024];

				const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

				if (pCurInstance != nullptr)
				{
					snprintf(debugText, sizeof(debugText), "[FN:%lld], pBestAnim is nullptr, pBestAnim.m_dbgNameId: %s, pCurInstance.phaseAnim: %s \n",
						currFN, DevKitOnly_StringIdToString(bestSample.GetDbgNameId()), DevKitOnly_StringIdToString(pCurInstance->GetPhaseAnim()));
				}
				else
				{
					snprintf(debugText, sizeof(debugText), "[FN:%lld], pBestAnim is nullptr, pBestAnim.m_dbgNameId: %s, pCurInstance is nullptr\n",
						currFN, DevKitOnly_StringIdToString(bestSample.GetDbgNameId()));
				}

				Character::AppendDebugTextInFinal(debugText);
			}
		}

		return false;
	}

	DC::AnimCharacterInstanceInfo* pCharInstInfo = pAnimControl->InstanceInfo<DC::AnimCharacterInstanceInfo>();

	const StringId64 curSetId = pCharInstInfo ? pCharInstInfo->m_locomotion.m_motionMatchSetId : INVALID_STRING_ID_64;
	const bool curSetIsMine = curSetId && (curSetId == m_set.GetId());

	Maybe<AnimSample> maybeCurSample = GetCurrentAnimSample();
	const ArtItemAnim* pCurAnim = maybeCurSample.Valid() ? maybeCurSample.Get().Anim().ToArtItem() : nullptr;

	bool sameAnim = false;
	if (maybeCurSample.Valid() && !IsUnattached())
	{
		const AnimSample& curSample = maybeCurSample.Get();
		const MotionMatchingSet* pMotionSet = m_set.GetSetArtItem();

		if (pBestAnim == pCurAnim)
		{
			const float resultFrames = bestSample.Frame();
			const float curFrames = curSample.Frame();
			const float maxFrames = GetDuration(pCurAnim) * 30.0f;

			if (Abs(curFrames - resultFrames) <= pSettings->m_sameAnimFrameTolerance)
			{
				if (g_motionMatchingOptions.m_debugInFinalPlayerState)
				{
					if (self.IsKindOf(SID("Player")))
					{
						Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::ApplyBestSample sameAnim 1\n");
					}
				}

				sameAnim = true;
			}
			else if (pBestAnim->m_flags & ArtItemAnim::kLooping)
			{
				if (g_motionMatchingOptions.m_debugInFinalPlayerState)
				{
					if (self.IsKindOf(SID("Player")))
					{
						Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::ApplyBestSample sameAnim 2\n");
					}
				}

				sameAnim = true;
			}
			// don't let static, non-looping animations run off the end just because the align doesn't move
			else if ((maxFrames - curSample.Frame()) > 30.0f)
			{
				const float deltaTime = (resultFrames - curFrames) / 30.0f;
				const float stoppingFaceDist = m_set.GetStoppingFaceDist();
				Maybe<MMLocomotionState> maybeRelAlign = ComputeLocomotionStateInFuture(curSample, deltaTime, stoppingFaceDist);

				if (maybeRelAlign.Valid())
				{
					MMLocomotionState relAlign = maybeRelAlign.Get();

					const float transDist = Length(AsVector(relAlign.m_alignOs.Pos()));
					const float rotDist = Abs(AngleFromXZVec(GetLocalZ(relAlign.m_alignOs)).ToDegrees());

					if (transDist < 0.2f && rotDist < 5.0f)
					{
						if (g_motionMatchingOptions.m_debugInFinalPlayerState)
						{
							if (self.IsKindOf(SID("Player")))
							{
								Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::ApplyBestSample sameAnim 3\n");
							}
						}

						sameAnim = true;
					}
				}
			}
		}
	}

	AnimSample bestSampleToUse = bestSample;

	if (sameAnim && !m_requestRefreshAnimState)
	{
		if (!curSetIsMine)
		{
			// This can happen when we feel like we don't need to play a different animation but our sets are still different
			// (this is considered procedural motion from a foreign set) 
			// So fade to a new instance but keep the same anim as before. 
			// In the future we may just want to force an anim change but right now that would be a subtle and sweeping change to make
			// at a very late stage in the project.
			bestSampleToUse = maybeCurSample.Otherwise(bestSampleToUse);
		}
		else
		{
			if (g_motionMatchingOptions.m_debugInFinalPlayerState)
			{
				if (self.IsKindOf(SID("Player")))
				{
					Character::AppendDebugTextInFinal("CharacterMotionMatchLocomotion::ApplyBestSample return false 2\n");
				}
			}
			return false;
		}
	}

	bool applied = false;

	const StringId64 setId = m_set.GetId();
	StringId64 bestSampleAnimId = pBestAnim->GetNameId();

	const bool switchingSets = m_prevSetId != setId;
	const bool newTrack = switchingSets && pStateLayer->HasFreeTrack();

	BlendPair blendTime = BlendPair(pSettings, 0.5f);

	if (switchingSets)
	{
		blendTime = GetMotionMatchBlendSpeeds(m_transitions, m_prevSetId, setId, blendTime);
	}

	if (const DC::BlendParams* pBlend = GetMotionMatchBlendSettings(pCurInstance,
																	pSettings,
																	bestSampleAnimId,
																	switchingSets))
	{
		blendTime = BlendPair(*pBlend);
	}

	if (switchingSets && (m_prevSetId == SID("none")) && (m_initialBlendTime >= 0.0f))
	{
		blendTime = BlendPair(m_initialBlendTime, m_initialBlendTime);

		if (m_initialBlendCurve != DC::kAnimCurveTypeInvalid)
		{
			blendTime.m_curve = m_initialBlendCurve;
		}
	}

	if (switchingSets && (m_prevSetId == SID("none")) && (m_initialMotionBlendTime >= 0.0f))
	{
		blendTime.m_motionFadeTime = m_initialMotionBlendTime;
	}

	bool changeSuppressed = false;

	FadeToStateParams params;
	if (m_requestRefreshAnimState && m_hasRefreshParams)
	{
		params = m_refreshFadeToStateParams;
	}

	if ((mode == AnimChangeMode::kSuppressed) && pCurAnim && !m_requestRefreshAnimState)
	{
		const float curPhase = maybeCurSample.Get().Phase();
		const float duration = GetDuration(pCurAnim);
		const float remTime = Limit01(1.0f - curPhase) * duration;

		const float blendOutTime = Max(blendTime.m_animFadeTime, blendTime.m_motionFadeTime);
			
		if (remTime > blendOutTime)
		{
			if (pCurInstance->GetSubsystemControllerId() != GetSubsystemId())
			{
				pBestAnim = pCurAnim;
				bestSampleAnimId = pCurAnim->GetNameId();
				bestSampleToUse = maybeCurSample.Get();
				params.m_phaseSync = true;
			}
			else
			{
				changeSuppressed = true;
			}
		}
		else
		{
			changeSuppressed = false;
		}
	}

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		if (self.IsKindOf(SID("Player")))
		{
			char debugText[1024];

			snprintf(debugText, sizeof(debugText), "CharacterMotionMatchLocomotion::ApplyBestSample: bestSampleAnimId:%s, bestSampleToUse:%s\n", 
				DevKitOnly_StringIdToString(bestSampleAnimId), DevKitOnly_StringIdToString(bestSampleToUse.GetDbgNameId()));
			Character::AppendDebugTextInFinal(debugText);

			snprintf(debugText, sizeof(debugText), "CharacterMotionMatchLocomotion::ApplyBestSample changeSuppressed: %d\n", changeSuppressed);
			Character::AppendDebugTextInFinal(debugText);
		}
	}

	if (!changeSuppressed)
	{
		DC::AnimCharacterInfo* pCharInfo = pAnimControl->Info<DC::AnimCharacterInfo>();
		DC::CharacterMotionMatchLocomotionInfo& pInfo = pCharInfo->m_motionMatchLocomotion;

		pInfo.m_anim	   = bestSampleAnimId;
		pInfo.m_startPhase = bestSampleToUse.Phase();
		pInfo.m_mirror	   = bestSampleToUse.Mirror();

		pCharInstInfo->m_locomotion.m_motionMatchSetId = setId;
		pCharInstInfo->m_locomotion.m_playerMoveMode   = m_playerMoveMode;
		pCharInstInfo->m_locomotion.m_loop = pBestAnim->IsLooping();

		params.m_stateStartPhase = bestSampleToUse.Phase();

		if (params.m_animFadeTime < 0.0f)
		{
			params.m_animFadeTime = blendTime.m_animFadeTime;
		}

		if (params.m_motionFadeTime < 0.0f)
		{
			params.m_motionFadeTime = blendTime.m_motionFadeTime;
		}

		if (params.m_blendType == DC::kAnimCurveTypeInvalid)
		{
			params.m_blendType = blendTime.m_curve;
		}

		if (params.m_newInstBehavior == FadeToStateParams::kUnspecified)
		{
			params.m_newInstBehavior = newTrack ? FadeToStateParams::kSpawnNewTrack
												: FadeToStateParams::kUsePreviousTrack;
		}

		params.m_skipFirstFrameUpdate  = GetActionState() != ActionState::kUnattached;
		params.m_subsystemControllerId = GetSubsystemId();

		const ArtItemAnim* pPrevPhaseAnim = pCurInstance ? pCurInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;

		if (pPrevPhaseAnim && !pPrevPhaseAnim->IsLooping() && !m_disableBlendLimiter
			&& !pCurInstance->IsTransitionValid(SID("auto"), pAnimControl->GetInfoCollection()))
		{
			const float totalTime = GetDuration(pPrevPhaseAnim);
			if (totalTime > NDI_FLT_EPSILON)
			{
				const float remPhase = Limit01(1.0f - pCurInstance->Phase());
				const float remTime	 = Max(remPhase * totalTime, 0.2f);

				params.m_animFadeTime	= Min(remTime, params.m_animFadeTime);
				params.m_motionFadeTime = Min(remTime, params.m_motionFadeTime);
			}
		}

		params.m_disableAnimReplacement = !pSettings->m_allowOverlayRemaps;

		pStateLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		const StateChangeRequest::ID requestId = pStateLayer->FadeToState(SID("s_motion-match-locomotion"), params);

		if (GetActionState() == ActionState::kInvalid || GetActionState() == ActionState::kUnattached)
		{
			BindToAnimRequest(requestId, SID("base"));
		}

		m_requestRefreshAnimState = false;
		m_hasRefreshParams		  = false;
		m_disableBlendLimiter = false;

		applied = true;
	}

	return applied;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::UpdateAndApplyRootRotation(NdGameObject& self)
{
	Vector* pDirPs = nullptr;
	Vector desiredDirPs = kZero;

	if (m_motionModelPs.GetUserFacing().Valid())
	{
		desiredDirPs = m_motionModelPs.GetUserFacing().Get();
		pDirPs = &desiredDirPs;
	}

	const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();

	Maybe<AnimSample> sample = GetCurrentAnimSample();
	Quat ikRotDesired = kIdentity;

	const bool wantIkRot = pSettings && pSettings->m_enableStrafingIk;

	if (wantIkRot && sample.Valid() && LengthSqr(m_motionModelPs.GetDesiredVel()) > 0.0f)
	{
		const float rotationTime = pSettings->m_proceduralRotationTime;
		const float futureTime = Max(m_set.GetMaxTrajectoryTime(), rotationTime);

		const float stoppingFaceDist = m_set.GetStoppingFaceDist();
		Maybe<MMLocomotionState> futureState = ComputeLocomotionStateInFuture(sample.Get(), futureTime, stoppingFaceDist);

		if (futureState.Valid() && pDirPs)
		{
			ikRotDesired = Conjugate(self.GetRotationPs()) * m_matchRotationPs;
		}
	}

	if (MotionMatchIkAdjustments* pIk = m_hIk.ToSubsystem())
	{
		const float blend = GetBlend();
		pIk->DoIk(ikRotDesired, blend);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator CharacterMotionMatchLocomotion::GetMatchLocatorPs(const NdGameObject& self) const
{
	Locator locPs = self.GetLocatorPs();

	if (g_motionMatchingOptions.m_proceduralOptions.m_enableProceduralAlignTranslation)
	{
		locPs = ApplyProceduralTranslationPs(locPs);
	}

	if (m_strafeBlend > 0.0f)
	{
		const Quat strafeRotPs = m_matchRotationPs;

		if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMatchLocSpeedBlending))
		{
			locPs.SetRotation(strafeRotPs);
		}
		else
		{
			const Quat actualRotPs = locPs.Rot();

			const float speed = Length(m_motionModelPs.GetVel());
			//const float speed = m_motionModelPs.GetSprungSpeed();
			const float rotBlend = LerpScaleClamp(0.1f, 0.75f, 0.0f, 1.0f, speed);
			const Quat matchRotPs = Slerp(actualRotPs, strafeRotPs, rotBlend);

			locPs.SetRotation(matchRotPs);
		}
	}

	return locPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::AreExternalTransitionAnimsAllowed() const
{
	if (m_disableExternalTransitions)
		return false;

	return m_prevSetId == SID("none") || m_prevSetId == m_set.GetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::IsCharInIdle() const
{
	const Character* pSelf = GetOwnerCharacter();
	if (!pSelf)
		return false;

	const DC::MotionMatchingSettings* pSettings = GetSettings();

	if (!pSettings || (INVALID_STRING_ID_64 == pSettings->m_idleAnim))
		return false;

	const AnimControl* pAnimControl = pSelf ? pSelf->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const StringId64 curAnimId = pCurInstance ? pCurInstance->GetPhaseAnim() : INVALID_STRING_ID_64;

	return curAnimId == pSettings->m_idleAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const
{
	float factor = desired * m_input.m_groundAdjustFactor;

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmGroundAdjust))
	{
		factor = 0.0f;
	}

	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::FillFootPlantParams(const AnimStateInstance* pInstance,
														 FootPlantParams* pParamsOut) const
{
	ANIM_ASSERT(pParamsOut);

	pParamsOut->m_tt = m_input.m_groundAdjustFactor;

	if (const CharacterLocomotionInterface* pInterface = GetInterface())
	{
		pInterface->FillFootPlantParams(pInstance, pParamsOut);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetNoAdjustToNavFactor(const AnimStateInstance* pInstance, float desired) const
{
	float factor = Max(desired, (1.0f - m_input.m_navMeshAdjustFactor));

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmNavMeshAdjust))
	{
		factor = 1.0f;
	}

	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterMotionMatchLocomotion::GetLegIkEnabledFactor(const AnimStateInstance* pInstance, float desired) const
{
	// NB: might want to detach these two some day, but for now...
	return GetGroundAdjustFactor(pInstance, desired);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::GetFutureFacingOs(float futureTimeSec, Vector* pFacingOsOut) const
{
	const AnimTrajectorySample futureSample = m_animTrajectoryOs.Get(futureTimeSec);

	if (!futureSample.IsFacingValid())
	{
		return false;
	}

	*pFacingOsOut = futureSample.GetFacingDir();
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterMotionMatchLocomotion::GetFuturePosWs(float futureTimeSecs, bool clampEnd, Point& outFuturePosWs) const
{
	const Character* const pSelf = GetOwnerCharacter();
	if (!pSelf)
	{
		return false;
	}

	const float maxTime = m_animTrajectoryOs.GetMaxTime();

	if ((futureTimeSecs > maxTime) && !clampEnd)
	{
		return false;
	}

	const float actualFutureTime = fmin(futureTimeSecs, maxTime);

	const AnimTrajectorySample futureSample = m_animTrajectoryOs.Get(actualFutureTime);
	if (futureSample.IsPositionValid())
	{
		const Locator& parentSpace = pSelf->GetParentSpace();
		const Point futurePosPs = m_lastMatchLocPs.TransformPoint(futureSample.GetPosition());

		outFuturePosWs = parentSpace.TransformPoint(futurePosPs);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::MotionModelSettings MotionSettingsBlendQueue::GetFromSet(const MMSetPtr& set) const
{
	static DC::MotionModelSettings s_defaultSettings;
	const DC::MotionMatchingSettings* pMatchSettings = set.GetSettings();
	DC::MotionModelSettings settings = pMatchSettings ? pMatchSettings->m_motionSettings : s_defaultSettings;

	CreateDirectionalSettings(settings);

	return settings;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::MotionModelSettings MotionSettingsBlendQueue::Get(TimeFrame time) const
{
	ANIM_ASSERT(m_queue.GetCount() > 0);
	SettingsQueue::ConstIterator it = m_queue.Begin();

	DC::MotionModelSettings current = GetFromSet(it->m_set);

	++it;
	while (it != m_queue.End())
	{
		float totalTime = it->m_blendTime.ToSeconds();
		float deltaTime = (time - it->m_startTime).ToSeconds();
		float blend = LerpScaleClamp(0.0f, totalTime, 0.0f, 1.0f, deltaTime);

		if (blend > 0.0f)
		{
			DC::MotionModelSettings next = GetFromSet(it->m_set);

			current = LerpSettings(current, next, blend);
		}
		++it;
	}

	//Return the blended value
	return current;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionSettingsBlendQueue::Update(TimeFrame curTime)
{
	//Remove any that are not needed any more
	for (int i = m_queue.GetCount() - 1; i > 0; --i)
	{
		const SettingsInst& elem = m_queue.GetAt(i);
		if ((elem.m_startTime + elem.m_blendTime) < curTime)
		{
			m_queue.Drop(i);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionSettingsBlendQueue::Push(const MMSetPtr& set, TimeFrame blendTime, const Clock* pClock)
{
	if (!set.IsValid())
	{
		return;
	}

	while (m_queue.IsFull())
	{
		m_queue.Drop(1);
	}

	SettingsInst newInst = SettingsInst{ pClock->GetCurTime(), blendTime, set };
	m_queue.Enqueue(newInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionSettingsBlendQueue::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_queue.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
#define FILL_DIRECTIONAL_VALUE(dirField, baseField)                                                                    \
	if (settings.dirField.m_count == 0)                                                                                \
	{                                                                                                                  \
		const int numDir	 = 8;                                                                                      \
		const int deltaAngle = 360.0f / numDir;                                                                        \
		float angle = -180.0f;                                                                                         \
		for (int i = 0; i < numDir + 1; i++, angle += deltaAngle)                                                      \
		{                                                                                                              \
			settings.dirField.m_keys[i]	  = angle;                                                                     \
			settings.dirField.m_values[i] = baseField;                                                                 \
		}                                                                                                              \
		settings.dirField.m_count = numDir + 1;                                                                        \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionSettingsBlendQueue::CreateDirectionalSettings(DC::MotionModelSettings& settings) const
{
	FILL_DIRECTIONAL_VALUE(m_velocityKDirectional, settings.m_velocitySpringConst);
	FILL_DIRECTIONAL_VALUE(m_velocityKDirectionalIdle, -1.0f);
	FILL_DIRECTIONAL_VALUE(m_velocityKDecelDirectional, settings.m_velocitySpringConstDecel);
	FILL_DIRECTIONAL_VALUE(m_maxSpeedDirectional, settings.m_maxSpeed);
	FILL_DIRECTIONAL_VALUE(m_turnRateDpsDirectional, settings.m_turnRateDps);
	FILL_DIRECTIONAL_VALUE(m_turnRateDpsDirectionalIdle, -1.0f);
	FILL_DIRECTIONAL_VALUE(m_horseFacingKAccelDirectional, 8.0f);
	FILL_DIRECTIONAL_VALUE(m_horseFacingKDecelDirectional, 8.0f);
	FILL_DIRECTIONAL_VALUE(m_horseDriftFactorDirectional, 8.0f);
	FILL_DIRECTIONAL_VALUE(m_horseDriftFactorDirectionalIdle, -360.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::MotionMatchingSettings* MMSetPtr::GetSettings() const
{
	if (const DC::MotionMatchingSet* pSet = GetSet())
	{
		return &pSet->m_settings;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const MotionMatchingSet* MMSetPtr::GetSetArtItem() const
{
	const DC::MotionMatchingSet* pSet = GetSet();
	if (!pSet)
		return nullptr;

	const MotionMatchingSet* pMotionSet = g_pMotionMatchingMgr->LookupMotionMatchingSetById(pSet->m_motionMatchId);

	return pMotionSet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F MMSetPtr::GetNumTrajectorySamples() const
{
	if (const MotionMatchingSet* pMotionSet = GetSetArtItem())
	{
		return pMotionSet->m_pSettings->m_goals.m_numTrajSamples;
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F MMSetPtr::GetNumPrevTrajectorySamples() const
{
	if (const MotionMatchingSet* pMotionSet = GetSetArtItem())
	{
		return pMotionSet->m_pSettings->m_goals.m_numTrajSamplesPrevTraj;
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MMSetPtr::GetMaxTrajectoryTime() const
{
	if (const MotionMatchingSet* pMotionSet = GetSetArtItem())
	{
		return pMotionSet->m_pSettings->m_goals.m_maxTrajSampleTime;
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MMSetPtr::GetMaxPrevTrajectoryTime() const
{
	if (const MotionMatchingSet* pMotionSet = GetSetArtItem())
	{
		return pMotionSet->m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj;
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MMSetPtr::GetStoppingFaceDist() const
{
	if (const MotionMatchingSet* pMotionSet = GetSetArtItem())
	{
		return pMotionSet->m_pSettings->m_goals.m_stoppingFaceDist;
	}

	return -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 MMSetPtr::MotionMatchSetArtItemId() const
{
	if (const DC::MotionMatchingSet* pSet = GetSet())
	{
		return pSet->m_motionMatchId;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MMSetPtr::IsValid() const
{
	return GetSetArtItem() != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::MotionMatchingSet* MMSetPtr::GetSet() const
{
	if (m_ptr.Valid())
	{
		const void* pData = m_ptr;
		auto typeId = ScriptManager::TypeOf(pData);
		if (typeId == SID("motion-matching-set"))
		{
			return reinterpret_cast<const DC::MotionMatchingSet*>(pData);
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// TODO: Npcs might handle this differently, see also ICharacterLocomotion::CreateLocomotionState
Quat CharacterMotionMatchLocomotion::AdjustRotationToUprightPs(Quat_arg rotPs) const
{
	const NdGameObject* pOwner = GetOwnerGameObject();
	if (!pOwner)
	{
		return rotPs;
	}
	const Locator& parentSpace = pOwner->GetParentSpace();

	ANIM_ASSERT(IsNormal(rotPs));
	ANIM_ASSERT(IsNormal(parentSpace.Rot()));

	const Quat rotWs = Normalize(parentSpace.Rot() * rotPs);

	Quat uprightWs = rotWs;

	if (Abs(rotWs.Y()) > NDI_FLT_EPSILON)
	{
		uprightWs.SetX(0.0f);
		uprightWs.SetZ(0.0f);
		uprightWs = Normalize(uprightWs);
	}
	else if (false)
	{
		const Vector ltWs = GetLocalX(rotWs);
		const Vector upWs = GetLocalY(rotWs);
		const Vector fwWs = GetLocalZ(rotWs);

		g_prim.Draw(DebugCoordAxes(Locator(pOwner->GetTranslation(), uprightWs), 0.5f, kPrimEnableHiddenLineAlpha));
	}

	ANIM_ASSERT(IsReasonable(parentSpace));
	ANIM_ASSERTF(IsNormal(uprightWs), ("Denormal upright quat %s", PrettyPrint(uprightWs)));

	const Quat uprightPs = Conjugate(parentSpace.Rot()) * uprightWs;

	return uprightPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator CharacterMotionMatchLocomotion::ApplyProceduralTranslationPs(const Locator& locPs) const
{
	const float clampDist = m_motionModelPs.GetProceduralClampDist();

	Locator retPs = locPs;

	if (clampDist >= 0.0f)
	{
		const DC::MotionMatchingSettings* pSettings = m_set.GetSettings();
		const bool flatten = pSettings
							 && (pSettings->m_motionSettings.m_projectionMode == DC::kProjectionModeCharacterPlane);

		const Point motionModelPosPs = flatten ? PointFromXzAndY(m_motionModelPs.GetPos(), locPs.Pos())
											   : m_motionModelPs.GetPos();

		GAMEPLAY_ASSERT(IsReasonable(motionModelPosPs));

		const Vector trajPtToObjPs = locPs.Pos() - motionModelPosPs;
		const Vector clampedOffsetPs = LimitVectorLength(trajPtToObjPs, 0.0f, clampDist);
		GAMEPLAY_ASSERT(IsReasonable(clampedOffsetPs));

		retPs.SetPos(motionModelPosPs + clampedOffsetPs);
	}

	retPs.SetPos(retPs.Pos() + m_proceduralTransDeltaPs);

	return retPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotion::ResetMotionModel()
{
	NdGameObject* pOwner = GetOwnerGameObject();

	const Vector velPs = pOwner->IsKindOf(SID("PlayerBase")) ? pOwner->GetBaseVelocityPs() : pOwner->GetVelocityPs();

	const Locator& locPs = pOwner->GetLocatorPs();

	m_motionModelPs = MotionModel(locPs.Pos(), GetLocalZ(locPs), VectorXz(velPs), nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float LerpPossiblyNegative(float a, float b, float tt)
{
	if ((a < 0.0f) && (b < 0.0f))
	{
		return -1.0f;
	}

	if (a < 0.0f)
	{
		return b;
	}

	if (b < 0.0f)
	{
		return a;
	}

	return Lerp(a, b, tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
#define BLEND_DIRECTIONAL(dirField, allowNegative)                                                                     \
	{                                                                                                                  \
		ANIM_ASSERT(a.dirField.m_count > 0 && b.dirField.m_count > 0);                                                 \
		const int numDir	 = 8;                                                                                      \
		const int numSamples = numDir + 1;                                                                             \
		const int deltaAngle = 360.0f / numDir;                                                                        \
		float angle = -180.0f;                                                                                         \
		for (I32F i = 0; i < numSamples; i++, angle += deltaAngle)                                                     \
		{                                                                                                              \
			float aSpeed = NdUtil::EvaluatePointCurve(angle, &a.dirField);                                             \
			float bSpeed = NdUtil::EvaluatePointCurve(angle, &b.dirField);                                             \
                                                                                                                       \
			float blendSpeed = allowNegative ? Lerp(aSpeed, bSpeed, tt) : LerpPossiblyNegative(aSpeed, bSpeed, tt);    \
			result.dirField.m_keys[i]	= angle;                                                                       \
			result.dirField.m_values[i] = blendSpeed;                                                                  \
		}                                                                                                              \
		result.dirField.m_count = numSamples;                                                                          \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
#define BLEND_DIRECTIONAL_WITH_FALLBACK(dirField, fallbackField, minValid, allowNegative)                              \
	{                                                                                                                  \
		ANIM_ASSERT(a.dirField.m_count > 0 && b.dirField.m_count > 0);                                                 \
		const int numDir	 = 8;                                                                                      \
		const int numSamples = numDir + 1;                                                                             \
		const int deltaAngle = 360.0f / numDir;                                                                        \
		float angle = -180.0f;                                                                                         \
                                                                                                                       \
		const bool aValid = a.dirField.m_values[0] >= minValid;                                                        \
		const bool bValid = b.dirField.m_values[0] >= minValid;                                                        \
		for (I32F i = 0; i < numSamples; i++, angle += deltaAngle)                                                     \
		{                                                                                                              \
			float aSpeed = NdUtil::EvaluatePointCurve(angle, aValid ? &a.dirField : &a.fallbackField);                 \
			float bSpeed = NdUtil::EvaluatePointCurve(angle, bValid ? &b.dirField : &b.fallbackField);                 \
                                                                                                                       \
			float blendSpeed = allowNegative ? Lerp(aSpeed, bSpeed, tt) : LerpPossiblyNegative(aSpeed, bSpeed, tt);    \
			result.dirField.m_keys[i]	= angle;                                                                       \
			result.dirField.m_values[i] = blendSpeed;                                                                  \
		}                                                                                                              \
		result.dirField.m_count = numSamples;                                                                          \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
static DC::MotionModelSettings LerpSettings(const DC::MotionModelSettings& a, const DC::MotionModelSettings& b, float tt)
{
	if (tt <= 0.0f)
	{
		return a;
	}

	if (tt >= 1.0f)
	{
		return b;
	}

	DC::MotionModelSettings result = (tt < 0.5f) ? a : b;

	result.m_applyPathModeFix = b.m_applyPathModeFix;
	result.m_projectionMode = b.m_projectionMode;

	result.m_maxSpeed = Lerp(a.m_maxSpeed, b.m_maxSpeed, tt);

	result.m_turnRateDps = Lerp(a.m_turnRateDps, b.m_turnRateDps, tt);

	result.m_velocitySpringConst	  = Lerp(a.m_velocitySpringConst, b.m_velocitySpringConst, tt);
	result.m_velocitySpringConstDecel = LerpPossiblyNegative(a.m_velocitySpringConstDecel,
															 b.m_velocitySpringConstDecel,
															 tt);
	result.m_speedSpringConst		  = LerpPossiblyNegative(a.m_speedSpringConst, b.m_speedSpringConst, tt);

	result.m_movingGroupSpeedPercentThreshold = LerpPossiblyNegative(a.m_movingGroupSpeedPercentThreshold,
																	 b.m_movingGroupSpeedPercentThreshold,
																	 tt);

	result.m_maxAccel		= LerpPossiblyNegative(a.m_maxAccel, b.m_maxAccel, tt);
	result.m_transClampDist = LerpPossiblyNegative(a.m_transClampDist, b.m_transClampDist, tt);

	result.m_strafeRoundingAngle   = LerpPossiblyNegative(a.m_strafeRoundingAngle, b.m_strafeRoundingAngle, tt);
	result.m_strafeHysteresisAngle = LerpPossiblyNegative(a.m_strafeHysteresisAngle, b.m_strafeHysteresisAngle, tt);
	result.m_strafeAngleSmoothing  = LerpPossiblyNegative(a.m_strafeAngleSmoothing, b.m_strafeAngleSmoothing, tt);

	result.m_pathCornerSpeedMin = Lerp(a.m_pathCornerSpeedMin, b.m_pathCornerSpeedMin, tt);
	result.m_pathCornerSpeedMax = Lerp(a.m_pathCornerSpeedMax, b.m_pathCornerSpeedMax, tt);
	result.m_pathCornerTimeAtMinSpeed = Lerp(a.m_pathCornerTimeAtMinSpeed, b.m_pathCornerTimeAtMinSpeed, tt);

	result.m_onPathTestAngleMinDeg	= Lerp(a.m_onPathTestAngleMinDeg, b.m_onPathTestAngleMinDeg, tt);
	result.m_onPathTestAngleMaxDeg	= Lerp(a.m_onPathTestAngleMaxDeg, b.m_onPathTestAngleMaxDeg, tt);
	result.m_onPathTestAngleSameDeg = Lerp(a.m_onPathTestAngleSameDeg, b.m_onPathTestAngleSameDeg, tt);
	result.m_newPathAngleDeg		= Lerp(a.m_newPathAngleDeg, b.m_newPathAngleDeg, tt);
	result.m_offPathSpringMult		= Lerp(a.m_offPathSpringMult, b.m_offPathSpringMult, tt);
	result.m_criticalOffPathSpringMult = Lerp(a.m_criticalOffPathSpringMult, b.m_criticalOffPathSpringMult, tt);

	result.m_baseYawSpeedFactor = Lerp(a.m_baseYawSpeedFactor, b.m_baseYawSpeedFactor, tt);

	result.m_alwaysInterpolateFacing  = a.m_alwaysInterpolateFacing || b.m_alwaysInterpolateFacing;

	result.m_horseFacingDampenRatio	 = Lerp(a.m_horseFacingDampenRatio, b.m_horseFacingDampenRatio, tt);
	result.m_horseMomentumSpeed		 = Lerp(a.m_horseMomentumSpeed, b.m_horseMomentumSpeed, tt);
	result.m_horseStopFacingMomentum = Lerp(a.m_horseStopFacingMomentum, b.m_horseStopFacingMomentum, tt);

	// Blend the directional speeds
	BLEND_DIRECTIONAL(m_maxSpeedDirectional, false);
	BLEND_DIRECTIONAL(m_velocityKDirectional, false);
	BLEND_DIRECTIONAL(m_velocityKDecelDirectional, false);
	BLEND_DIRECTIONAL(m_turnRateDpsDirectional, false);
	BLEND_DIRECTIONAL(m_horseFacingKAccelDirectional, false);
	BLEND_DIRECTIONAL(m_horseFacingKDecelDirectional, false);
	BLEND_DIRECTIONAL(m_horseDriftFactorDirectional, true);

	// These won't gracefully blend to the other curves in the fallback case
	BLEND_DIRECTIONAL_WITH_FALLBACK(m_velocityKDirectionalIdle, m_velocityKDirectional, 0.0f, false);
	BLEND_DIRECTIONAL_WITH_FALLBACK(m_turnRateDpsDirectionalIdle, m_turnRateDpsDirectional, 0.0f, false);
	BLEND_DIRECTIONAL_WITH_FALLBACK(m_horseDriftFactorDirectionalIdle, m_horseDriftFactorDirectional, -180.0f, true);

	return result;
}
