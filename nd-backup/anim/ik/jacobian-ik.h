/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/locator.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/ik/vector-matrix.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class JacobianMap;
class JointSet;

namespace DC
{
	struct IkConstraintInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IkGoal
{
public:
	enum GoalType
	{
		kGoalNone,
		kPosition,
		kPlane,
		kPlaneSide,
		kRotation,
		kLookTarget,
		kLookTargetWithY,
		kLookDir,
		kLine
	};

	enum LookTargetAxis
	{
		kLookTargetAxisY,
		kLookTargetAxisYNeg,
		kLookTargetAxisZ,
		kLookTargetAxisZNeg,
	};

	GoalType GetGoalType() const { return m_type; }

	void SetGoalPosition(Point_arg pos, bool validate = false);
	void SetGoalPlane(Plane plane);
	void SetGoalPlaneSide(Plane plane);
	void SetGoalRotation(Quat_arg rot, bool validate = false);
	void SetGoalLookTarget(Point_arg targetPos, LookTargetAxis lookAxis, Vector_arg upVec);
	void SetGoalLookTargetWithY(Point_arg targetPos, LookTargetAxis lookAxis, Vector_arg upVec);
	void SetGoalLookDir(Vector_arg targetDir, Vector_arg lookAxisLS);
	void SetGoalLine(Point_arg targetPos, Vector_arg lineDir);
	void SetStrength(float t) { m_strength = Limit01(t); }

	Point GetGoalPosition() const;
	Plane GetGoalPlane() const;
	Plane GetGoalPlaneSide() const;
	Quat GetGoalRotation() const;
	void GetGoalLookTarget(Point* targetPos, LookTargetAxis* lookAxis, Vector* upVec) const;
	void GetGoalLine(Point* pTargetPt, Vector* pTargetLine) const;
	float GetStrength() const { return m_strength;}

	const Vector ComputeError(const Locator& endEffectorLocator, const float maxErrDist) const;
	const Vector ComputeJacobian( short jointOffset,
								  Vector_arg axis,
								  const char dofType,
								  const Locator& jointLoc,
								  const Locator& endEffectorPos,
								  const float rotationGoalFactor) const;

	static const Vector LookAtAxisFromLocator(const Locator& loc, LookTargetAxis targetAxis);

private:
	GoalType	m_type = kGoalNone;
	Vec4		m_goal;

	Vec4		m_goalDataVec4A;
	I32			m_goalDataIntA;

