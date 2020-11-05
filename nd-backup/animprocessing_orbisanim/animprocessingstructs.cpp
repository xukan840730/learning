/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animobjectrootanims.h"
#include "animcompression.h"
#include <map>

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

const ITGEOM::Vec3 kIdentityScale(1.0f, 1.0f, 1.0f);
const ITGEOM::Quat kIdentityRotation(0.0f, 0.0f, 0.0f, 1.0f);
const ITGEOM::Vec3 kIdentityTranslation(0.0f, 0.0f, 0.0f);

//-----------------

bool AnimSample::IsEqual(AnimSample const& other, float const tolerance)
{
	// use Euclidean distance
	float sumSqrs = 0;
	for (int i = 0; i < 4; ++i) {
		float d = fabsf(m_v[i] - other.m_v[i]);
		sumSqrs += d*d;
	}
	float const dist = sqrtf(sumSqrs);
	return dist <= tolerance;
}

bool SampledAnim::DetermineIfConstantVec3(float const tolerance)
{
	ITASSERT(m_samples.size()>0);
	bool isConstant = true;
	ITGEOM::Vec3 min = m_samples[0];
	ITGEOM::Vec3 max = m_samples[0];
	for (size_t i=1; isConstant && i<m_samples.size(); i++) {
		ITGEOM::Vec3 sample = m_samples[i];
		min.x = std::min(min.x, sample.x);	max.x = std::max(max.x, sample.x);
		min.y = std::min(min.y, sample.y);	max.y = std::max(max.y, sample.y);
		min.z = std::min(min.z, sample.z);	max.z = std::max(max.z, sample.z);
		isConstant = max.x - min.x <= tolerance*2.0f &&
					 max.y - min.y <= tolerance*2.0f &&
					 max.z - min.z <= tolerance*2.0f;
	}
	if (isConstant) {
		ITGEOM::Vec3 center = 0.5f * ( min + max );
		SetToConstant(center);
	}
	return isConstant;
}

bool SampledAnim::DetermineIfConstantQuat(float const tolerance)
{
	ITASSERT(m_samples.size()>0);
	bool isConstant = true;
	ITGEOM::Quat min = m_samples[0];
	ITGEOM::Quat max = m_samples[0];
	ITGEOM::Quat center = m_samples[0];
	for (size_t i=1; isConstant && i<m_samples.size(); i++) {
		ITGEOM::Quat sample = m_samples[i];
		sample = (Dot4(sample, center) < 0.0f) ? -sample : sample;
		min.x = std::min(min.x, sample.x);	max.x = std::max(max.x, sample.x);
		min.y = std::min(min.y, sample.y);	max.y = std::max(max.y, sample.y);
		min.z = std::min(min.z, sample.z);	max.z = std::max(max.z, sample.z);
		min.w = std::min(min.w, sample.w);	max.w = std::max(max.w, sample.w);
		center = 0.5f * ( min + max );
		isConstant = max.x - min.x <= tolerance*2.0f &&
					 max.y - min.y <= tolerance*2.0f &&
					 max.z - min.z <= tolerance*2.0f &&
					 max.w - min.w <= tolerance*2.0f;
	}
	if (isConstant) {
		center.Normalize();
		SetToConstant(center);
	}
	return isConstant;
}

bool SampledAnim::DetermineIfConstantFloat(float const tolerance)
{
	ITASSERT(m_samples.size()>0);
	bool isConstant = true;
	float min = m_samples[0];
	float max = m_samples[0];
	for (size_t i=1; isConstant && i<m_samples.size(); i++) {
		float sample = m_samples[i];
		min = std::min(min, sample);	
		max = std::max(max, sample);
		isConstant = max - min <= tolerance*2.0f;
	}
	if (isConstant) {
		float center = 0.5f * ( min + max );
		SetToConstant(center);
	}
	return isConstant;
}

