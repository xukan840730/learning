/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/nd-anim-align-util.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::GetApToAlignDelta(const AnimStateInstance* pStateInst)
{
	return GetApToAlignDelta(pStateInst, pStateInst->GetApRefChannelId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::GetApToAlignDelta(const AnimStateInstance* pStateInst, StringId64 apChannelId)
{
	PROFILE(AI, GetApToAlignDelta);

	// Default the locator to something valid.
	Locator deltaLoc = Locator(kIdentity);

	const StringId64 channels[2] =
	{
		SID("align"),
		apChannelId,
	};

	ndanim::JointParams outChannelJoints[2];
	const U32F evaluatedChannelsMask = pStateInst->EvaluateChannels(channels, 2, outChannelJoints);

	// If we were able to evaluate the align we can continue...
	if (evaluatedChannelsMask & (1 << 0))
	{
		ndanim::JointParams& alignJoint = outChannelJoints[0];
		Locator alignLoc(alignJoint.m_trans, alignJoint.m_quat);

		// Ensure that we found the 'apReference' channel
		if (evaluatedChannelsMask & (1 << 1))
		{
			ndanim::JointParams& apRefJoint = outChannelJoints[1];
			Locator apRefLoc(apRefJoint.m_trans, apRefJoint.m_quat);

			// Move the ApRef into 'animation space'. Now the apRef and the align are in the same space.
			Locator apRefInAnimLoc = alignLoc.TransformLocator(apRefLoc);

			// Translate apRef to the origin (and align)
			const Vector apRefInAnimTrans = apRefInAnimLoc.Pos() - SMath::kOrigin;
			alignLoc.SetPos(alignLoc.Pos() - apRefInAnimTrans);

			// Rotate the 'align' so that apRef coincides with the base coordinate axis.
			const Locator rotLoc(SMath::kZero, Conjugate(apRefInAnimLoc.Rot()));

			deltaLoc = rotLoc.TransformLocator(alignLoc);
		}
		else
		{
			// No 'apReference' channel was present. Let's use the raw align movement in relation to the origin.
			deltaLoc = Locator(alignJoint.m_trans, alignJoint.m_quat);
		}
	}

	return deltaLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::WalkComputeAlign(const Process* pObject,
									  const AnimStateLayer* pStateLayer,
									  const BoundFrame& currentAlign,
									  const Locator& alignSpace,
									  const float scale)
{
	const Locator currentAlignAs = alignSpace.UntransformLocator(currentAlign.GetLocatorWs());
	Locator newAlign = currentAlignAs;

	AnimAlignBlender b(pObject, currentAlign, alignSpace, scale, nullptr, false);
	newAlign = b.BlendForward(pStateLayer, LocatorData(currentAlignAs, kInvalidInterp)).m_locator;

	return newAlign;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::WalkComputeAlignDelta(const Process* pObject,
										   const AnimStateLayer* pStateLayer,
										   const BoundFrame& currentAlign,
										   const Locator& alignSpace,
										   const float scale)
{
	const Locator currentAlignAs = alignSpace.UntransformLocator(currentAlign.GetLocatorWs());

	const Locator interpolatedNewAlign = NdAnimAlign::WalkComputeAlign(pObject,
																	   pStateLayer,
																	   currentAlign,
																	   alignSpace,
																	   scale);

	// Get the delta transform needed to get from the current align to the new interpolated one.
	// Translation need to be in the space of the current align and not in world space. Also, we need
	// the delta rotation to get from current align rotation to desired rotation.
	const Vector deltaPos = interpolatedNewAlign.Pos() - currentAlignAs.Pos();
	const Quat inverseCurrentAlignRot = Conjugate(currentAlignAs.Rot());
	const Locator animAlignDelta	  = Locator(Point(kZero) + Rotate(inverseCurrentAlignRot, deltaPos),
											inverseCurrentAlignRot * interpolatedNewAlign.Rot());
	return animAlignDelta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimAlign::DetermineAlignWs(const AnimSimpleLayer* pAnimSimpleLayer,
								   const Locator& currentAlignWs,
								   Locator& newAlignWs)
{
	bool result = false;

	Locator interpAlignWs = kIdentity;

	const I32F numInstances = pAnimSimpleLayer->GetNumInstances();

	for (I32F i = numInstances - 1; i >= 0; --i)
	{
		const AnimSimpleInstance* pInst = pAnimSimpleLayer->GetInstance(i);
		const float blend = result ? pInst->GetFade() : 1.0f;

		Locator curAlignWs;
		result |= DetermineAlignWs(pInst, currentAlignWs, curAlignWs);

		if (result)
		{
			interpAlignWs = Lerp(interpAlignWs, curAlignWs, blend);
		}
	}

	if (result)
	{
		newAlignWs = interpAlignWs;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// DetermineAlign() will return true iff AlignToActionPackOrigin() was called after AddAnimation().
// In this case, it returns the origin transform (m_pGameObject->m_loc) the object *should* have
// in order to align its apReference with the master apOrigin specified in AlignToActionPackOrigin().
bool NdAnimAlign::DetermineAlignWs(const AnimSimpleInstance* pAnimSimpleInstance,
								   const Locator& currentAlignWs,
								   Locator& newAlignWs)
{
	bool valid = false;

	if (pAnimSimpleInstance->IsValid())
	{
		F32 phase = pAnimSimpleInstance->GetPhase();

		if (pAnimSimpleInstance->IsAlignedToActionPackOrigin())
		{
			valid = FindAlignFromApReference(pAnimSimpleInstance->GetAnimTable()->GetSkelId(),
											 pAnimSimpleInstance->GetAnim().ToArtItem(),
											 phase,
											 pAnimSimpleInstance->GetApOrigin().GetLocatorWs(),
											 pAnimSimpleInstance->GetApChannelName(),
											 &newAlignWs,
											 pAnimSimpleInstance->IsFlipped());
		}
		else if (pAnimSimpleInstance->IsAligningLocation())
		{
			const bool result = EvaluateChannelInAnim(pAnimSimpleInstance->GetAnimTable()->GetSkelId(), 
													  pAnimSimpleInstance->GetAnim().ToArtItem(),
													  SID("align"),
													  phase,
													  &newAlignWs,
													  pAnimSimpleInstance->IsFlipped());
			if (result)
			{
				// evaluating align is in local space, ap references get converted to world space through aporigin, so this
				// must be in world space as well
				newAlignWs = currentAlignWs.TransformLocator(newAlignWs);

				// make sure we're normalized
				newAlignWs.SetRotation(Normalize(newAlignWs.GetRotation()));

				valid = true;
			}
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimAlign::AnimAlignBlender::GetDataForInstance(const AnimStateInstance* pInstance, LocatorData* pDataOut)
{
	ANIM_ASSERT(pInstance);

	const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
	const float scale = m_scale;
	Locator apAlignDelta = Locator(kIdentity);
	const Locator baseAlignAs = m_alignSpace.UntransformLocator(m_baseAlign.GetLocatorWs());

	const AnimStateSnapshot& stateSnapshot = pInstance->GetAnimStateSnapshot();

	if (stateFlags & DC::kAnimStateFlagFirstAlignRefMoveUpdate)
	{
		apAlignDelta = Locator(kIdentity);

		const StringId64 channelIds[1] = { SID("align") };
		ndanim::JointParams outChannelJoints[1];
		const U32F evaluatedChannelsMask = pInstance->EvaluateChannels(channelIds, 1, outChannelJoints);

		// If we were able to evaluate the align we can continue...
		if (evaluatedChannelsMask & (1 << 0))
		{
			apAlignDelta = Locator(outChannelJoints[0].m_trans, outChannelJoints[0].m_quat);
			ANIM_ASSERTF(IsFinite(apAlignDelta),
						 ("Anim Instance '%s' evaluated a bad align channel",
						  DevKitOnly_StringIdToString(pInstance->GetStateName())));
		}
	}
	else
	{
		// Get the relative align location in relation to the apReference in the animation.
		apAlignDelta = NdAnimAlign::GetApToAlignDelta(pInstance);
		ANIM_ASSERTF(IsFinite(apAlignDelta),
					 ("Anim Instance '%s' has bad ap-align delta",
					  DevKitOnly_StringIdToString(pInstance->GetStateName())));
	}

	// Apply uniform scale to the translation only
	const bool tweakEnabled	   = pInstance->IsAnimDeltaTweakEnabled();
	const Transform tweakXform = pInstance->GetAnimDeltaTweakTransform();

	ANIM_ASSERTF(IsFinite(tweakXform),
				 ("Anim Instance '%s' has bad tweak xform", DevKitOnly_StringIdToString(pInstance->GetStateName())));

	const Point originalTranslation = apAlignDelta.GetTranslation();
	const Point apAlignDeltaTrans = tweakEnabled ? originalTranslation * tweakXform : originalTranslation;
	const Point scaledApAlignDeltaTrans(apAlignDeltaTrans.X() * scale, apAlignDeltaTrans.Y() * scale, apAlignDeltaTrans.Z() * scale);
	apAlignDelta.SetTranslation(scaledApAlignDeltaTrans);

	Locator alignDeltaLoc = pInstance->GetChannelDelta(SID("align"));

	// Apply uniform scale to the translation only
	const Point alignDeltaTrans = alignDeltaLoc.GetTranslation();
	const Point scaledAlignDeltaTrans(alignDeltaTrans.X() * scale, alignDeltaTrans.Y() * scale, alignDeltaTrans.Z() * scale);
	alignDeltaLoc.SetTranslation(scaledAlignDeltaTrans);

	const float phase = pInstance->Phase();
	const float prevPhase = pInstance->PrevPhase();

	const bool apAlignExtrapolation		  = (stateFlags & DC::kAnimStateFlagExtrapolateAlign) && (phase == 1.0f);
	const bool extractAlignDeltaNotFromAp = (stateFlags & DC::kAnimStateFlagExtractDeltaApMoveUpdate);
	const bool useApAlignEvaluation		  = ((stateFlags
										  & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagFirstAlignRefMoveUpdate))
										 && !apAlignExtrapolation)
									  && !extractAlignDeltaNotFromAp && !pInstance->GetFlags().m_disableApRef;

	Locator instanceAlign = Locator(kIdentity);
	if ((stateFlags & DC::kAnimStateFlagSaveTopAlign) && pInstance->HasSavedAlign())
	{
		const Locator apRefWs = pInstance->GetApLocator().GetLocatorWs();
		instanceAlign = m_alignSpace.UntransformLocator(apRefWs);
		ANIM_ASSERTF(IsFinite(instanceAlign),
					 ("Anim Instance '%s' generated bad apReference",
					  DevKitOnly_StringIdToString(pInstance->GetStateName())));
	}
	else if (!(stateFlags & DC::kAnimStateFlagNoAlignMoveUpdate))
	{
		if (useApAlignEvaluation)
		{
			const Locator apRefWs = pInstance->GetApLocator().GetLocatorWs();
			const Locator apRefLoc = instanceAlign = m_alignSpace.UntransformLocator(apRefWs);

			ANIM_ASSERTF(IsFinite(apRefLoc),
						 ("Anim Instance '%s' generated bad apReference",
						  DevKitOnly_StringIdToString(pInstance->GetStateName())));
			instanceAlign = apRefLoc.TransformLocator(apAlignDelta);
		}
		else
		{
			Locator alignDeltaLs = alignDeltaLoc;
			const bool frozen = pInstance->GetFlags().m_phaseFrozen;
		
			if ((stateFlags & DC::kAnimStateFlagExtrapolateAlign) && !frozen)
			{
				const float phaseDelta	  = phase - prevPhase;
				const float updateRate	  = stateSnapshot.m_updateRate;
				const float remainderTime = pInstance->GetRemainderTime();

				// If this is zero something is wrong with the way we are updating the phase PhaseUpdate. The previous
				// delta need to be saved off. Note (JDB) if something as a func-driven phase that gets incidentally
				// frozen we shouldn't crash here SYSTEM_ASSERT(phaseDelta > 0.0f);
				if (phaseDelta > 0.0f)
				{
					const float lastUpdateDeltaTimeTruncated = phaseDelta / updateRate;
					const float lastUpdateDeltaTime = lastUpdateDeltaTimeTruncated + remainderTime;
					const float timeScaleFactor		= lastUpdateDeltaTime / lastUpdateDeltaTimeTruncated;
					const Vector translation		= alignDeltaLs.GetTranslation() - kOrigin;
					const Vector scaledTranslation	= translation * timeScaleFactor;
					const Locator extrapolatedDeltaLoc = Locator(kOrigin + scaledTranslation,
																 alignDeltaLs.GetRotation());
					alignDeltaLs = extrapolatedDeltaLoc;
				}
			}

			ANIM_ASSERT(IsFinite(alignDeltaLs.GetTranslation()));
			ANIM_ASSERT(IsFinite(alignDeltaLs.GetRotation()));

			instanceAlign = baseAlignAs.TransformLocator(alignDeltaLs);
		}
	}
	else
	{
		instanceAlign = baseAlignAs;
	}

	if (!(stateFlags & DC::kAnimStateFlagNoAdjustToUpright))
	{
		instanceAlign = AdjustLocatorToUpright(instanceAlign);
	}

	if (m_pObject)
	{
		const AnimStateLayer* pLayer	   = pInstance->GetLayer();
		const DC::AnimState* pDcState	   = pInstance->GetState();
		const DC::ScriptLambda* pAlignFunc = pDcState ? pDcState->m_alignFunc : nullptr;

		if (pAlignFunc)
		{
			// Fetch the info collection and resolve all the pointers
			const DC::AnimInfoCollection* pInfoCollection = pInstance->GetAnimInfoCollection();

			DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
			scriptInfoCollection.m_actor	= scriptInfoCollection.m_actor;
			scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
			scriptInfoCollection.m_top		= scriptInfoCollection.m_top;

			DC::AnimStateSnapshotInfo dcSnapshot;
			dcSnapshot.m_translatedPhaseAnimName		= stateSnapshot.m_translatedPhaseAnimName;
			dcSnapshot.m_translatedPhaseAnimSkelId		= stateSnapshot.m_translatedPhaseSkelId.GetValue();
			dcSnapshot.m_translatedPhaseAnimHierarchyId = stateSnapshot.m_translatedPhaseHierarchyId;
			dcSnapshot.m_stateSnapshot = &stateSnapshot;

			const BoundFrame curAlign = BoundFrame(m_alignSpace.TransformLocator(instanceAlign),
												   m_baseAlign.GetBinding());

			BoundFrame apRef(pInstance->GetApLocator());
			const ScriptValue argv[] = { ScriptValue(pDcState),
										 ScriptValue(&scriptInfoCollection),
										 ScriptValue(m_pObject->GetScriptId()),
										 ScriptValue(&m_baseAlign),
										 ScriptValue(&curAlign),
										 ScriptValue(&apRef),
										 ScriptValue(pInstance->GetPhase()),
										 ScriptValue(&dcSnapshot) };

			const ScriptValue res = ScriptManager::Eval(pAlignFunc,
														SID("anim-state-align-func"),
														ARRAY_COUNT(argv),
														argv);

			const BoundFrame* pLocator = res.m_boundFrame;

			ANIM_ASSERT(pLocator);
			if (pLocator)
			{
				const Locator instanceAlignWs = pLocator->GetLocatorWs();
				instanceAlign = m_alignSpace.UntransformLocator(instanceAlignWs);
			}
		}
		else if (pLayer && pLayer->HasInstanceCallBackAlignFunc())
		{
			const BoundFrame curAlign = BoundFrame(m_alignSpace.TransformLocator(instanceAlign), m_baseAlign.GetBinding());
			BoundFrame newAlign;

			if (pLayer->InstanceCallBackAlignFunc(pInstance, m_baseAlign, curAlign, apAlignDelta, &newAlign, m_debugDraw))
			{
				const Locator instanceAlignWs = newAlign.GetLocatorWs();
				instanceAlign = m_alignSpace.UntransformLocator(instanceAlignWs);
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_drawAPLocators && (stateFlags & DC::kAnimStateFlagApMoveUpdate)
							 && !pInstance->GetFlags().m_disableApRef))
	{
		Locator wsApLocator = pInstance->GetApLocator().GetLocatorWs();
		g_prim.Draw(DebugCoordAxes(wsApLocator, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
		StringBuilder<256> buf;
		buf.format("(%s, %0.2f) ", DevKitOnly_StringIdToString(pInstance->GetStateName()), pInstance->Phase());
		
		g_prim.Draw(DebugString(wsApLocator.Pos() + GetLocalZ(wsApLocator.Rot()) * 0.15f,
								buf.c_str(),
								kColorWhite,
								0.6f));
	} 

	*pDataOut = LocatorData(instanceAlign, kLinearInterp);
	GAMEPLAY_ASSERT(IsReasonable(pDataOut->m_locator));
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::AnimAlignBlender::AdjustLocatorToUpright(const Locator& loc) const
{
	Vector newZ = ProjectOntoPlane(GetLocalZ(loc.Rot()), kUnitYAxis);
	newZ = SafeNormalize(newZ, kUnitZAxis);
	Locator ret = loc;
	ret.SetRot(QuatFromLookAt(newZ, kUnitYAxis));
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAnimAlign::LocatorData NdAnimAlign::AnimAlignBlender::BlendData(const LocatorData& leftData,
																  const LocatorData& rightData,
																  float masterFade,
																  float animFade,
																  float motionFade)
{
	const bool useSlerpA = (leftData.m_flags & kSphericalInterp) && !(rightData.m_flags & kLinearInterp);
	const bool useSlerpB = !(leftData.m_flags & kLinearInterp) && (rightData.m_flags & kSphericalInterp);

	if (useSlerpA || useSlerpB)
	{
		const U64 newFlags = leftData.m_flags | rightData.m_flags;

		return LocatorData(SphericalBlendData(leftData.m_locator, 
											  rightData.m_locator, 
											  masterFade, animFade, 
											  motionFade),
						   leftData.m_flags | rightData.m_flags);
	}
	else
	{
		const U64 newFlags = (leftData.m_flags | rightData.m_flags) & ~kSphericalInterp;

		return LocatorData(Lerp(leftData.m_locator, rightData.m_locator, motionFade), newFlags);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector VecSlerp(Vector_arg a, Vector_arg b, float t)
{
	float angle = Acos(MinMax((float)Dot(a, b), -1.0f, 1.0f));
	if (abs(angle) < FLT_EPSILON)
		return Lerp(a, b, t);
	Vector result = (Sin((1.0f - t)* angle) / Sin(angle)) * a + (Sin(t * angle) / Sin(angle)) * b;
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdAnimAlign::AnimAlignBlender::SphericalBlendData(const Locator& leftData,
														  const Locator& rightData,
														  float masterFade,
														  float animFade,
														  float motionFade) const
{
	Locator ret;

	// use spherical linear lerp for position.
	Point finalPos;
	{
		const Point basePos = m_baseAlign.GetTranslation();

		const Point leftDataPos = leftData.Pos();
		const Point rightDataPos = rightData.Pos();
		const Vector leftDataDelta = leftDataPos - basePos;
		const Vector rightDataDelta = rightDataPos - basePos;

		const float leftDataLen = Length(leftDataDelta);
		const float rightDataLen = Length(rightDataDelta);

		if (Sqr(leftDataLen) <= 1.0e-30f || Sqr(rightDataLen) <= 1.0e-30f)
		{
			// just use linear lerp since one of vectors is too small.
			finalPos = Lerp(leftDataPos, rightDataPos, motionFade);
		}
		else
		{
			const Vector leftDeltaNorm = SafeNormalize(leftDataDelta, kZero);
			const Vector rightDeltaNorm = SafeNormalize(rightDataDelta, kZero);
			const float dotV = Dot(leftDeltaNorm, rightDeltaNorm);

			if (dotV < Cos(DEGREES_TO_RADIANS(135.f)))
			{
				// two anim delta are opposite direction, use slerp will cause blending result go side way.
				finalPos = Lerp(leftDataPos, rightDataPos, motionFade);
			}
			else
			{
				const Vector finalDir = VecSlerp(leftDeltaNorm, rightDeltaNorm, motionFade);
				ANIM_ASSERT(IsFinite(finalDir));
				const Vector finalDirNorm = SafeNormalize(finalDir, kZero);

				const float finalLen = Lerp(leftDataLen, rightDataLen, motionFade);
				finalPos = basePos + finalDirNorm * finalLen;
			}
		}
	}

	ret.SetPos(finalPos);
	ret.SetRot(Slerp(leftData.Rot(), rightData.Rot(), motionFade));

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimAlign::InstanceAlignTable::Init(size_t maxSize)
{
	m_numEntries = 0;
	m_maxEntries = maxSize;

	if (maxSize > 0)
	{
		m_pEntries = NDI_NEW Entry[maxSize];
	}
	else
	{
		m_pEntries = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimAlign::InstanceAlignTable::Set(AnimStateInstance::ID id, const Locator& align)
{
	if ((!m_pEntries) || (0 == m_maxEntries))
	{
		return false;
	}

	for (U32F i = 0; i < m_numEntries; ++i)
	{
		if (m_pEntries[i].m_id == id)
		{
			m_pEntries[i].m_align = align;
			return true;
		}
	}

	if (m_numEntries < m_maxEntries)
	{
		m_pEntries[m_numEntries].m_id = id;
		m_pEntries[m_numEntries].m_align = align;
		++m_numEntries;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimAlign::InstanceAlignTable::Get(AnimStateInstance::ID id, Locator* pAlignOut) const
{
	if (!m_pEntries)
	{
		return false;
	}

	bool found = false;

	for (U32F i = 0; i < m_numEntries; ++i)
	{
		if (m_pEntries[i].m_id == id)
		{
			if (pAlignOut)
				*pAlignOut = m_pEntries[i].m_align;
			found = true;
			break;
		}
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAlignPath(const Locator& space,
						const SkeletonId& skelId,
						const ArtItemAnim* pAnim,
						const Locator* pApRef,
						StringId64 apRefNameId,
						float startPhase,
						float endPhase,
						bool mirror,
						Color clr,
						DebugPrimTime tt)
{
	if (!pAnim || startPhase > endPhase)
		return;

	PrimServerWrapper ps(space);

	ps.SetDuration(tt);
	ps.EnableHiddenLineAlpha();
	ps.SetLineWidth(3.0f);

	if (pApRef)
	{
		ps.DrawCoordAxes(*pApRef, 0.2f);
		ps.DrawString(pApRef->Pos(), DevKitOnly_StringIdToString(apRefNameId), kColorWhiteTrans, 0.5f);
	}

	Locator prevLoc = kIdentity;
	bool prevValid = false;

	float phase = startPhase;

	do
	{
		Locator loc;

		if (pApRef)
		{
			if (!FindAlignFromApReference(skelId, pAnim, phase, *pApRef, apRefNameId, &loc, mirror))
				continue;
		}
		else if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase, &loc, mirror))
		{
			continue;
		}

		ps.DrawCoordAxes(loc, 0.1f);
		ps.DrawString(loc.Pos(), StringBuilder<64>("%0.3f", phase).c_str(), kColorWhiteTrans, 0.5f);

		if (prevValid)
		{
			ps.DrawLine(prevLoc.Pos(), loc.Pos(), clr);
		}

		prevLoc = loc;
		prevValid = true;

		if (phase >= endPhase)
			break;

		phase += pAnim->m_pClipData->m_phasePerFrame;
		phase = Min(endPhase, phase);
	} while (true);
}
