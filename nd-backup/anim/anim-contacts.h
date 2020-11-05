/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/anim/effect-group.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/scriptx/h/eff-generator-defines.h"
#include "ndlib/util/maybe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
class Locator;

/// --------------------------------------------------------------------------------------------------------------- ///
struct OrientedBox
{
	Locator m_loc;
	Aabb m_aab;
};

struct Line
{
	Point m_p;
	Vector m_v;
};

struct ContactInfo
{
	Line m_contact;
	float m_distContactToJoint;
	float m_distContactToBody;
	Segment m_contactOnBody;
	float m_bodyEnergy;
	float m_bodyDistToGround;
	float m_closestPointDist;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void GetContactsFromAnim(const ArtItemAnim* pAnim,
						 const StringId64 jointId,
						 const Locator firstFrameAlignLoc,
						 int frameIndex,
						 const OrientedBox& jointBody,
						 ListArray<Maybe<ContactInfo>>& outContactInfo,
						 ListArray<Locator>* pOutTransforms = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
class JointEffGenerator
{
public:
	using BoolList = BitArray<2048>;

	JointEffGenerator(ArtItemAnimHandle anim, const StringId64 jointId, const OrientedBox& jointBody);
	JointEffGenerator(ArtItemAnimHandle anim,
					  const DC::EffJointGenerator& settings,
					  const DC::EffThresholds& thresholds);
	~JointEffGenerator();

	void Draw(const Locator firstFrameAlignLoc, int frameIndex) const;
	void GenerateEFFs(const StringId64 jointId,
					  const StringId64 effectName,
					  ListArray<EffectAnimEntry>& outEffList) const;
	ArtItemAnimHandle Anim() const;
	bool SetGroundDistThreshold(const float groundThresh);
	bool SetContactDistThreshold(const float contactThresh);
	bool SetWindowFilter(const int w);

private:
	DC::EffJointGenerator m_settings;
	float m_distToGroundThresh	= 0.02f;
	float m_distToBodyThresh	= 0.06f;
	float m_energyThresh		= 0.53f;
	float m_closestPtDistThresh = 0.3f;
	int m_windowFilterSize		= 2;

	bool IsInContact(const ContactInfo& c) const;
	BoolList GetContactStates() const;

	StringId64 m_jointId;
	ArtItemAnimHandle m_animHandle;
	OrientedBox m_jointBody;
	ListArray<Maybe<ContactInfo>> m_contacts;
	ListArray<Locator> m_jointTransformsWs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EffGenerator
{
public:
	static bool CanGenerateForAnim(const ArtItemAnim* pAnim);
	static bool GenerateEffFileForActor(const char* pActorName, const char* pSkelActor);
	static DMENU::Menu* CreateMenu();

	EffGenerator(ArtItemAnimHandle anim);
	~EffGenerator();

	ArtItemAnimHandle Anim() const;
	void Update();
	void Draw(const Locator firstFrameAlignLoc, int frameIndex) const;

	void GenerateDebug() const;
	void GenerateTxt(FILE* pFile) const;

private:
	static const DC::EffGeneratorSet* LookupSettings(const ArtItemAnim* pAnim);

	void InitFromDc(ArtItemAnimHandle anim, const DC::EffGeneratorSet* pSet);

	enum EffGen
	{
		kRAnkle,
		kRBall,
		kLAnkle,
		kLBall,
		kEffJointCount
	};

	DC::EffGeneratorSet m_dcSettings;
	JointEffGenerator* m_apJoints[kEffJointCount] = {};
};