bool SampledAnim::DetermineIfConstant(float const tolerance)
{
	switch(m_type) {
	case kChannelTypeScale:
	case kChannelTypeTranslation:
		return DetermineIfConstantVec3(tolerance);
	case kChannelTypeRotation:
		return DetermineIfConstantQuat(tolerance);
	case kChannelTypeScalar:
		return DetermineIfConstantFloat(tolerance);
	}
	return false;
}

bool SampledAnim::IsEqual(AnimSample const sample, float const tolerance)
{
	ITASSERT(m_samples.size() == 1);
	return m_samples[0].IsEqual(sample, tolerance);
}

//-----------------

// copy constructor
AnimationClipSourceData::AnimationClipSourceData(AnimationClipSourceData const& copy) 
	: m_flags(copy.m_flags)
	, m_numFrames(copy.m_numFrames)
	, m_pRootAnim(copy.m_pRootAnim ? copy.m_pRootAnim->ConstructCopy() : NULL)
{
	// copy anim channels
	for (size_t i=0; i<kNumChannelTypes; i++) {
		for (auto it = copy.m_channelAnims[i].begin(); it != copy.m_channelAnims[i].end(); it++) {
			Anim* channel = *it;
			if (channel) {
				m_channelAnims[i].push_back( channel->Clone() );
			} else {
				m_channelAnims[i].push_back( NULL );
			}
		}
	}
}

// destructor
AnimationClipSourceData::~AnimationClipSourceData()
{
	delete m_pRootAnim;
	for (auto type = m_channelAnims.begin(); type != m_channelAnims.end(); type++) {
		for (auto chan = type->begin(); chan != type->end(); chan++) {
			delete *chan;
		}
	}
}

// copy by assignment operator
AnimationClipSourceData const& AnimationClipSourceData::operator=(AnimationClipSourceData const& copy)
{
	// free any existing data
	delete m_pRootAnim;
	for (auto type = m_channelAnims.begin(); type != m_channelAnims.end(); type++) {
		for (auto chan = type->begin(); chan != type->end(); chan++) {
			delete *chan;
		}
		type->clear();
	}

	m_flags = copy.m_flags;
	m_numFrames = copy.m_numFrames;

	m_pRootAnim = copy.m_pRootAnim ? copy.m_pRootAnim->ConstructCopy() : NULL;
		
	// copy anim channels
	for (size_t i=0; i<kNumChannelTypes; i++) {
		for (auto it = copy.m_channelAnims[i].begin(); it != copy.m_channelAnims[i].end(); it++) {
			Anim* channel = *it;
			if (channel) {
				m_channelAnims[i].push_back( channel->Clone() );
			} else {
				m_channelAnims[i].push_back( NULL );
			}
		}
	}
	
	return *this;
}

// FIXME - this should be a constructor
void AnimationClipSourceData::Init(size_t numJointAnims, size_t numFloatAnims)
{
	m_flags = 0;
	m_numFrames = 0;
	for (size_t i=0; i<kNumJointChannels; i++) {
		m_channelAnims[i].resize(numJointAnims);
	}
	m_channelAnims[kChannelTypeScalar].resize(numFloatAnims);
}

// Determine which channels are constant within the tolerances given in the metadata
void AnimationClipSourceData::DetermineConstantChannels(ClipLocalSpaceErrors const& tolerances)
{
	for (size_t type=0; type<m_channelAnims.size(); type++) {
		for (size_t chan=0; chan<m_channelAnims[type].size(); chan++) {
			Anim* channel = m_channelAnims[type][chan];
			if (channel) {
				channel->DetermineIfConstant(tolerances.m_const[type][chan]);
			}
		}
	}
}

