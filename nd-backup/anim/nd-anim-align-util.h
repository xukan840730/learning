/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/render/util/prim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSimpleInstance;
class AnimSimpleLayer;
class Process;

/// --------------------------------------------------------------------------------------------------------------- ///
/// Helper functions to extract the align from the animation states.
/// --------------------------------------------------------------------------------------------------------------- ///

namespace NdAnimAlign
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	Locator GetApToAlignDelta(const AnimStateInstance* pStateInst);
	Locator GetApToAlignDelta(const AnimStateInstance* pStateInst, StringId64 apChannelId);

	Locator WalkComputeAlign(const Process* pProcess,
							 const AnimStateLayer* pStateLayer,
							 const BoundFrame& currentAlign,
							 const Locator& alignSpace,
							 const float scale);

	Locator WalkComputeAlignDelta(const Process* pProcess,
								  const AnimStateLayer* pStateLayer,
								  const BoundFrame& currentAlign,
								  const Locator& alignSpace,
								  const float scale);

	// choose blending mode:
	// Invalid + Linear			==> Linear
	// Invalid + Spherical		==> Spherical
	// Linear + Linear			==> Linear
	// Spherical + Spherical	==> Spherical
	// Linear + Spherical		==> Linear, ex: blending igc and strafe anim, just choose linear interpolation.
	enum BlendType
	{
		kInvalidInterp	 = 0x0,
		kLinearInterp	 = 0x1, // linear interpolation
		kSphericalInterp = 0x2, // spherical linear interpolation
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class InstanceAlignTable
	{
	public:
		void Init(size_t maxSize);
		bool Set(AnimStateInstance::ID id, const Locator& align);
		bool Get(AnimStateInstance::ID id, Locator* pAlignOut) const;

	private:
		struct Entry
		{
			AnimStateInstance::ID m_id;
			Locator m_align;
		};

		Entry* m_pEntries   = nullptr;
		size_t m_numEntries = 0;
		size_t m_maxEntries = 0;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// add extra data in AnimStateLayer::InstanceBlender, so that we can choose linear interpolation or spherical linear
	// interpolation.
	// slerp works better in strafe moveset.
	struct LocatorData
	{
	public:
		LocatorData() : m_locator(kIdentity), m_flags(kInvalidInterp) {}

		LocatorData(const Locator& loc, U64 flags) : m_locator(loc), m_flags(flags) {}

		Locator m_locator;
		U64 m_flags;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimAlignBlender : public AnimStateLayer::InstanceBlender<LocatorData>
	{
	public:
		AnimAlignBlender(const Process* pObject,
						 const BoundFrame& baseAlign,
						 const Locator& alignSpace,
						 float scale,
						 InstanceAlignTable* pInstanceTable,
						 bool debugDraw)
			: m_baseAlign(baseAlign)
			, m_alignSpace(alignSpace)
			, m_scale(scale)
			, m_pInstanceTable(pInstanceTable)
			, m_pObject(pObject)
			, m_debugDraw(debugDraw)
		{
			ANIM_ASSERT(IsFinite(m_scale));
			ANIM_ASSERT(IsFinite(baseAlign));
			ANIM_ASSERT(IsFinite(alignSpace));
		}

		virtual ~AnimAlignBlender() override {}

	protected:
		virtual LocatorData GetDefaultData() const override 
		{
			Locator baseLoc = m_alignSpace.UntransformLocator(m_baseAlign.GetLocatorWs());
			return LocatorData(baseLoc, kInvalidInterp);
		}
		virtual bool GetDataForInstance(const AnimStateInstance* pInstance, LocatorData* pDataOut) override;

		virtual LocatorData BlendData(const LocatorData& leftData,
									  const LocatorData& rightData,
									  float masterFade,
									  float animFade,
									  float motionFade) override;

		virtual Locator AdjustLocatorToUpright(const Locator& loc) const;

		virtual void OnHasDataForInstance(const AnimStateInstance* pInstance, const LocatorData& data) override
		{
			if (pInstance && m_pInstanceTable)
			{
				m_pInstanceTable->Set(pInstance->GetId(), data.m_locator);
			}
		}

		Locator SphericalBlendData(const Locator& leftData,
								   const Locator& rightData,
								   float masterFade,
								   float animFade,
								   float motionFade) const;

		BoundFrame m_baseAlign;
		Locator m_alignSpace;
		InstanceAlignTable* m_pInstanceTable;
		const Process* m_pObject;
		float m_scale;
		bool m_debugDraw;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool DetermineAlignWs(const AnimSimpleLayer* pAnimSimpleLayer, const Locator& currentAlignWs, Locator& newAlignWs);
	bool DetermineAlignWs(const AnimSimpleInstance* pAnimSimpleInstance,
						  const Locator& currentAlignWs,
						  Locator& newAlignWs);

} // namespace NdAnimAlign


/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAlignPath(const Locator& space,
						const SkeletonId& skelId,
						const ArtItemAnim* pAnim,
						const Locator* pApRef,
						StringId64 apRefNameId,
						float startPhase,
						float endPhase,
						bool mirror = false,
						Color clr = kColorRed,
						DebugPrimTime tt = kPrimDuration1FrameAuto);
