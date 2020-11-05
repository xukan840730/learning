/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/libs/libdb2/db2-motion-match-set.h"

#include "common/util/msg.h"

#include <iostream>

//#pragma optimize("", off)

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchSetList::MotionMatchSetList(const QueryElement& queryElement,
											   const MotionMatchSetRefList& refList)
	: ListFacade<std::shared_ptr<MotionMatchSet>>(queryElement)
{
	// Dereference all the set references
	for (const auto& mmSetRef : refList)
	{
		const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("motionmatchset", mmSetRef.m_name);
		bool foundSet = false;
		if (pDbElem)
		{
			QueryElement queryElem(pDbElem);
			m_elements.emplace_back(std::make_shared<MotionMatchSet>(queryElem.Element("motionMatchSet")));

			INOTE_DEBUG("Motion match set found: %s\n", mmSetRef.m_path.c_str());
			foundSet = true;
		}
		if (!foundSet)
		{
			IABORT("Motion match set not found: %s\n", pDbElem ? pDbElem->DiskPath().c_str() : mmSetRef.m_path.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchSet::MotionMatchSet(const QueryElement& queryElement)
	: ElementFacade(queryElement)
	, m_bundles(queryElement.Element("bundles"), "ref")
	, m_allAnimations(queryElement, m_bundles, AnimRefList(QueryElement::null, ""), AnimRefList(QueryElement::null, ""))
	, m_pose(queryElement.Element("pose"))
	, m_biasedAnimList(queryElement.Element("animBias"), "anim")
	, m_animGroupList(queryElement.Element("animGroups"), "entry")
	, m_animLayerList(queryElement.Element("animLayers"), "entry")
	, m_mmGoalLocList(queryElement.Element("goalLocs"), "entry")
	, m_goals(queryElement.Element("goals"))
	, m_useNormalizedScales(queryElement.Value("useNormalizedScales").first
							&& queryElement.Value("useNormalizedScales").second == "true")
	, m_useNormalizedData(queryElement.Value("useNormalizedData").first
								&& queryElement.Value("useNormalizedData").second == "true")
	, m_trajClampBeginAnimList(queryElement.Element("animTrajClampBegin"), "anim")
	, m_trajClampEndAnimList(queryElement.Element("animTrajClampEnd"), "anim")
{
	EnumeratedFacade<I32, 1> minNumSamplesElem(queryElement, "minNumSamples");

	if (minNumSamplesElem.FullyParsed())
	{
		m_minNumSamples = minNumSamplesElem.ParsedValue();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool libdb2::MotionMatchSet::IsAnimBiased(const std::string& animName) const
{
	auto it = std::find_if(m_biasedAnimList.begin(),
						   m_biasedAnimList.end(),
						   [&animName](const NameString& biasAnim) -> bool { return biasAnim.m_name == animName; });
	return it != m_biasedAnimList.end();
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F libdb2::MotionMatchSet::GetAnimGroupId(const std::string& animName) const
{
	I32F groupId = 0;

	for (const AnimGroupEntry& entry : m_animGroupList)
	{
		if (entry.HasAnimInGroup(animName))
		{
			groupId = entry.GetGroupId();
			break;
		}
	}

	return groupId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::string libdb2::MotionMatchSet::GetAnimLayerName(const std::string& animName) const
{
	std::string name = "default";

	bool found = false;

	for (const AnimLayerEntry& entry : m_animLayerList)
	{
		if (entry.HasAnimInLayer(animName))
		{
			IABORT_IF(found,
					  "Anim '%s' found in multiple layers ('%s' and '%s')",
					  animName.c_str(),
					  name.c_str(),
					  entry.GetLayerName().c_str());

			name = entry.GetLayerName();
			break;
		}
	}

	return name;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool libdb2::MotionMatchSet::ShouldAnimTrajClampBegining(const std::string& animName) const
{
	auto it = std::find_if(m_trajClampBeginAnimList.begin(),
						   m_trajClampBeginAnimList.end(),
						   [&animName](const NameString& biasAnim) -> bool { return biasAnim.m_name == animName; });
	return it != m_trajClampBeginAnimList.end();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool libdb2::MotionMatchSet::ShouldAnimTrajClampEnding(const std::string& animName) const
{
	auto it = std::find_if(m_trajClampEndAnimList.begin(),
						   m_trajClampEndAnimList.end(),
						   [&animName](const NameString& biasAnim) -> bool { return biasAnim.m_name == animName; });
	return it != m_trajClampEndAnimList.end();
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchSetRef::MotionMatchSetRef(const QueryElement& queryElement)
	: m_path(queryElement.Value("path").second)
	, m_name(m_path.substr(0, m_path.rfind(".motionmatchset.xml")))
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchPose::MotionMatchPose(const QueryElement& queryElement)
	: m_weight(ParseScalar<float>(queryElement, "weight", 0.0f))
	, m_bodies(queryElement.Element("bodies"), "body")
	, m_facing(queryElement.Element("facingAxis"))
	, m_xml(queryElement.Xml())
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
const libdb2::MotionMatchPose::BodyList& libdb2::MotionMatchPose::Bodies() const
{
	return m_bodies;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchPose::Weight() const
{
	return m_weight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const libdb2::MotionMatchPoseFacing& libdb2::MotionMatchPose::Facing() const
{
	return m_facing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchPoseFacing::MotionMatchPoseFacing(const QueryElement& queryElement)
	: m_jointName(queryElement.Value("jointName").first ? queryElement.Value("jointName").second : "")
	, m_axis(ParseVector(queryElement, "axis", SMath::kUnitZAxis))
	, m_weight(ParseScalar<float>(queryElement, "weight", 0.0f))
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::string& libdb2::MotionMatchPoseFacing::JointName() const
{
	return m_jointName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SMath::Vector libdb2::MotionMatchPoseFacing::Axis() const
{
	return m_axis;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchPoseFacing::Weight() const
{
	return m_weight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchPoseBody::MotionMatchPoseBody(const QueryElement& queryElement)
	: m_jointName(queryElement.Value("jointName").first ? queryElement.Value("jointName").second : "")
	, m_isCenterOfMass(queryElement.Value("centerOfMass").first && queryElement.Value("centerOfMass").second == "true")
	, m_positionWeight(ParseScalar<float>(queryElement, "positionWeight", 0.0f))
	, m_velocityWeight(ParseScalar<float>(queryElement, "velocityWeight", 0.0f))
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool libdb2::MotionMatchPoseBody::IsCenterOfMass() const
{
	return m_isCenterOfMass;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::string& libdb2::MotionMatchPoseBody::JointName() const
{
	return m_jointName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchPoseBody::PositionWeight() const
{
	return m_positionWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchPoseBody::VelocityWeight() const
{
	return m_velocityWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchGoals::MotionMatchGoals(const QueryElement& queryElement)
	: m_numSamples(ParseScalar<int>(queryElement, "numTrajectorySamples", 0))
	, m_maxSampleTime(ParseScalar<float>(queryElement, "maxTrajectoryTime", 0.0f))
	, m_masterWeight(ParseScalar<float>(queryElement, "masterWeight", 0.0f))
	, m_positionWeight(ParseScalar<float>(queryElement, "positionWeight", 0.0f))
	, m_velocityWeight(ParseScalar<float>(queryElement, "velocityWeight", 0.0f))
	, m_directionWeight(ParseScalar<float>(queryElement, "directionalWeight", 0.0f))
	, m_interimDirectionWeight(ParseScalar<float>(queryElement, "interimDirectionalWeight", 0.0f))
	, m_yawSpeedWeight(ParseScalar<float>(queryElement, "yawSpeedWeight", 0.0f))
	, m_biasWeight(ParseScalar<float>(queryElement, "animBiasWeight", 0.0f))
	, m_numSamplesPrevTraj(ParseScalar<int>(queryElement, "numPrevTrajectorySamples", 0))
	, m_maxSampleTimePrevTraj(ParseScalar<float>(queryElement, "maxPrevTrajectoryTime", 0.0f))
	, m_prevTrajWeight(ParseScalar<float>(queryElement, "prevTrajWeight", 0.0f))
	, m_groupingWeight(ParseScalar<float>(queryElement, "groupingWeight", 0.0f))
	, m_stoppingFaceDist(ParseScalar<float>(queryElement, "stoppingFaceDist", -1.0f))
	, m_xml(queryElement.Xml())
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 libdb2::MotionMatchGoals::GetNumSamples() const
{
	return m_numSamples;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::GetMaxSampleTime() const
{
	return m_maxSampleTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::MasterWeight() const
{
	return m_masterWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::PositionWeight() const
{
	return m_positionWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::VelocityWeight() const
{
	return m_velocityWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::DirectionWeight() const
{
	return m_directionWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::InterimDirectionWeight() const
{
	return m_interimDirectionWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::YawSpeedWeight() const
{
	return m_yawSpeedWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::BiasWeight() const
{
	return m_biasWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::GroupingWeight() const
{
	return m_groupingWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::StoppingFaceDist() const
{
	return m_stoppingFaceDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 libdb2::MotionMatchGoals::GetNumSamplesPrevTrajectory() const
{
	return m_numSamplesPrevTraj;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::GetMaxSampleTimePrevTrajectory() const
{
	return m_maxSampleTimePrevTraj;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float libdb2::MotionMatchGoals::PrevTrajectoryWeight() const
{
	return m_prevTrajWeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::MotionMatchAnimList::MotionMatchAnimList(const QueryElement& elem,
												 const BundleRefList& bundleRefs,
												 const AnimRefList& animRefs,
												 const AnimRefList& linkedAnimRefs)
	: AnimList(elem, bundleRefs, animRefs, linkedAnimRefs)
{
	m_elements.erase(std::remove_if(m_elements.begin(),
									m_elements.end(),
									[](const Anim* pAnim) { return pAnim->m_flags.m_Disabled; }),
					 m_elements.end());
}