// Remove constant channels that are equal to the default pose using the given tolerances
void AnimationClipSourceData::RemoveConstantDefaultPoseChannels(ClipLocalSpaceErrors const& tolerances,
																AnimationHierarchyDesc const& hierarchyDesc,
																AnimationBinding const& binding)
{
	for (size_t type = 0; type < m_channelAnims.size(); type++) {
		for (size_t animId = 0; animId < m_channelAnims[type].size(); animId++) {
			int animatedId = -1;
			if (type == kChannelTypeScalar) {
				animatedId = binding.m_floatAnimIdToFloatId[animId];
			} else {
				animatedId = binding.m_jointAnimIdToJointId[animId];
			}
			if (animatedId < 0) {
				continue;
			}
			Anim* anim = m_channelAnims[type][animId];
			if (anim && anim->IsConstant()) {
				AnimSample defaultSample;
				switch (type) {
				case kChannelTypeScale:
					defaultSample = hierarchyDesc.m_joints[animatedId].m_jointScale;
					break;
				case kChannelTypeTranslation:
					defaultSample = hierarchyDesc.m_joints[animatedId].m_jointTranslation;
					break;
				case kChannelTypeRotation:
					defaultSample = hierarchyDesc.m_joints[animatedId].m_jointQuat;
					break;
				case kChannelTypeScalar:
					defaultSample = hierarchyDesc.m_floatChannels[animatedId].m_defaultValue;
					break;
				}
				if (anim->IsEqual(defaultSample, tolerances.m_const[type][animId])) {
					m_channelAnims[type][animId] = NULL;
					m_flags |= kClipConstsRemoved;
					delete anim;
				}
			}
		}
	}
}

void AnimationClipSourceData::SetNumFrames()
{
	m_numFrames = 0;
	for (size_t type = 0; type < m_channelAnims.size(); type++) {
		for (size_t chan = 0; chan < m_channelAnims[type].size(); chan++) {
			SampledAnim* channel = GetSampledAnim((ChannelType)type, chan);
			if (channel) {
				m_numFrames = std::max(m_numFrames, (unsigned)channel->GetNumSamples());
			}
		}
	}
	if (m_numFrames == 0) {
		m_flags &= ~kClipLooping;	// single frame animations can not be looping
	}
}

//-----------------

AnimationClipCompressedData::~AnimationClipCompressedData()
{
	for (size_t c = 0; c < kNumChannelTypes; ++c) {
		for (size_t i = 0; i < m_anims[c].size(); ++i) {
			delete m_anims[c][i].m_pCompressedArray;
		}
		for (size_t i = 0; i < m_const[c].size(); ++i) {
			delete m_const[c][i].m_pCompressedArray;
		}
	}
	delete m_pRootAnim;
}

/// resizes all anim & const vectors to zero
void AnimationClipCompressedData::Clear()
{
	for (size_t i = 0; i < kNumChannelTypes; i++) {
		m_anims[i].resize(0);
		m_const[i].resize(0);
	}
}

//-----------------

void ClipStats::start(std::string name, size_t loc)
{
	Section section;
	section.m_name = name;
	section.m_location = loc;
	section.m_size = 0;
	m_stack.push_back(section);
}

void ClipStats::end(size_t loc)
{
	ITASSERT(m_stack.size());
	Section s = m_stack.back();
	s.m_size = loc - s.m_location;
	m_sections.push_back(s);
	m_stack.pop_back();
	m_maxSectionNameLength = std::max(m_maxSectionNameLength, s.m_name.size());
}

void ClipStats::report(std::string name)
{
	if (m_sections.size()) {
		INOTE(IMsg::kQuiet, "ClipStats: %s\n", name.c_str());
		size_t totalSize = 0;
		for (auto it = m_sections.begin(); it != m_sections.end(); it++) {
			INOTE(IMsg::kQuiet, "    %-*s%d\n", 2+m_maxSectionNameLength, (it->m_name+":").c_str(), it->m_size);
			totalSize += it->m_size;
		}
		INOTE(IMsg::kQuiet, "Total bytes: %d\n", totalSize);
	}
}

void ClipStats::reportCsvHdr(FILE* fp)
{
	if (fp) {
		std::string header = "clipName";
		for (auto it = m_sections.begin(); it != m_sections.end(); it++) {
			header += "," + it->m_name;
		}
		fprintf_s(fp, "%s\n", header.c_str());
	}
}

