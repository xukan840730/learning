/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/frame-params.h"
#include <orbisanim/animhierarchy.h>

enum class RigNodeType
{
#define MAKE_MULTI_RIG_NODE(multi, base) multi = base + kRigNodeMultiBase
										// Equal to the runtime version; CommandBlock::kCmdCustomBase
	kMeasureCone,						// 0x80: MeasureCone
	kMeasureTwist,						// 0x81: MeasureTwist
	kInterpolateRbf,					// 0x82: InterpolateRbf
	kMatrixToFloats,					// 0x83: MatrixToFloats
	kRivetPlane,						// 0x84: RivetPlane
	kPointPoser,						// 0x85: PointPoser
	kFatConstraint,						// 0x86: FatConstraint
	kMultMatrixToPos,					// 0x87: MultMatrixToPos
	kMatrixToEuler,						// 0x88: MatrixToEuler
	kSetRange,							// 0x89: SetRange
	kAddDoubleLinear,					// 0x8A: AddDoubleLinear
	kMultDoubleLinear,					// 0x8B: MultDoubleLinear
	kMatrixToExpMap,					// 0x8C: kMatrixToExpMap
	kAngleDriver,						// 0x8D: kAngleDriver
	kBendTwistDriver,					// 0x8E: kBendTwistDriver,		
	kDecomposeMatrix,					// 0x8F: kDecomposeMatrix,		
	kInterpolateMatrix1D,				// 0x90: kInterpolateMatrix1D,	
	kInterpolateMatrixArray,			// 0x91: kInterpolateMatrixArray,
	kMultMatrix,						// 0x92: kMultMatrix,			
	kNormalizeRange,					// 0x93: kNormalizeRange,		
	kPairBlend,							// 0x94: kPairBlend,				
	kWachspress,						// 0x95: kWachspress,			
	kInterpolateMatrixArray16,			// 0x96: kInterpolateMatrixArray16,			
	kNumCustomRigNodes
};

const char* GetRigNodeName(RigNodeType type);

/// --------------------------------------------------------------------------------------------------------------- ///

namespace OrbisAnim
{
	struct HierarchyHeader;

	namespace CommandBlock
	{
	
struct AngleDriverParams
{
	U32	m_mayaNodeTypeId;

	U32 m_rollAxis;
	U32 m_yawAxis;
	U32 m_rotateOrder;
	float m_jointOrient[3];

	Location m_inputSqtLoc;
	Location m_outputYawLoc;
	Location m_outputPitchLoc;
	Location m_outputRollLoc;
};

struct BendTwistDriverParams
{
	U32	m_mayaNodeTypeId;

	U32 m_driverMode;
	U32 m_rotateOrder;
	float m_weight;
	float m_aimVector[3];
	float m_jointOrient[3];

	Location m_inputSqtLoc;
	Location m_outputBendXLoc;
	Location m_outputBendYLoc;
	Location m_outputBendZLoc;
	Location m_outputTwistXLoc;
	Location m_outputTwistYLoc;
	Location m_outputTwistZLoc;
};

struct DecomposeMatrixParams
{
	U32	m_mayaNodeTypeId;

	U32 m_rotateOrder;
	float m_jointOrient[3];

	Location m_inputMatLoc;
	Location m_outputTransLoc[3];
	Location m_outputRotLoc[3];
	Location m_outputScaleLoc[3];
};

struct FatConstraintParams
{
	U32 m_mayaNodeTypeId;

	Location m_inMatrixLoc;
	Location m_spaceMatrixLoc;
	Location m_limitMatrixLoc;
	Location m_relativeMatrixLoc;

	Location m_rotateWeightLoc;
	Location m_scaleWeightLoc;
	Location m_translateWeightLoc;

	F32 m_offsetMatrix[16];

	F32 m_translateWeightDefault;
	F32 m_rotateWeightDefault;
	F32 m_scaleWeightDefault;

	F32 m_mass;
	F32 m_stiffness;
	F32 m_damping;

	F32 m_limitPositive[3];
	F32 m_limitNegative[3];
	F32 m_limitSmoothPercent;

