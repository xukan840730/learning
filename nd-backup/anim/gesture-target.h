/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/process/process.h"
#include "ndlib/util/maybe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Gesture
{
	class Target;

	static U32 const kTargetMaxSize = 128;

	struct ALIGNED(16) TargetBuffer
	{
		void Clear() { memset(&m_buf, 0, sizeof(U8) * kTargetMaxSize); }
		void CopyFrom(const Gesture::Target* pSrc) { memcpy(m_buf, pSrc, sizeof(U8) * kTargetMaxSize); }
		Gesture::Target* AsTarget() { return (Gesture::Target*)&m_buf[0]; }
		const Gesture::Target* AsTarget() const { return (const Gesture::Target*)&m_buf[0]; }

		U8 m_buf[kTargetMaxSize];
	};

	class Target
	{
	public:
		virtual Maybe<Point> GetWs(const Locator& originWs) const = 0;
	};

	/* WARNING:
	   Targets are straight-up memcpy'd in gesture-target-manager.cpp and elsewhere,
	   so don't put anything in a Target that would need to be properly copy-constructed
	   or relocated.
	*/
	
	extern const float kDefaultGestureSpringConstant;
	extern const float kDefaultAimGestureSpringConstant;
	extern const float kDefaultDampingRatio;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetPoint : public Target
	{
	public:
		TargetPoint(const BoundFrame& p) : m_point(p) {}
		virtual Maybe<Point> GetWs(const Locator& originWs) const override { return m_point.GetTranslationWs(); }
	private:
		const BoundFrame m_point;
	};
	STATIC_ASSERT(sizeof(TargetPoint) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetObject : public Target
	{
	public:
		TargetObject(const NdLocatableObject* pTarget) : m_hTarget(pTarget) {}
		explicit TargetObject(NdLocatableObjectHandle hTarget) : m_hTarget(hTarget) {}

		virtual Maybe<Point> GetWs(const Locator& originWs) const override;
	private:
		NdLocatableObjectHandle m_hTarget;
	};
	STATIC_ASSERT(sizeof(TargetObject) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetObjectJoint : public Target
	{
	public:
		TargetObjectJoint(const NdGameObject* pTarget, const I32 jointIndex)
			: m_hTarget(pTarget), m_jointIndex(jointIndex)
		{
		}

		virtual Maybe<Point> GetWs(const Locator& originWs) const override;
	private:
		NdGameObjectHandle m_hTarget;
		I32 m_jointIndex;
	};
	STATIC_ASSERT(sizeof(TargetObjectJoint) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetScriptLambda : public Target
	{
	public:
		TargetScriptLambda(const NdGameObject* pOwner, const DC::ScriptLambda* pTargetFn)
			: m_hOwner(pOwner), m_pTargetFn(pTargetFn)
		{
		}
		virtual Maybe<Point> GetWs(const Locator& originWs) const override;

	private:
		NdGameObjectHandle m_hOwner;
		const DC::ScriptLambda* m_pTargetFn;
	};
	STATIC_ASSERT(sizeof(TargetScriptLambda) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetAimAt : public Target
	{
	public:
		TargetAimAt(const NdGameObject* pOwner) : m_hOwner(pOwner) {}
		virtual Maybe<Point> GetWs(const Locator& originWs) const override;

	private:
		NdGameObjectHandle m_hOwner;
	};
	STATIC_ASSERT(sizeof(TargetAimAt) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetLookAt : public Target
	{
	public:
		TargetLookAt(const NdGameObject* pOwner) : m_hOwner(pOwner) {}
		virtual Maybe<Point> GetWs(const Locator& originWs) const override;
	
	private:
		NdGameObjectHandle m_hOwner;
	};
	STATIC_ASSERT(sizeof(TargetLookAt) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetLocator : public Target
	{
	public:
		TargetLocator(const NdGameObject* pOwner, const StringId64 locNameId)
			: m_hOwner(pOwner)
			, m_locNameId(locNameId)
		{
		}

		virtual Maybe<Point> GetWs(const Locator& originWs) const override;
	private:
		NdGameObjectHandle m_hOwner;
		StringId64 m_locNameId;
	};
	STATIC_ASSERT(sizeof(TargetLocator) <= kTargetMaxSize);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class TargetAnimEulerAngles : public Target
	{
	public:
		TargetAnimEulerAngles(const NdGameObject* pOwner, const StringId64 xAngleChannelId, const StringId64 yAngleChannelId)
			: m_hOwner(pOwner)
			, m_xAngleId(xAngleChannelId)
			, m_yAngleId(yAngleChannelId)
		{
		}

		virtual Maybe<Point> GetWs(const Locator& originWs) const override;

	private:
		NdGameObjectHandle m_hOwner;
		StringId64 m_xAngleId;
		StringId64 m_yAngleId;
	};
	STATIC_ASSERT(sizeof(TargetAnimEulerAngles) <= kTargetMaxSize);
}