void ClipStats::reportCsv(FILE* fp)
{
	if (fp) {
		if (m_sections.size()) {
			std::string row = m_clipName;
			for (auto it = m_sections.begin(); it != m_sections.end(); it++) {
				std::ostringstream sizeString; sizeString << it->m_size;
				row	 += ", " + sizeString.str();
			}
			fprintf_s(fp, "%s\n", row.c_str());
		}
	}
}

void ClipStats::reportCsvTransposed(FILE* fp, std::vector<ClipStats>& clipStats)
{
	if (!fp || clipStats.empty()) {
		return;
	}

	// each ClipStats can have a different number of sections & multiple sections with the same name
	// so first we merge the multiples together to get unique section totals per ClipStats
	// then we find the global list of sections (across all ClipStats) and rebuild each ClipStats 
	// so that we can report them all uniformly
	std::set<std::string> unionSectionNames;
	for (auto it = clipStats.begin(); it != clipStats.end(); it++) {
		it->sumSections();
		for (auto itSection = it->m_sections.begin(); itSection != it->m_sections.end(); itSection++) {
			unionSectionNames.emplace(itSection->m_name);
		}
	}
	for (auto it = clipStats.begin(); it != clipStats.end(); it++) {
		it->resizeSections(unionSectionNames);
	}

	size_t const numSections = clipStats[0].m_sections.size();
	
	size_t const maxFnameLen = 255;
	char fname[maxFnameLen]; 

	// first row is clip names
	std::string csv = "clipName";
	for (auto it = clipStats.begin(); it!=clipStats.end(); it++) {
		_splitpath_s(it->m_clipName.c_str(), NULL, 0, NULL, 0, fname, maxFnameLen, NULL, 0);
		csv += ", " + std::string(fname);
	}
	csv += "\n";
	// subsequent rows are the sections
	for (size_t s=0; s<numSections; s++) {
		csv += clipStats[0].m_sections[s].m_name;
		for (auto it = clipStats.begin(); it!=clipStats.end(); it++) {
			size_t i = std::min(s, it->m_sections.size()-1);	// prevent vector subscript out of range if we don't have all sections (compressed differently to first clip)
			std::ostringstream sizeString; sizeString << it->m_sections[i].m_size;
			csv	 += ", " + sizeString.str();
		}
		csv += "\n";
	}
	// sections total size == clip size
	csv += "clipTotalSize";
	for (auto it = clipStats.begin(); it!=clipStats.end(); it++) {
		size_t totalSize = 0;
		for (size_t s=0; s<numSections; s++) {
			size_t i = std::min(s, it->m_sections.size()-1);	// prevent vector subscript out of range if we don't have all sections (compressed differently to first clip)
			totalSize += it->m_sections[i].m_size;
		}
		std::ostringstream sizeString; sizeString << totalSize;
		csv	 += ", " + sizeString.str();
	}
	csv += "\n";
	// add compression stats
	#define COMPSTATS(_s, _m)														\
		csv += _s;																	\
		for (auto it = clipStats.begin(); it!=clipStats.end(); it++) {				\
			std::ostringstream sizeString; sizeString << it->m_compressionStats._m;	\
			csv	 += ", " + sizeString.str();										\
		}																			\
		csv += "\n";
	
	COMPSTATS("numAnimScales", m_numScaleJoints);
	COMPSTATS("numAnimQuats", m_numQuatJoints);
	COMPSTATS("numAnimTrans", m_numTransJoints);
	COMPSTATS("numAnimFloats", m_numFloatChannels);
	COMPSTATS("numConstScales", m_numConstChannels[kChannelTypeScale]);
	COMPSTATS("numConstQuats", m_numConstChannels[kChannelTypeRotation]);
	COMPSTATS("numConstTrans", m_numConstChannels[kChannelTypeTranslation]);
	COMPSTATS("numConstFloats", m_numConstChannels[kChannelTypeScalar]);
	COMPSTATS("numConstScalesRemoved", m_numConstChannelsRemoved[kChannelTypeScale]);
	COMPSTATS("numConstQuatsRemoved", m_numConstChannelsRemoved[kChannelTypeRotation]);
	COMPSTATS("numConstTransRemoved", m_numConstChannelsRemoved[kChannelTypeTranslation]);
	COMPSTATS("numConstFloatsRemoved", m_numConstChannelsRemoved[kChannelTypeScalar]);

	COMPSTATS("uncompressedSize", m_uncompressedSize);
	COMPSTATS("constDataSize", m_sizeofConsts);
	COMPSTATS("constantCompressedSize", m_constantCompressedSize);
	COMPSTATS("bitCompressedSize", m_bitCompressedSize);
	COMPSTATS("keyCompressedSize", m_keyCompressedSize);
	COMPSTATS("keyCompressedSizeNoOverheads", m_keyCompressedSizeNoOverheads);

	// animated channels by compression type
	COMPSTATS("numUncompressedScales", m_numScalesByType[0]);
	COMPSTATS("numFloat16Scales", m_numScalesByType[1]);
	COMPSTATS("numRangeScales", m_numScalesByType[2]);
	COMPSTATS("numUncompressedQuats", m_numQuatsByType[0]);
	COMPSTATS("numSmallest3Quats", m_numQuatsByType[1]);
	COMPSTATS("numLogQuats", m_numQuatsByType[2]);
	COMPSTATS("numLogPcaQuats", m_numQuatsByType[3]);
	COMPSTATS("numUncompressedTrans", m_numTransByType[0]);
	COMPSTATS("numFloat16Trans", m_numTransByType[1]);
	COMPSTATS("numRangeTrans", m_numTransByType[2]);
	COMPSTATS("numUncompressedFloats", m_numFloatsByType[0]);

	// constant channels by compression type
	COMPSTATS("numConstUncompressedScales", m_numConstScalesByType[0]);
	COMPSTATS("numConstFloat16Scales", m_numConstScalesByType[1]);
	COMPSTATS("numConstUncompressedQuats", m_numConstQuatsByType[0]);
	COMPSTATS("numConst48Smallest3Quats", m_numConstQuatsByType[1]);
	COMPSTATS("numConstUncompressedTrans", m_numConstTransByType[0]);
	COMPSTATS("numConstFloat16Trans", m_numConstTransByType[1]);
	COMPSTATS("numConstUncompressedFloats", m_numConstFloatsByType[0]);
	
	// number of quat log channels compressed with cross-correlation
	COMPSTATS("numCrossCorrelatedQuatLog", m_numCrossCorrelatedQuatLog);
	COMPSTATS("numCrossCorrelatedQuatLogPca", m_numCrossCorrelatedQuatLogPca);

	fprintf_s(fp, "%s", csv.c_str());
}

void ClipStats::sumSections()
{
	std::map<std::string, Section> summedSections;
	for (auto it = m_sections.begin(); it != m_sections.end(); it++) {
		auto summed = summedSections.find(it->m_name);
		if (summed != summedSections.end()) {
			summed->second.m_size += it->m_size;
		} else {
			summedSections[it->m_name] = *it;
		}
	}
	m_sections.clear();
	for (auto it = summedSections.begin(); it != summedSections.end(); it++) {
		m_sections.push_back(it->second);
	}
}

void ClipStats::resizeSections(std::set<std::string> sectionNames)
{
	// 1. set up map from names to uninitialized sections
	std::map<std::string, Section> sections;
	for (auto it = sectionNames.begin(); it != sectionNames.end(); it++) {
		sections[*it] = Section(*it);
	}
	// 2. now copy the size into the section with the right name
	for (auto it = m_sections.begin(); it != m_sections.end(); it++) {
		auto s = sections.find(it->m_name);
		if (s != sections.end()) {
			s->second.m_location = it->m_location;
			s->second.m_size = it->m_size;
		}
	}
	// 3. and overwrite our original sections
	m_sections.clear();
	for (auto it = sections.begin(); it != sections.end(); it++) {
		m_sections.push_back(it->second);
	}
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