	enum Flags
	{
		kUseLimit = 1 << 0,
		kInMatrixIsIntermediate4x4 = 1 << 1,
		kSpaceMatrixIsIntermediate4x4 = 1 << 2,
		kLimitMatrixIsIntermediate4x4 = 1 << 3,
		kRelativeMatrixIsIntermediate4x4 = 1 << 4,
	};
	U32 m_flags;

	Location m_outputLocs[9];

	U32 m_persistentDataOffset;

	StringId64 m_nodeNameId;
};

struct InterpolateMatrix1DState
{
	float m_time;
	float m_values[16];
};

struct InterpolateMatrix1DParams
{
	U32	m_mayaNodeTypeId;
	U32 m_numEntries;
	U32 m_pad[2];			// align to 16

	struct Entry
	{
		U8 m_inputAsAngle;
		U8 m_interpMode;
		U8 m_postInfinityMode;
		U8 m_preInfinityMode;

		U32 m_numStates;
		U32 m_statesOffset;			// Offset from start of this struct to where the State structs are
		Location m_inputLoc;
		Location m_outputLoc;
		U32 m_pad[3];
	};
};


struct InterpolateMatrixArrayParams
{
	U32	m_mayaNodeTypeId;
	U32 m_numEntries;
	U16	m_version;
	U16	m_pad[3];

	struct Entry
	{
		enum EFlags : U16 { kConstMatrices = 1 };
		U16 m_numMatrices;
		EFlags m_flags;
		U16 m_inputArrayOffset;			// Offset from start of InterpolateMatrixArrayParams to where the input locations are
		U16 m_weightArrayOffset;		// Offset from start of InterpolateMatrixArrayParams to where the input locations are
		U32 m_matrixArrayOffset;		// Offset from start of InterpolateMatrixArrayParams to where the input locations are
		Location m_outputMatrixLoc;

		struct PositionsAndQuats			// element type of inputArray for const Matrices
		{
			Point m_pos[4];
			Quat m_quats[4];
		};
	};
};


struct InterpolateMatrixArray16Params
{
	enum {kMaxData = 32};
	U32	m_mayaNodeTypeId;
	U32 m_numEntries;
	U32	m_pad[2];

	Location m_outputLocs[kMaxData];

	struct Entry
	{
		Location m_weightLocs[16 + 4];	// 16 weights + 4 normalization weights
		struct QuatAndPos				// element type of inputArray for const Matrices
		{
			Quat m_quat;
			Point m_pos;
		} m_quatsAndPositions[16];
	};
};

struct InterpolateRbfParams
{
	U32		m_mayaNodeTypeId;
	U32		m_numInputs;
	U32		m_numOutputs;
	U32		m_numPoses;

	float	m_epsilon;
	U16		m_inputLocsOffset;			// Offset from start of this struct to where the input locations are
	U16		m_outputLocsOffset;			// Offset from start of this struct to where the output locations are
	U16		m_inputPosesOffset;			// Offset from start of this struct to where the input poses reside (numInputs * numPoses)
	U16		m_weightsOffset;			// Offset from start of this struct to where the weights reside (numOutputs * numPoses)
	U16		m_padding[2];

	StringId64		m_nodeNameId;
	U8				m_padding2[8];
};


struct InputControlDriverParams
{
	SMath::Quat		m_refPosePs;
	SMath::Quat		m_twistRefPosePs;
	U32				m_mayaNodeTypeId;
	U32				m_inputQuatLoc;
	U32				m_primaryAxis;

	U32				m_twistAxis;
	Location		m_inputTwistAngleLoc;
	F32				m_minAngleDeg;
	F32				m_maxAngleDeg;
	Location		m_outputLoc;
};

struct MatrixToExpMatParams
{
	U32			m_mayaNodeTypeId;
	U16			m_useMatrix;
	U16			m_rotateOrder;
	Location	m_inputQuatLoc;
	Location	m_inputLocs[3];
	Location	m_outputLocs[3];
};

struct MultMatrixParams
{
	U32	m_mayaNodeTypeId;