	float		m_strength = 1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianIkInstance
{
public:
	enum { kMaxNumGoals = 16 };

	JointSet*		m_pJoints = nullptr;
	JacobianMap*	m_pJacobianMap = nullptr;
	JointLimits*	m_pJointLimits = nullptr;
	IkGoal			m_goal[kMaxNumGoals];

	int				m_maxIterations = 0;
	float			m_errTolerance = 0.0f;
	float			m_blend = 1.0f;
	float			m_restoreFactor = 1.0f;
	float			m_maxDeltaAngleDeg = -1.0f;		// max degrees joint is allow to rotate per frame.
	float			m_solverDampingFactor = 0.5f;
	float			m_maxError = -1.0f;
	bool			m_disableJointLimits = false;
	bool			m_debugDrawJointLimits = false;

	// Ensure all WS joint locs in JointSet are up to date, not just ones in the jacobian map. Set to false if working on different
	// parts of a JointTree in parallel. If false, the parent joint loc to the jacobian map must be valid or you'll get an Assert.
	bool			m_updateAllJointLocs = true;
	bool			m_forceEnable = false;	// Enable even if all IK is disabled

	const DC::IkConstraintInfo* m_pConstraints = nullptr;		// deprecated
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianIkResult
{
public:
	JacobianIkResult()
		: m_solved(false)
		, m_iteration(0)
		, m_sqrErr(0.0f)
	{}

	bool			m_solved;
	I32				m_iteration;
	float			m_sqrErr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianDebugInfo
{
public:
	JacobianDebugInfo()
		: m_numUniqueJoints(0)
		, m_numJoints(0)
		, m_maxIterations(0)
		, m_numIterationsUsed(0)
		, m_errSqr(0.0f)
		, m_time(TimeFrameNegInfinity())
		, m_preJointIkOffset(nullptr)
		, m_postJointIkOffset(nullptr)
		, m_postJointLocWs(nullptr)
		, m_uniqueJointNames(nullptr)
		, m_finalDeltaAngles(nullptr)
	{
	}

	void Init(const JacobianMap* pJacobian);
	void Clear();
	void CopyFrom(const JacobianDebugInfo& other);

	~JacobianDebugInfo()
	{
		NDI_DELETE[] m_preJointIkOffset;
		NDI_DELETE[] m_postJointIkOffset;
		NDI_DELETE[] m_postJointLocWs;
		NDI_DELETE[] m_uniqueJointNames;
		NDI_DELETE[] m_finalDeltaAngles;
	}

	void DebugDrawJointLocWs(float length, Color xColor, Color yColor, Color zColor, bool needText) const;

	int				m_numUniqueJoints;
	int				m_numJoints;
	int				m_maxIterations;
	int				m_numIterationsUsed;
	float			m_errSqr;
	TimeFrame		m_time;

	Locator GetPreJointIkOffset(int index) const { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); return m_preJointIkOffset[index]; }
	void SetPreJointIkOffset(int index, const Locator& loc) { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); m_preJointIkOffset[index] = loc; }
	Locator GetPostJointIkOffset(int index) const { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); return m_postJointIkOffset[index]; }
	void SetPostJointIkOffset(int index, const Locator& loc) { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); m_postJointIkOffset[index] = loc; }
	Locator GetPostJointLocWs(int index) const { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); return m_postJointLocWs[index]; }
	void SetPostJointLocWs(int index, const Locator& loc) { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); m_postJointLocWs[index] = loc; }
	StringId64 GetUniqueJointName(int index) const { ANIM_ASSERT(index >= 0 && index < m_numUniqueJoints); return m_uniqueJointNames[index]; }

	float GetFinalDeltaAngle(int index) const { ANIM_ASSERT(index >= 0 && index < m_numJoints); return m_finalDeltaAngles[index]; }
	void AddFinalDeltaAngle(int index, float delta) { ANIM_ASSERT(index >= 0 && index < m_numJoints); m_finalDeltaAngles[index] += delta; }

