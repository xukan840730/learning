/*
* Copyright (c) 2013 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/anim/anim-table.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/process/process-handles.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/script/script-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;

#ifndef FINAL_BUILD
#define JOINT_LIMIT_DEBUG
#endif

class ArtItemAnim;
class ArtItemSkeleton;
class JointCache;
class JointSet;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct IkConstraint;
	struct IkConstraintInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct JointConstraintData
{
public:
	Locator		m_bindPoseLs;
	Vector		m_coneVisPointPs;
	Quat		m_coneRotPs;
	StringId64	m_jointName;
	Vector*		m_coneBoundaryPointsPs;
	Vector*		m_coneSlicePlanePs;
	Vector*		m_coneBoundaryPlanePs;
	float*		m_coneBoundryTwistCenter;
	float*		m_coneBoundryTwistRange;
	int			m_coneNumBoundaryPoints;
	float		m_visPointTwistCenter;
	float		m_visPointTwistRange;
	U32			m_debugFlags;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ConstraintFuncResult
{
	Locator m_jointLocPs;
	Vector m_jointFwdPs;
	I32 m_sliceNum;
	float m_targetTwist;
	bool m_inCone;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JointLimits
{
private:
	typedef ScriptPointer<DC::IkConstraintInfo> IkConstraintsPtr;

public:
	struct JointData
	{
		enum SetupType
		{
			kSetupInvalid,
			kSetupAnim,
			kSetupConePoint,
			kSetupElliptical,
		};

		JointConstraintData m_data;

		StringId64	m_jointName;
		int			m_jointIndex;
		SetupType	m_setupType;

		JointData();

#ifdef JOINT_LIMIT_DEBUG
		bool		m_debugDraw;
		bool		m_debugMirror;

		mutable int			m_debugConeSlice;
		mutable float		m_debugPrevSliceDot;
		mutable float		m_debugNextSliceDot;
		mutable float		m_debugBoundryDot;
		mutable float		m_debugWeightCenter;
		mutable float		m_debugWeightPrev;
		mutable float		m_debugWeightNext;
		mutable float		m_debugTwist;
		mutable float		m_debugTwistCenter;
		mutable float		m_debugTwistRange;
		mutable bool		m_debugAppliedThisFrame;
		void DumpToTTY();
#endif
	};

	struct JointSetMap
	{
		// Maps a joint set offset to a joint limit index to avoid having to search
		// every time constraints are applied.
		short* m_jointSetOffset;
		short* m_jointLimitIndex;
		int m_numJoints;

		JointSetMap();
		void Init(int numJoints);
		void Destroy();

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	};

private:
	//MutableNdGameObjectHandle	m_hGo;
	ArtItemSkeletonHandle	m_hSkel;
	IkConstraintsPtr	m_dcIkConstraints;

	JointData*			m_jointData;
	int					m_numJoints;


	enum
	{
		kDebugDrawBindPoseLoc	= 0x1,
		kDebugDrawCone			= 0x2,
		kDebugDrawConeAxes		= 0x4,
		kDebugDrawConeVisPoint	= 0x8,
		kDebugDrawConeTwist		= 0x10,
		kDebugDrawJointDir		= 0x20,
		kDebugDrawText			= 0x40,
		kDebugDrawSwingLimitHit	= 0x80,
		kDebugDrawTwistLimitHit	= 0x100,
		kDebugValidateConeTwist	= 0x200,

		kDebugDrawDefault		= kDebugDrawCone | kDebugDrawConeAxes | kDebugDrawConeVisPoint | kDebugDrawConeTwist | kDebugDrawJointDir
	};

public:
#ifdef JOINT_LIMIT_DEBUG
	bool				m_debugDraw;
	U32					m_debugDrawFlags;
	float				m_debugDrawConeSize;
	mutable float		m_debugTextY;
	bool				m_debugRefreshMenu;
	mutable bool		m_debugDisableLimitsDisable;
	bool				m_debugDisableLimits;
#endif




public:
	JointLimits();
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	bool SetupJoints(NdGameObject* pGo, StringId64 dcIkConstraintId);
	bool SetupJoints(ArtItemSkeletonHandle hSkel, const AnimTable* pAnimTable, StringId64 dcIkConstraintId);
	bool SetupJointConstraintEllipticalCone(JointData* pJointData, const DC::IkConstraint* pConstraint);
	static void SetupJointConstraintEllipticalCone(JointConstraintData* ikData, const DC::IkConstraint* pConstraint);
	bool SetupJointConstraintConePoints(JointData* pJointData, const DC::IkConstraint* pConstraint);
	bool SetupJointConstraintAnim(const AnimTable* pAnimTable,
								  JointData* pJointData,
								  const DC::IkConstraint* pConstraint);
	bool Reload(NdGameObject* pGo);

	Locator GetAnimJointParamsLs(const ArtItemSkeleton* pSkel,
								 const ArtItemAnim* pAnim,
								 float frame,
								 int jointIndex,
								 int parentJointIndex,
								 bool parentMirror);

	int GetNumJoints();
	JointData* GetJointData(int jointNum);

	StringId64 GetSettingsId() const { return m_dcIkConstraints.GetId(); }

	void ApplyJointLimits(JointSet* pJoints) const; // slow version, does linear search for each entry
	void ApplyJointLimits(JointSet* pJoints, JointSetMap* pJointSetMap) const;
	void ApplyJointLimit(JointSet* pJoints, int jointSetOffset, int jointLimitIndex) const;
	static void ApplyJointLimit(JointSet* pJoints,
								const JointConstraintData* ikData,
								const JointData* optionalJointData,
								int jointSetOffset,
								ConstraintFuncResult* outResult);

	static float ComputeTwistRel(Quat_arg rot, Quat_arg baseRot, float refRot);			// Twist relative to a reference value
	static float ComputeTwistAbs(Quat_arg rot, Quat_arg baseRot);						// Absolute twist
	static void ComputeTwistRange(Quat_arg rotMin, Quat_arg rotMax, Quat_arg rotCenter, float* twistCenter, float* twistRange);

	// Debug funcs
	//struct DebugMenuData
	//{
	//	NdGameObject m_hGo;
	//	int m_jointNum;
	//	U32 m_data;
	//};

#ifdef JOINT_LIMIT_DEBUG
	void DebugDraw(const NdGameObject* pGo) const;
	void DebugDrawLimit(int jointLimitIndex, const NdGameObject* pGo, const JointCache& jointCache) const;

	void DebugApplyJointLimits() const;

	friend void JointLimitUpdateDebugMenu(DMENU::Menu* pMenu);
	friend bool JointLimitExecuteDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg);
	friend bool JointLimitExecuteObjectDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg);
	friend void DebugDrawJointLimits();
	friend void JointLimitReloadAll();

	friend float JointLimitObjectConeSizeFunc(DMENU::Item& item, DMENU::Message message, float desiredValue, float oldValue);
	friend bool JointLimitObjectDrawConeFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawConeAxesFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawConeVisPointFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawConeTwistFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectValidateConeTwistFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawJointDirFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawBindPoseLocFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawTextFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDisableLimitsFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawSwingLimitExceededFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitObjectDrawTwistLimitExceededFunc(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitOutputSelected(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitOutputAll(DMENU::Item& item, DMENU::Message message);
	friend bool JointLimitReload(DMENU::Item& item, DMENU::Message message);
#endif
};

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef JOINT_LIMIT_DEBUG

void JointLimitUpdateDebugMenu(DMENU::Menu* pMenu);
bool JointLimitExecuteDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg);
bool JointLimitExecuteObjectDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg);
void DebugDrawJointLimits();
void JointLimitReloadAll();

float JointLimitObjectConeSizeFunc(DMENU::Item& item, DMENU::Message message, float desiredValue, float oldValue);
bool JointLimitObjectDrawConeFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawConeAxesFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawConeVisPointFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawConeTwistFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectValidateConeTwistFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawJointDirFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawBindPoseLocFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawTextFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDisableLimitsFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawSwingLimitExceededFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitObjectDrawTwistLimitExceededFunc(DMENU::Item& item, DMENU::Message message);
bool JointLimitOutputSelected(DMENU::Item& item, DMENU::Message message);
bool JointLimitOutputAll(DMENU::Item& item, DMENU::Message message);
bool JointLimitReload(DMENU::Item& item, DMENU::Message message);

#else

inline void DebugDrawJointLimits() {}
inline bool JointLimitExecuteObjectDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg) {return false;}
inline bool JointLimitExecuteDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg) {return false;}

#endif