	U32 m_numInputs;
	U32 m_inputArrayOffset;			// Offset from start of this struct to where the input poses reside (numInputs * numPoses)
	Location m_outputMatrixLoc;
};

struct MultMatrixToPosParams
{
	U32 m_mayaNodeTypeId;
	Location m_inputLocs[2];
	Location m_outputLocs[3];
};

struct NormalizeRangeParams
{
	U32	m_mayaNodeTypeId;

	float m_inputMin;
	float m_inputMax;
	float m_outputMin;
	float m_outputMax;

	U8 m_interpMode;
	U8 m_interpAsAngle;
	Location m_inputLoc;
	Location m_outputLoc;
};

struct PairBlendParams
{
	U32	m_mayaNodeTypeId;

	float m_translate1[3];
	float m_translate2[3];
	float m_rotateDeg1[3];
	float m_rotateDeg2[3];

	float m_weight;

	enum Flags
	{
		kInTranslate1Connected		= 1 << 0,
		kInTranslate2Connected		= 1 << 1,
		kInRotate1Connected			= 1 << 2,
		kInRotate2Connected			= 1 << 3,
		kInWeightConnected			= 1 << 4,
		kOutTranslateConnected		= 1 << 5,
		kOutRotateConnected			= 1 << 6,
	};
	U32 m_flags;

	Location m_locs[19];
};

struct PointPoserParams
{
	U32				m_mayaNodeTypeId;
	U32				m_numPoses;

	U32				m_numPointsPerPose;
	U16				m_inputArrayLocOffset;		// Offset from start of this struct to where the input locations are
	U16				m_outputArrayLocOffset;		// Offset from start of this struct to where the input locations are

	U16				m_pointPosesLocOffset;		// Offset from start of this struct to where the input locations are
	U16				m_padding[3];

	StringId64		m_nodeNameId;
};

struct RivetPlaneParams
{
	U32				m_mayaNodeTypeId;
	U32				m_numOutputs;
	U32				m_mode;
	U32				m_inputPointsAreDriven;

	F32				m_pointU;
	F32				m_pointV;
	F32				m_scaleMultiplierU;
	F32				m_scaleMultiplierV;

	F32				m_bulgeShrinkU;							// If the 'U' axis shrunk we use this scale factor to figure out how much to translate along the plane normal as (shrinkFactor * bulgeWeight)
	F32				m_bulgeShrinkV;							// If the 'V' axis shrunk we use this scale factor to figure out how much to translate along the plane normal as (shrinkFactor * bulgeWeight)
	F32				m_bulgeStretchU;						// If the 'U' axis grew we use this scale factor to figure out how much to translate along the plane normal as (growFactor * bulgeWeight)
	F32				m_bulgeStretchV;						// If the 'V' axis grew we use this scale factor to figure out how much to translate along the plane normal as (growFactor * bulgeWeight)

	F32				m_normalOffset;							// The base offset of the point along the plane normal
	U32				m_numSkinningJoints;

	F32				m_jointOrient[3];

	U16				m_inputArrayLocOffset;			// Offset from start of this struct to where the input locations are
	U16				m_outputArrayLocOffset;			// Offset from start of this struct to where the X are
	U16				m_planePointArrayLocOffset;		// Offset from start of this struct to where the X are
	U16				m_bindPoseMatrixArrayLocOffset;	// Offset from start of this struct to where the X are
	U16				m_skinningWeightArrayLocOffset;	// Offset from start of this struct to where the X are
	U16				m_inputScaleFactorsLocOffset;	// Offset from start of this struct to where the X are

	StringId64		m_nodeNameId;
};

struct RivetOutputInfo
{
	U32 m_type;
	Location m_loc;
};


struct WachspressParams
{
	U32	m_mayaNodeTypeId;
	U32 m_numEntries;
	U32 m_pad[2];			// align to 16