private:
	Locator*		m_preJointIkOffset;
	Locator*		m_postJointIkOffset;
	Locator*		m_postJointLocWs;
	StringId64*		m_uniqueJointNames;
	float*			m_finalDeltaAngles;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JacobianIkResult SolveJacobianIK(JacobianIkInstance* ik, JacobianDebugInfo* debugInfo = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianEndEffectorEntry
{
	StringId64		 endEffectorNameId;
	short			 endEffectorOffset;
	IkGoal::GoalType endEffectorGoalType;
	Locator			 endEffectJointOffset;
	bool			 endEffectEnabled;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JacobianMap
{
public:
	enum Axis
	{
		kAxisX,
		kAxisY,
		kAxisZ,
	};
	static const char* AxisToString(Axis type);

	enum DofType
	{
		kJointTypeRot,
		kJointTypeTrans,
	};
	static const char* DofTypeToString(DofType type);

	struct JacobianJointEntry
	{
		StringId64		m_jointNameId;
		short			m_jointOffset;
		Axis			m_axis;
		DofType			m_dofType;
		float			m_ikFactor;
		U32				m_endEffectorAncestor;
	};

	class EndEffectorDef
	{
	public:
		EndEffectorDef()
			: m_jointId(INVALID_STRING_ID_64)
			, m_goalType(IkGoal::kGoalNone)
			, m_jointOffset(kIdentity)
			, m_enabled(false)
		{}

		EndEffectorDef(StringId64 jointId, IkGoal::GoalType goalType)
			: m_jointId(jointId)
			, m_goalType(goalType)
			, m_jointOffset(kIdentity)
			, m_enabled(true)
		{}

		EndEffectorDef(StringId64 jointId, IkGoal::GoalType goalType, const Locator& loc)
			: m_jointId(jointId)
			, m_goalType(goalType)
			, m_jointOffset(loc)
			, m_enabled(true)
		{}

		EndEffectorDef(StringId64 jointId, IkGoal::GoalType goalType, const Locator& loc, bool enabled)
			: m_jointId(jointId)
			, m_goalType(goalType)
			, m_jointOffset(loc)
			, m_enabled(enabled)
		{}

		StringId64			m_jointId;
		IkGoal::GoalType	m_goalType;
		Locator				m_jointOffset;
		bool				m_enabled;
	};

	struct UniqueJoint
	{
		int					m_jointOffset;
		StringId64			m_jointName;
		Locator				m_ikOffset;
	};

	JointLimits::JointSetMap	m_jointSetMap;

	JacobianJointEntry*			m_jointEntries;
	int							m_numJoints;

	JacobianEndEffectorEntry*	m_endEffectorEntries;
	int							m_numEndEffectors;

	int							m_rootJointOffset;
	UniqueJoint*				m_uniqueJoints;
	int							m_numUniqueJoints;

	float						m_rotationGoalFactor;
	bool						m_valid;

	JacobianMap();
	~JacobianMap();

	void Init(const JointSet* pJoints,
			  StringId64 rootId,
			  int numEndEffectors,
			  const EndEffectorDef* pEndEffectorDefs); // Allocates memory
	void InitWithConstraints(JointSet* pJoints,
							 StringId64 rootId,
							 int numEndEffectors,
							 const EndEffectorDef* pEndEffectorDefs,
							 StringId64 constraintInfoId); // Allocates memory
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);
	void Destroy();

	void SetEndEffectorEnabled(StringId64 endEffectorNameId, IkGoal::GoalType goalType, bool value);
	void SetEndEffectorOffset(StringId64 endEffectorNameId, const Locator& offset);
	void ResetJointIkOffsets();

	void CreateJacobianMatrix(const IkGoal* goals, const JointSet* pJointChain, ScalarMatrix& outMatrix) const;

	int GetNumEnabledEndEffectors() const;

	void GetJacobianSize(int& outRows, int& outCols) const;
	void CreateJacobianMatrix(const IkGoal* goals, const JointSet* pJointChain, EMatrix& outJacobian) const;

	void DumpToTTY();

private:
	void InitInternal(const JointSet* pJoints,
					  const DC::IkConstraintInfo* pConstraints,
					  StringId64 rootId,
					  int numEndEffectors,
					  const EndEffectorDef* pEndEffectorDefs);
};

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJacobianJointDeltaWs(const JacobianDebugInfo& currInfos, const JacobianDebugInfo* prevInfo);
void DebugDrawJacobianJointDeltaAngles(const JacobianMap& jacobian,
									   const JacobianDebugInfo** debugInfos,
									   const I32 numInfos,
									   float xOrigin,
									   float yOrigin,
									   float xSize,
									   float ySize,
									   float fScale);
void DebugDrawJacobianIterationsTime(const JacobianDebugInfo** debugInfos,
									 const I32 numInfos,
									 float xOrigin,
									 float yOrigin,
									 float xSize,
									 float ySize,
									 float fScale);

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianSolverContext
{
	JacobianIkInstance* m_pIk;
	float* m_paDeltaAngles;
	float* m_paDeltaAngleSum;
	Locator* m_paStartLocs;
	JacobianDebugInfo* m_pDebugInfo;
	float m_maxDeltaAngleRad;
	float m_sqrErr;

	JacobianSolverContext()
		: m_pIk(nullptr)
		, m_paDeltaAngles(nullptr)
		, m_paDeltaAngleSum(nullptr)
		, m_paStartLocs(nullptr)
		, m_pDebugInfo(nullptr)
		, m_maxDeltaAngleRad(0.0f)
		, m_sqrErr(0.0f)
	{}
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool IterateSolver(JacobianSolverContext& context, JacobianIkResult& result);
JacobianSolverContext BeginSolve(JacobianIkInstance* ik, JacobianDebugInfo* debugInfo);
void FinishSolve(JacobianSolverContext& context);
