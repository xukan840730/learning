/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/util/timer.h"

#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"

#include <orbisanim/animhierarchy.h>
#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/joints.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR float kEpsilon = 1e-7f;

/// --------------------------------------------------------------------------------------------------------------- ///
#define APPLY_PARAM_OVERRIDE(field)                                                                                    \
	if (overrides.field >= 0.0f)                                                                                       \
	{                                                                                                                  \
		params.field = overrides.field;                                                                                \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
#define APPLY_PARAM_OVERRIDE_NEG(field)                                                                                \
	if (overrides.field <= 0.0f)                                                                                       \
	{                                                                                                                  \
		params.field = overrides.field;                                                                                \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
static SMath::Mat44 Mat44FromFloats(const float* pFloatData)
{
	Mat44 ret;
	for (U32F i = 0; i < 4; ++i)
	{
		for (U32F j = 0; j < 4; ++j)
			ret.Set(i, j, pFloatData[(i * 4) + j]);
	}
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vec4 LimitVec4Length(Vec4_arg vec, Scalar_arg minLen, Scalar_arg maxLen)
{
	const Scalar curLen = Length4(vec);
	const Vec4_arg normVec = SafeNormalize4(vec, kZero);
	const Scalar constrainedLen = MinMax(curLen, minLen, maxLen);
	const Vec4 newVec = normVec * constrainedLen;
	return newVec;
}

namespace OrbisAnim
{
	namespace CommandBlock
	{
		struct PersistentDataBlock
		{
			ndanim::JointParams m_prev;
			ndanim::JointParams m_prevVel;
			TimeFrame m_prevTime;
		};

		/// --------------------------------------------------------------------------------------------------------------- ///
		static float Accel(float x, float v, const FatConstraintParams& params)
		{
			// system [mass spring damper]
			// critical damping  =  2m sqrt(k/m)    -------->   realDamp  =  ratio  *  criticalDamping
			// m(a+g)  +  cv  +  kx  = 0    ---->  a = (-cv - kx) /m  -  g
			const float invMass		 = 1.0f / params.m_mass;
			const float criticalDamp = params.m_damping * 2.0f * params.m_mass * Sqrt(params.m_stiffness * invMass);

			return ((-x * params.m_stiffness) - (v * criticalDamp)) * invMass;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static void RungeKutta4(float x, float v, float scale, const FatConstraintParams& params, float& newX, float& newV)
		{
			const float x1 = v;
			const float v1 = Accel(x, x1, params);

			const float x2 = v + v1 * scale * 0.5f;
			const float v2 = Accel(x + x1 * scale * 0.5f, x2, params);

			const float x3 = v + v2 * scale * 0.5f;
			const float v3 = Accel(x + x2 * scale * 0.5f, x3, params);

			const float x4 = v + v3 * scale;
			const float v4 = Accel(x + x3 * scale, x4, params);

			newX = x + (x1 + 2.0 * x2 + 2.0f * x3 + x4) * scale / 6.0f;
			newV = v + (v1 + 2.0 * v2 + 2.0f * v3 + v4) * scale / 6.0f;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static Vec4 InterpolateVec4(Vec4_arg v,
									Vec4_arg prev,
									Vec4_arg vel,
									Vec4& newVelOut,
									float dt,
									const FatConstraintParams& params)
		{
			float newDelta[4];
			float newVel[4];

			const Vec4 delta = prev - v;

			RungeKutta4(delta.X(), vel.X(), dt, params, newDelta[0], newVel[0]);
			RungeKutta4(delta.Y(), vel.Y(), dt, params, newDelta[1], newVel[1]);
			RungeKutta4(delta.Z(), vel.Z(), dt, params, newDelta[2], newVel[2]);
			RungeKutta4(delta.W(), vel.W(), dt, params, newDelta[3], newVel[3]);

			const Vec4 newV = Vec4(v.X() + newDelta[0],
								   v.Y() + newDelta[1],
								   v.Z() + newDelta[2],
								   v.W() + newDelta[3]);

			newVelOut = Vec4(newVel);

			return newV;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static Point InterpolatePoint(Point_arg pos,
									  Point_arg prev,
									  Point_arg vel,
									  Point& newVelOut,
									  float dt,
									  const FatConstraintParams& params)
		{
			Vec4 newVel;
			const Vec4 interpVec = InterpolateVec4(pos.GetVec4(), prev.GetVec4(), vel.GetVec4(), newVel, dt, params);

			const Point interpPos = Point(interpVec);

			newVelOut = Point(newVel);
			return interpPos;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static Quat InterpolateQuat(Quat_arg rot,
									Quat_arg prev,
									Quat_arg vel,
									Quat& newVelOut,
									float dt,
									const FatConstraintParams& params)
		{
			ANIM_ASSERT(IsFinite(rot));
			ANIM_ASSERT(IsFinite(prev));
			ANIM_ASSERT(IsFinite(vel));
			ANIM_ASSERT(IsFinite(dt));
			ANIM_ASSERT(Length4(vel.GetVec4()) < 1e5f);

			Vec4 newVel;
			const Vec4 interpVec = InterpolateVec4(rot.GetVec4(), prev.GetVec4(), vel.GetVec4(), newVel, dt, params);
			
			Quat interpRot = Quat(interpVec.X(), interpVec.Y(), interpVec.Z(), interpVec.W());
			//Quat interpRot = Quat(interpVec);

			interpRot = SafeNormalize(interpRot, kIdentity);

			newVel = LimitVec4Length(newVel, 0.0f, 1000.0f);
			newVelOut = Quat(newVel);

			ANIM_ASSERT(IsFinite(interpRot));
			ANIM_ASSERT(IsFinite(newVelOut));

			return interpRot;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static Vector InterpolateVector(Vector_arg v,
										Vector_arg prev,
										Vector_arg vel,
										Vector& newVelOut,
										float dt,
										const FatConstraintParams& params)
		{
			Vec4 newVel;
			const Vec4 interpVec = InterpolateVec4(v.GetVec4(), prev.GetVec4(), vel.GetVec4(), newVel, dt, params);
			const Vector interp	 = Vector(interpVec);
			newVelOut = Vector(newVel);
			return interp;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static Point ApplyFatConstraintLimit(Point_arg prevPos,
											 Point_arg inPos,
											 Point_arg desPos,
											 Vector& limitVelOut,
											 Quat_arg limitRot,
											 const FatConstraintParams& params)
		{
			const Quat invLimitRot = Conjugate(limitRot);

			//const Vector moveVec = desPos - prevPos;
			const Vector moveVec = desPos - inPos;
			const Vector rotatedMoveVec = Rotate(invLimitRot, moveVec);

			Vector dMaxes = Vector(kEpsilon, kEpsilon, kEpsilon);

			if (IsPositive(rotatedMoveVec.X()))
			{
				dMaxes.SetX(Max(params.m_limitPositive[0], kEpsilon));
			}
			else
			{
				dMaxes.SetX(Min(params.m_limitNegative[0], -kEpsilon));
			}

			if (IsPositive(rotatedMoveVec.Y()))
			{
				dMaxes.SetY(Max(params.m_limitPositive[1], kEpsilon));
			}
			else
			{
				dMaxes.SetY(Min(params.m_limitNegative[1], -kEpsilon));
			}

			if (IsPositive(rotatedMoveVec.Z()))
			{
				dMaxes.SetZ(Max(params.m_limitPositive[2], kEpsilon));
			}
			else
			{
				dMaxes.SetZ(Min(params.m_limitNegative[2], -kEpsilon));
			}

			const float smoothRatio = Max(params.m_limitSmoothPercent, kEpsilon);

			float newVecValues[3];

			for (I32F i = 0; i < 3; ++i)
			{
				const Scalar dCurrent = Abs(rotatedMoveVec[i]);
				const Scalar dMax	  = Abs(dMaxes[i]);

				const Scalar delta	= smoothRatio * dMax;
				const Scalar dStart = dMax - delta;

				if (dCurrent >= dStart)
				{
					newVecValues[i] = delta * (1.0f - Exp((dStart - dCurrent) / delta)) + dStart;
				}
				else
				{
					newVecValues[i] = dCurrent;
				}

				newVecValues[i] *= Sign(rotatedMoveVec[i]);
			}

			const Vector newVec = Rotate(limitRot, Vector(newVecValues));

			const Point newPos = inPos + newVec;

			limitVelOut = newVec - moveVec;

			return newPos;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		static ndanim::JointParams InterpolateJp(const ndanim::JointParams& des,
												 const ndanim::JointParams& prev,
												 const ndanim::JointParams& vel,
												 ndanim::JointParams& newVelOut,
												 float dt,
												 Quat_arg limitRot,
												 const FatConstraintParams& params,
												 float rotateWeight,
												 float scaleWeight,
												 float translateWeight)
		{
			ndanim::JointParams ret;

			if (translateWeight < kEpsilon)
			{
				ret.m_trans		  = des.m_trans;
				newVelOut.m_trans = kOrigin;
			}
			else
			{
				const Point interpPos = InterpolatePoint(des.m_trans,
														 prev.m_trans,
														 vel.m_trans,
														 newVelOut.m_trans,
														 dt,
														 params);

				Point newPos = interpPos;
				if (params.m_flags & FatConstraintParams::Flags::kUseLimit)
				{
					Vector limitVel = kZero;
					newPos = ApplyFatConstraintLimit(prev.m_trans, des.m_trans, newPos, limitVel, limitRot, params);
					newVelOut.m_trans += limitVel;
				}

				ret.m_trans = Lerp(des.m_trans, newPos, translateWeight);
			}

			if (rotateWeight < kEpsilon)
			{
				ret.m_quat		 = des.m_quat;
				newVelOut.m_quat = kZero;
			}
			else
			{
				Quat inRot		 = des.m_quat;
				const float dotP = Dot(inRot, prev.m_quat);
				if (dotP < 0.0f)
				{
					inRot *= SCALAR_LC(-1.0f);
				}

				const Quat interpRot = InterpolateQuat(inRot, prev.m_quat, vel.m_quat, newVelOut.m_quat, dt, params);

				ret.m_quat = Slerp(inRot, interpRot, rotateWeight);
				ret.m_quat = Normalize(ret.m_quat);
			}

			if (scaleWeight < kEpsilon)
			{
				ret.m_scale		  = des.m_scale;
				newVelOut.m_scale = kZero;
			}
			else
			{
				const Vector interpScale = InterpolateVector(des.m_scale,
															 prev.m_scale,
															 vel.m_scale,
															 newVelOut.m_scale,
															 dt,
															 params);

				ret.m_scale = Lerp(des.m_scale, interpScale, scaleWeight);
			}

			return ret;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void DebugOverrideFatConstraintParams(FatConstraintParams& params)
		{
			STRIP_IN_FINAL_BUILD;
			/*
			F32 m_translateWeight;
			F32 m_rotateWeight;
			F32 m_scaleWeight;
			F32 m_mass;
			F32 m_stiffness;
			F32 m_damping;

			F32 m_limitPositive[3];
			F32 m_limitNegative[3];
			F32 m_limitSmoothPercent;
			U32 m_useLimit;
			*/

			const AnimOptions::FatConstraintOverrides& overrides = g_animOptions.m_proceduralRig.m_fatOverrides;

			APPLY_PARAM_OVERRIDE(m_rotateWeightDefault);
			APPLY_PARAM_OVERRIDE(m_scaleWeightDefault);
			APPLY_PARAM_OVERRIDE(m_translateWeightDefault);
			APPLY_PARAM_OVERRIDE(m_mass);
			APPLY_PARAM_OVERRIDE(m_stiffness);
			APPLY_PARAM_OVERRIDE(m_damping);
			APPLY_PARAM_OVERRIDE(m_limitPositive[0]);
			APPLY_PARAM_OVERRIDE(m_limitPositive[1]);
			APPLY_PARAM_OVERRIDE(m_limitPositive[2]);
			APPLY_PARAM_OVERRIDE_NEG(m_limitNegative[0]);
			APPLY_PARAM_OVERRIDE_NEG(m_limitNegative[1]);
			APPLY_PARAM_OVERRIDE_NEG(m_limitNegative[2]);
			APPLY_PARAM_OVERRIDE(m_limitSmoothPercent);
			APPLY_PARAM_OVERRIDE(m_flags);
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		const Mat44 GetJoints(Location loc, const HierarchyHeader* pHierarchyHeader, bool isIntermediate4x4)
		{
			if (isIntermediate4x4)
				return *(const Mat44*)HierarchyQuadword(loc, pHierarchyHeader);
			else
				return ((const OrbisAnim::JointTransform*)HierarchyQuadword(loc, pHierarchyHeader))->GetMat44();
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandFatConstraintImpl(const HierarchyHeader* pHierarchyHeader,
											 const FatConstraintParams* pParams,
											 const OrbisAnim::SegmentContext* pSegmentContext)
		{
			RIG_NODE_TIMER_START();

			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_proceduralRig.m_enableFatConstraintOverrides))
			{
				FatConstraintParams* pDebugParams = STACK_ALLOC(FatConstraintParams, 1);
				*pDebugParams = *pParams;
				DebugOverrideFatConstraintParams(*pDebugParams);
				pParams = pDebugParams;
			}

			const FatConstraintParams& params = *pParams;

			const Mat44 inMatrixJoint	 = GetJoints(params.m_inMatrixLoc,
													 pHierarchyHeader,
													 !!(params.m_flags
														& FatConstraintParams::Flags::kInMatrixIsIntermediate4x4));
			const Mat44 spaceMatrixJoint = GetJoints(params.m_spaceMatrixLoc,
													 pHierarchyHeader,
													 !!(params.m_flags
														& FatConstraintParams::Flags::kSpaceMatrixIsIntermediate4x4));
			const Mat44 limitMatrixJoint = GetJoints(params.m_limitMatrixLoc,
													 pHierarchyHeader,
													 !!(params.m_flags
														& FatConstraintParams::Flags::kLimitMatrixIsIntermediate4x4));
			const Mat44 relMatrixJoint	 = GetJoints(params.m_relativeMatrixLoc,
													 pHierarchyHeader,
													 !!(params.m_flags
														& FatConstraintParams::Flags::kRelativeMatrixIsIntermediate4x4));

			ANIM_ASSERTF(IsReasonable(inMatrixJoint),
						 ("FatConstraint '%s' has unreasonable input matrix %s",
						  DevKitOnly_StringIdToString(params.m_nodeNameId),
						  PrettyPrint(inMatrixJoint)));
			ANIM_ASSERTF(IsReasonable(spaceMatrixJoint),
						 ("FatConstraint '%s' has unreasonable space matrix %s",
						  DevKitOnly_StringIdToString(params.m_nodeNameId),
						  PrettyPrint(spaceMatrixJoint)));
			ANIM_ASSERTF(IsReasonable(limitMatrixJoint),
						 ("FatConstraint '%s' has unreasonable limit matrix %s",
						  DevKitOnly_StringIdToString(params.m_nodeNameId),
						  PrettyPrint(limitMatrixJoint)));
			ANIM_ASSERTF(IsReasonable(relMatrixJoint),
						 ("FatConstraint '%s' has unreasonable relative matrix %s",
						  DevKitOnly_StringIdToString(params.m_nodeNameId),
						  PrettyPrint(relMatrixJoint)));

			float rotateWeight = params.m_rotateWeightLoc != Location::kInvalid
									 ? *HierarchyFloat(params.m_rotateWeightLoc, pHierarchyHeader)
									 : params.m_rotateWeightDefault;
			float scaleWeight = params.m_scaleWeightLoc != Location::kInvalid
									? *HierarchyFloat(params.m_scaleWeightLoc, pHierarchyHeader)
									: params.m_scaleWeightDefault;
			float translateWeight = params.m_translateWeightLoc != Location::kInvalid
										? *HierarchyFloat(params.m_translateWeightLoc, pHierarchyHeader)
										: params.m_translateWeightDefault;

			float* pOutputRotateX	 = HierarchyFloat(params.m_outputLocs[0], pHierarchyHeader);
			float* pOutputRotateY	 = HierarchyFloat(params.m_outputLocs[1], pHierarchyHeader);
			float* pOutputRotateZ	 = HierarchyFloat(params.m_outputLocs[2], pHierarchyHeader);
			float* pOutputScaleX	 = HierarchyFloat(params.m_outputLocs[3], pHierarchyHeader);
			float* pOutputScaleY	 = HierarchyFloat(params.m_outputLocs[4], pHierarchyHeader);
			float* pOutputScaleZ	 = HierarchyFloat(params.m_outputLocs[5], pHierarchyHeader);
			float* pOutputTranslateX = HierarchyFloat(params.m_outputLocs[6], pHierarchyHeader);
			float* pOutputTranslateY = HierarchyFloat(params.m_outputLocs[7], pHierarchyHeader);
			float* pOutputTranslateZ = HierarchyFloat(params.m_outputLocs[8], pHierarchyHeader);

			const SMath::Mat44 inMatrix	   = inMatrixJoint;
			const SMath::Mat44 spaceMatrix = spaceMatrixJoint;
			const SMath::Mat44 relMatrix   = Inverse(relMatrixJoint);

			Mat44 limitMatrix = limitMatrixJoint;
			RemoveScale(&limitMatrix);
			const Quat limitRot = Quat(limitMatrix);

			const SMath::Mat44 offsetMatrix = Mat44FromFloats(params.m_offsetMatrix);

			const SMath::Mat44 computeMatrix = inMatrix * Inverse(spaceMatrix);
			Mat44 computeMatrixNoScale = computeMatrix;
			RemoveScale(&computeMatrixNoScale);

			ndanim::JointParams inParams;
			inParams.m_quat	 = Quat(computeMatrixNoScale);
			inParams.m_scale = Vector(Length3(inMatrixJoint.GetRow(0)),
									  Length3(inMatrixJoint.GetRow(1)),
									  Length3(inMatrixJoint.GetRow(2)))
							   / Vector(Length3(spaceMatrixJoint.GetRow(0)),
										Length3(spaceMatrixJoint.GetRow(1)),
										Length3(spaceMatrixJoint.GetRow(2)));
			inParams.m_trans = Point(computeMatrix.GetRow(3));

			PersistentDataBlock* pPersistentData = (PersistentDataBlock*)((U8*)pSegmentContext->m_pPersistentData + params.m_persistentDataOffset);
			if (!pSegmentContext->m_pPersistentData || !pPersistentData)
			{
				*pOutputTranslateX	= 0.0f;
				*pOutputTranslateY	= 0.0f;
				*pOutputTranslateZ	= 0.0f;
				*pOutputRotateX		= 0.0f;
				*pOutputRotateY		= 0.0f;
				*pOutputRotateZ		= 0.0f;
				*pOutputScaleX		= 1.0f;
				*pOutputScaleY		= 1.0f;
				*pOutputScaleZ		= 1.0f;
				return;
			}

			const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
			const TimeFrame curTime = pClock->GetCurTime();

			bool initial = false;
			if (pPersistentData->m_prevTime == Seconds(0.0f))
			{
				pPersistentData->m_prev = inParams;
				pPersistentData->m_prevTime = curTime;
				initial = true;
			}

			static const F32 kFrameDeltaSecondsMin = 1.0f / 20.0f;
			const float currentFrameDeltaSeconds = ToSeconds(curTime - pPersistentData->m_prevTime);
			const F32 dt = Min(kFrameDeltaSecondsMin, currentFrameDeltaSeconds) * 30.0f;

			ndanim::JointParams newVel;
			ndanim::JointParams interpParams;

			if (initial || FALSE_IN_FINAL_BUILD(g_animOptions.m_proceduralRig.m_disableFatConstraint))
			{
				newVel.m_trans = kOrigin;
				newVel.m_scale = kZero;
				newVel.m_quat  = kZero;
				interpParams   = inParams;
			}
			else if (pClock->IsPaused() || (dt < NDI_FLT_EPSILON))
			{
				interpParams = pPersistentData->m_prev;
				newVel = pPersistentData->m_prevVel;
			}
			else
			{
				interpParams = InterpolateJp(inParams,
											 pPersistentData->m_prev,
											 pPersistentData->m_prevVel,
											 newVel,
											 dt,
											 limitRot,
											 params,
											 rotateWeight,
											 scaleWeight,
											 translateWeight);
			}

			// output = offset * <interp> * space * relative
			Mat44 scaleMat = Mat44(kIdentity);
			scaleMat.SetScale(interpParams.m_scale.GetVec4() + Vec4(0.0f, 0.0f, 0.0f, 1.0f));
			const Mat44 interpMatrix = BuildTransform(interpParams.m_quat, interpParams.m_trans.GetVec4()) * scaleMat;

			const Mat44 outputMatrix = offsetMatrix * interpMatrix * spaceMatrix * relMatrix;

			ANIM_ASSERT(Length3(interpParams.m_trans.GetVec4()) < 10000.0f);
			ANIM_ASSERT(Length3(interpParams.m_scale.GetVec4()) < 10000.0f);
			ANIM_ASSERT(Length3(newVel.m_trans.GetVec4()) < 10000.0f);
			ANIM_ASSERT(Length3(newVel.m_scale.GetVec4()) < 10000.0f);
			pPersistentData->m_prev = interpParams;
			pPersistentData->m_prevVel	= newVel;
			pPersistentData->m_prevTime = curTime;


			float ox, oy, oz;
			outputMatrix.GetEulerAngles(ox, oy, oz);
			*pOutputTranslateX = outputMatrix.GetRow(3).X();
			*pOutputTranslateY = outputMatrix.GetRow(3).Y();
			*pOutputTranslateZ = outputMatrix.GetRow(3).Z();
			*pOutputScaleX	   = Length4(outputMatrix.GetRow(0));
			*pOutputScaleY	   = Length4(outputMatrix.GetRow(1));
			*pOutputScaleZ	   = Length4(outputMatrix.GetRow(2));

			*pOutputRotateX = RADIANS_TO_DEGREES(ox);
			*pOutputRotateY = RADIANS_TO_DEGREES(oy);
			*pOutputRotateZ = RADIANS_TO_DEGREES(oz);

			ANIM_ASSERT(IsReasonable(*pOutputTranslateX));
			ANIM_ASSERT(IsReasonable(*pOutputTranslateY));
			ANIM_ASSERT(IsReasonable(*pOutputTranslateZ));
			ANIM_ASSERT(IsReasonable(*pOutputScaleX));
			ANIM_ASSERT(IsReasonable(*pOutputScaleY));
			ANIM_ASSERT(IsReasonable(*pOutputScaleZ));
			ANIM_ASSERT(IsReasonable(*pOutputRotateX));
			ANIM_ASSERT(IsReasonable(*pOutputRotateY));
			ANIM_ASSERT(IsReasonable(*pOutputRotateZ));

			RIG_NODE_TIMER_END(RigNodeType::kFatConstraint);

			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_proceduralRig.m_debugFatConstraint && !pClock->IsPaused()))
			{
				const Point inPos = inParams.m_trans; //Point(inMatrix.GetRow(3));
				float ex, ey, ez;
				inMatrix.GetEulerAngles(ex, ey, ez);

				const float elapsedTimeSec = ConvertTicksToSeconds(startTick, endTick);
				MsgConPauseable("--------- FatConstraint (%s) --------- \n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				MsgConPauseable("time: %0.4fms\n", elapsedTimeSec * 1000.0f);
				MsgConPauseable("input:\n");
				MsgConPauseable("  trans: %f %f %f\n", (float)inPos.X(), (float)inPos.Y(), (float)inPos.Z());
				MsgConPauseable("  rot:   %0.1fdeg %0.1fdeg %0.1fdeg\n", RADIANS_TO_DEGREES(ex), RADIANS_TO_DEGREES(ey), RADIANS_TO_DEGREES(ez));
				MsgConPauseable("  scale: %f %f %f\n", (float)inParams.m_scale.X(), (float)inParams.m_scale.Y(), (float)inParams.m_scale.Z());

				MsgConPauseable("output:\n");
				MsgConPauseable("  trans: %f %f %f\n", *pOutputTranslateX, *pOutputTranslateY, *pOutputTranslateZ);
				MsgConPauseable("  rot:   %0.1fdeg %0.1fdeg %0.1fdeg\n", *pOutputRotateX, *pOutputRotateY, *pOutputRotateZ);
				MsgConPauseable("  scale: %f %f %f\n", *pOutputScaleX, *pOutputScaleY, *pOutputScaleZ);

				MsgConPauseable("vel:\n");
				MsgConPauseable("  trans: %f %f %f\n", (float)newVel.m_trans.X(), (float)newVel.m_trans.Y(), (float)newVel.m_trans.Z());
				MsgConPauseable("  rot:   %f %f %f %f\n", (float)newVel.m_quat.X(), (float)newVel.m_quat.Y(), (float)newVel.m_quat.Z(), (float)newVel.m_quat.W());
				MsgConPauseable("  scale: %f %f %f\n", (float)newVel.m_scale.X(), (float)newVel.m_scale.Y(), (float)newVel.m_scale.Z());
			}

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("FatConstraint (%s)\n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				MsgAnim("   Trans: %.4f %.4f %.4f \n", (float)outputMatrix.GetRow(3).X(), (float)outputMatrix.GetRow(3).Y(), (float)outputMatrix.GetRow(3).Z());
				MsgAnim("   Rotate: %.4f %.4f %.4f deg\n", (float)RADIANS_TO_DEGREES(ox), (float)RADIANS_TO_DEGREES(oy), (float)RADIANS_TO_DEGREES(oz));
				MsgAnim("   Scale: %.4f %.4f %.4f \n", (float)Length4(outputMatrix.GetRow(0)), (float)Length4(outputMatrix.GetRow(1)), (float)Length4(outputMatrix.GetRow(2)));
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandFatConstraint(DispatcherFunctionArgs_arg_const param_qw0,
										 LocationMemoryMap_arg_const memoryMap,
										 OrbisAnim::SegmentContext* pSegmentContext)
		{
			RIG_NODE_TIMER_START();

			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding

			const size_t paramsLoc = ((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3];
			void* pParamsMem = OrbisAnim::CommandBlock::LocationToPointer(paramsLoc, memoryMap);

			const FatConstraintParams* pParams = (const FatConstraintParams*)pParamsMem;

			ExecuteCommandFatConstraintImpl(pHierarchyHeader, pParams, pSegmentContext);
		}

	} // namespace CommandBlock
} // namespace OrbisAnim
