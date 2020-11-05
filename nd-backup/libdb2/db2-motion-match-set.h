/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "shared/math/vector.h"

#include "tools/libs/libdb2/db2-commontypes.h"
#include "tools/libs/libdb2/db2-bundle.h"
#include "tools/libs/libdb2/db2-facade.h"

#include "common/util/basictypes.h"

#include <memory>

namespace libdb2
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchPoseBody
	{
	public:
		MotionMatchPoseBody(const QueryElement& elem);

		bool IsCenterOfMass() const;
		const std::string& JointName() const;
		float PositionWeight() const;
		float VelocityWeight() const;

		std::string GetDesc() const { return m_isCenterOfMass ? "COM" : JointName(); }

	private:
		std::string m_jointName;
		bool m_isCenterOfMass;
		float m_positionWeight;
		float m_velocityWeight;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchPoseFacing
	{
	public:
		MotionMatchPoseFacing(const QueryElement& elem);

		const std::string& JointName() const;
		SMath::Vector Axis() const;
		float Weight() const;

	private:
		std::string m_jointName;
		SMath::Vector m_axis;
		float m_weight;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchPose
	{
	public:
		using BodyList = ListFacade<MotionMatchPoseBody>;

		MotionMatchPose(const QueryElement& elem);
		const BodyList& Bodies() const;
		float Weight() const;
		const MotionMatchPoseFacing& Facing() const;

		const std::string& Xml() const { return m_xml; }

	private:		
		float m_weight;
		BodyList m_bodies;
		MotionMatchPoseFacing m_facing;

		std::string m_xml;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchGoalLocEntry
	{
	public:
		MotionMatchGoalLocEntry(const QueryElement& queryElement)
			: m_locName(queryElement.Element("locName"))
			, m_weight(ParseScalar<float>(queryElement, "weight", 0.0f))
			, m_minDist(ParseScalar<float>(queryElement, "minDist", 0.0f))
		{
		}

		const std::string& GetLocName() const { return m_locName.m_name; }

		float GetWeight() const { return m_weight; }
		float GetMinDist() const { return m_minDist; }

	protected:
		NameString m_locName;
		float m_weight;
		float m_minDist;
	};

	typedef ListFacade<MotionMatchGoalLocEntry> MmGoalLocList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchGoals
	{
	public:
		MotionMatchGoals(const QueryElement& elem);

		I32 GetNumSamples() const;
		float GetMaxSampleTime() const;
		float MasterWeight() const;
		float PositionWeight() const;
		float VelocityWeight() const;
		float DirectionWeight() const;
		float InterimDirectionWeight() const;
		float YawSpeedWeight() const;
		float BiasWeight() const;
		float GroupingWeight() const;
		float StoppingFaceDist() const;

		I32 GetNumSamplesPrevTrajectory() const;
		float GetMaxSampleTimePrevTrajectory() const;
		float PrevTrajectoryWeight() const;

		const std::string& Xml() const { return m_xml; }

	private:
		I32 m_numSamples;
		float m_maxSampleTime;
		float m_masterWeight;
		float m_positionWeight;
		float m_velocityWeight;
		float m_directionWeight;
		float m_interimDirectionWeight;
		float m_yawSpeedWeight;
		float m_biasWeight;
		I32 m_numSamplesPrevTraj;
		float m_maxSampleTimePrevTraj;
		float m_prevTrajWeight;
		float m_groupingWeight;
		float m_stoppingFaceDist;

		std::string m_xml;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchSetRef
	{
	public:
		std::string m_path;
		std::string m_name; // path stripped from all prefix and extension
		MotionMatchSetRef(const QueryElement& elem);
	};

	typedef ListFacade<MotionMatchSetRef> MotionMatchSetRefList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	//This class removes disabled anims form the list
	class MotionMatchAnimList : public AnimList
	{
	public:
		MotionMatchAnimList(const QueryElement& elem,
							const BundleRefList& bundleRefs,
							const AnimRefList& animRefs,
							const AnimRefList& linkedAnimRefs);
	};

	using AnimNameList = ListFacade<NameString>;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimGroupEntry
	{
	public:
		AnimGroupEntry(const QueryElement& queryElement)
			: m_animNames(queryElement.Element("animNames"), "anim")
			, m_groupIdValue(ParseScalar<int>(queryElement, "groupId", 0))
		{
		}


		I32F GetGroupId() const { return m_groupIdValue; }

		bool HasAnimInGroup(const std::string& animName) const
		{
			auto it = std::find_if(m_animNames.begin(),
								   m_animNames.end(),
								   [&animName](const NameString& entryAnim) -> bool {
									   return entryAnim.m_name == animName;
								   });
			return it != m_animNames.end();
		}

	protected:
		I32 m_groupIdValue;
		AnimNameList m_animNames;
	};

	typedef ListFacade<AnimGroupEntry> AnimGroupList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimLayerEntry
	{
	public:
		AnimLayerEntry(const QueryElement& queryElement)
			: m_animNames(queryElement.Element("animNames"), "anim")
			, m_layerName(queryElement.Element("layerName"))
		{
		}

		const std::string& GetLayerName() const { return m_layerName.m_name; }

		bool HasAnimInLayer(const std::string& animName) const
		{
			auto it = std::find_if(m_animNames.begin(),
								   m_animNames.end(),
								   [&animName](const NameString& entryAnim) -> bool {
				return entryAnim.m_name == animName;
			});
			return it != m_animNames.end();
		}

	protected:
		NameString m_layerName;
		AnimNameList m_animNames;
	};
	typedef ListFacade<AnimLayerEntry> AnimLayerList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchSet : public ElementFacade
	{
	public:
		MotionMatchSet(const QueryElement& elem);
		MotionMatchSet(const MotionMatchSet& other) = delete;

		const AnimList& Anims() const { return m_allAnimations; }		
		const AnimNameList& BiasedAnims() const { return m_biasedAnimList; }
		bool IsAnimBiased(const std::string& animName) const;
		I32F GetAnimGroupId(const std::string& animName) const;
		std::string GetAnimLayerName(const std::string& animName) const;
		const MotionMatchPose& Pose() const { return m_pose; }
		const MotionMatchGoals& Goals() const { return m_goals; }
		bool UseNormalizedScales() const { return m_useNormalizedScales; }
		bool UseNormalizedData() const { return m_useNormalizedData; }
		const AnimNameList& TrajClampBeginAnims() const { return m_trajClampBeginAnimList; }
		const AnimNameList& TrajClampEndAnims() const { return m_trajClampEndAnimList; }

		bool ShouldAnimTrajClampBegining(const std::string& animName) const;
		bool ShouldAnimTrajClampEnding(const std::string& animName) const;

		int MinNumSamples() const { return m_minNumSamples; }

		const MmGoalLocList& GetGoalLocs() const { return m_mmGoalLocList; }

	protected:
		virtual std::string Prefix() const { return "MotionMatchSet." + ElementFacade::Prefix(); }
		
		BundleRefList m_bundles;
		MotionMatchAnimList m_allAnimations;
		MotionMatchPose m_pose;
		AnimNameList m_biasedAnimList;
		AnimGroupList m_animGroupList;
		AnimLayerList m_animLayerList;
		MmGoalLocList m_mmGoalLocList;

		MotionMatchGoals m_goals;
		bool m_useNormalizedScales;
		bool m_useNormalizedData;
		int m_minNumSamples;
		AnimNameList m_trajClampBeginAnimList;
		AnimNameList m_trajClampEndAnimList;
	};
	using MotionMatchSetPtr = std::shared_ptr<const libdb2::MotionMatchSet>;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class MotionMatchSetList : public ListFacade<std::shared_ptr<MotionMatchSet>>
	{
	public:
		MotionMatchSetList(const QueryElement& elem, const MotionMatchSetRefList& refList);
	};
}