	struct Entry
	{
		float m_pointValues[4][3];	// 48
		float m_inputDefaults[3];	// 12
		Location m_inputLoc[3];	    // 12
		Location m_outputLoc[4];	// 16
		U32 m_pad[2];				//  8
									//@96
	};
};

extern void ExecuteCommandAngleDriverImpl(const HierarchyHeader* pHierarchyHeader, const AngleDriverParams* pParams);
extern void ExecuteCommandBendTwistDriverImpl(const HierarchyHeader* pHierarchyHeader,
											  const BendTwistDriverParams* pParams);
extern void ExecuteCommandDecomposeMatrixImpl(const HierarchyHeader* pHierarchyHeader,
											  const DecomposeMatrixParams* pParams);
extern void ExecuteCommandFatConstraintImpl(const HierarchyHeader* pHierarchyHeader,
											const FatConstraintParams* pParams,
											const OrbisAnim::SegmentContext* pContext);
extern void ExecuteCommandInterpolateMatrix1DImpl(const HierarchyHeader* pHierarchyHeader,
												  const InterpolateMatrix1DParams* pParams,
												  const InterpolateMatrix1DParams::Entry* pEntries,
												  const InterpolateMatrix1DState* pStateData);
extern void ExecuteCommandInterpolateMatrixArrayImpl(const HierarchyHeader* pHierarchyHeader,
													 const InterpolateMatrixArrayParams* pParams,
													 const InterpolateMatrixArrayParams::Entry* pEntries,
													 const Transform* pDefaultMatricesTable,
													 const float* pDefaultWeightsTable,
													 const Location* pInputLocsTable);
extern void ExecuteCommandInterpolateRbfImpl(const HierarchyHeader* pHierarchyHeader,
											 const InterpolateRbfParams* pParams,
											 const Location* pInputLocs,
											 const Location* pOutputLocs,
											 const float* pInputPoses,
											 const float* pWeights);
extern void ExecuteCommandMatrixToExpMapImpl(const HierarchyHeader* pHierarchyHeader,
											 const MatrixToExpMatParams* pParams);
extern void ExecuteCommandMeasureConeImpl(const HierarchyHeader* pHierarchyHeader,
										  const InputControlDriverParams* pParams);
extern void ExecuteCommandMeasureTwistImpl(const HierarchyHeader* pHierarchyHeader,
										   const InputControlDriverParams* pParams);
extern void ExecuteCommandMultMatrixImpl(const HierarchyHeader* pHierarchyHeader,
										 const MultMatrixParams* pParams,
										 const float* pDefaultValues,
										 const Location* pInputLocs,
										 const U8* pInvertMat,
										 const U8* pDataFormat);
extern void ExecuteCommandMultMatrixToPosImpl(const HierarchyHeader* pHierarchyHeader,
											  const MultMatrixToPosParams* pParams);
extern void ExecuteCommandNormalizeRangeImpl(const HierarchyHeader* pHierarchyHeader,
											 const NormalizeRangeParams* pParams);
extern void ExecuteCommandPairBlendImpl(const HierarchyHeader* pHierarchyHeader, const PairBlendParams* pParams);
extern void ExecuteCommandPointPoserImpl(const HierarchyHeader* pHierarchyHeader, const PointPoserParams* pParams);
extern void ExecuteCommandRivetPlaneImpl(const HierarchyHeader* pHierarchyHeader, const RivetPlaneParams* pParams);
extern void ExecuteCommandWachspressImpl(const HierarchyHeader* pHierarchyHeader,
										 const WachspressParams* pParams,
										 const WachspressParams::Entry* pEntries);

	}	//namespace CommandBlock
}	//namespace OrbisAnim

/// --------------------------------------------------------------------------------------------------------------- ///

struct RigNodeStats
{
	void Reset();
	void Print();

	float m_elapsedSec[(U32)RigNodeType::kNumCustomRigNodes];
	U32 m_numExecutions[(U32)RigNodeType::kNumCustomRigNodes];
	float m_totalElapsedSec;
};


U64 RigNodeGetTick();
void RegisterRigNodeStats(RigNodeType customRigNodeType, U64 startTick, U64 endTick);
void RegisterTotalRigNodeStats(U64 startTick, U64 endTick);

#define RIG_NODE_TIMER_START()	U64 startTick = RigNodeGetTick()
#define RIG_NODE_TIMER_END(x)	U64 endTick = RigNodeGetTick(); RegisterRigNodeStats(x, startTick, endTick)

extern RigNodeStats g_rigNodeStats[kMaxNumFramesInFlight];
extern bool g_printRigNodeOutputs;
