/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>

#include "ndlib/anim/rig-nodes/rig-nodes.h"

RigNodeStats g_rigNodeStats[kMaxNumFramesInFlight];
bool g_printRigNodeOutputs = false;

/// --------------------------------------------------------------------------------------------------------------- ///
void RigNodeStats::Reset()
{
	for (int i = 0; i < (int)RigNodeType::kNumCustomRigNodes; ++i)
	{
		m_elapsedSec[i] = 0.0f;
		m_numExecutions[i] = 0;
	}
	m_totalElapsedSec = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void RigNodeStats::Print()
{
	float customTimeSec = 0.0f;
	float slowestTimeSec = 0.0f;
	for (int i = 0; i < (int)RigNodeType::kNumCustomRigNodes; ++i)
	{
		customTimeSec += m_elapsedSec[i];
		slowestTimeSec = Max(slowestTimeSec, m_elapsedSec[i]);
	}

	for (int i = 0; i < (int)RigNodeType::kNumCustomRigNodes; ++i)
	{
		const float customPcnt = (slowestTimeSec > NDI_FLT_EPSILON) ? Limit01(m_elapsedSec[i] / slowestTimeSec) : 0.0f;
		const Color timeColor = Slerp(kColorGreen, kColorRed, customPcnt);

		char colorBuf[128];

		MsgCon("%25s: %s%.3f ms%s [%u instances]\n",
			   GetRigNodeName((RigNodeType)i),
			   GetTextColorString(timeColor, colorBuf),
			   m_elapsedSec[i] * 1000.0f,
			   GetTextColorString(kTextColorNormal),
			   m_numExecutions[i]);
	}
	MsgCon("\n");

	MsgCon("Total Evaluation Time: %.3f ms (%.3f ms spent in custom nodes)\n",
		   m_totalElapsedSec * 1000.0f,
		   customTimeSec * 1000.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetRigNodeName(RigNodeType type)
{
	switch (type)
	{
	case RigNodeType::kMeasureCone: return "Measure Cone";
	case RigNodeType::kMeasureTwist: return "Measure Twist";
	case RigNodeType::kInterpolateRbf: return "Interpolate Rbf";
	case RigNodeType::kMatrixToFloats: return "Matrix To Floats";
	case RigNodeType::kRivetPlane: return "Rivet Plane";
	case RigNodeType::kPointPoser: return "Point Poser";
	case RigNodeType::kFatConstraint: return "Fat Constraint";
	case RigNodeType::kMultMatrixToPos: return "Mult Matrix To Pos";
	case RigNodeType::kMatrixToEuler: return "Matrix To Euler";
	case RigNodeType::kSetRange: return "Set Range";
	case RigNodeType::kAddDoubleLinear: return "Add Double Linear";
	case RigNodeType::kMultDoubleLinear: return "Mult Double Linear";
	case RigNodeType::kMatrixToExpMap: return "Matrix To Exp Map";
	case RigNodeType::kAngleDriver: return "Angle Driver";
	case RigNodeType::kBendTwistDriver: return "Bend Twist Driver";
	case RigNodeType::kDecomposeMatrix: return "Decompose Matrix";
	case RigNodeType::kInterpolateMatrix1D: return "Interpolate Matrix 1D";
	case RigNodeType::kInterpolateMatrixArray: return "Interpolate Matrix Array";
	case RigNodeType::kMultMatrix: return "Mult Matrix";
	case RigNodeType::kNormalizeRange: return "Normalize Range";
	case RigNodeType::kPairBlend: return "Pair Blend";
	case RigNodeType::kWachspress: return "Wachspress";
	case RigNodeType::kInterpolateMatrixArray16: return "Interp. Matrix Array 16";
	}

	return "Unknown";
}


/// --------------------------------------------------------------------------------------------------------------- ///
U64 RigNodeGetTick()
{
#ifndef FINAL_BUILD
	return TimerGetRawCount();
#else
	return 0;
#endif
}


/// --------------------------------------------------------------------------------------------------------------- ///
void RegisterRigNodeStats(RigNodeType customRigNodeType, U64 startTick, U64 endTick)
{
#ifndef FINAL_BUILD
	const RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
	const I64 frameIndex = pRenderFrameParams ? (pRenderFrameParams->m_frameNumber + 1) % kMaxNumFramesInFlight : 0;
	g_rigNodeStats[frameIndex].m_elapsedSec[(U32)customRigNodeType] += ConvertTicksToSeconds(startTick, endTick);
	g_rigNodeStats[frameIndex].m_numExecutions[(U32)customRigNodeType]++;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void RegisterTotalRigNodeStats(U64 startTick, U64 endTick)
{
#ifndef FINAL_BUILD
	const RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
	const I64 frameIndex = pRenderFrameParams ? (pRenderFrameParams->m_frameNumber + 1) % kMaxNumFramesInFlight : 0;
	g_rigNodeStats[frameIndex].m_totalElapsedSec += ConvertTicksToSeconds(startTick, endTick);
#endif
}

// void RegisterCustomRigNode(U64 customNodeIndex, DispatcherFunction func)
// {
// 	ANIM_ASSERT(g_pfnCustomCommand[customNodeIndex] == nullptr);
// 	ANIM_ASSERT(customNodeIndex < kNumCustomRigNodes);
// 	g_pfnCustomCommand[customNodeIndex] = func;
// }


namespace OrbisAnim
{
	namespace CommandBlock
	{
		extern void ExecuteCommandMeasureCone(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMeasureTwist(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandInterpolateRbf(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMatrixToFloats(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandRivetPlane(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandPointPoser(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandFatConstraint(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMultMatrixToPos(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMatrixToEuler(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandNormalizeRange(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandAddFloat(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMultFloat(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMatrixToExpMap(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandAngleDriver(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandBendTwistDriver(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandDecomposeMatrix(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandInterpolateMatrix1D(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandInterpolateMatrixArray(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandMultMatrix(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandNormalizeRange(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandPairBlend(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandWachspress(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);
		extern void ExecuteCommandInterpolateMatrixArray16(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/);

		DispatcherFunction g_pfnCustomCommand[] = {
			ExecuteCommandMeasureCone,			  // 0x80: OrbisAnim::CommandBlock::kCmdCustomBase + 0
			ExecuteCommandMeasureTwist,			  // 0x81: OrbisAnim::CommandBlock::kCmdCustomBase + 1
			ExecuteCommandInterpolateRbf,		  // 0x82: OrbisAnim::CommandBlock::kCmdCustomBase + 2
			ExecuteCommandMatrixToFloats,		  // 0x83: OrbisAnim::CommandBlock::kCmdCustomBase + 3
			ExecuteCommandRivetPlane,			  // 0x84: OrbisAnim::CommandBlock::kCmdCustomBase + 4
			ExecuteCommandPointPoser,			  // 0x85: OrbisAnim::CommandBlock::kCmdCustomBase + 5
			ExecuteCommandFatConstraint,		  // 0x86: OrbisAnim::CommandBlock::kCmdCustomBase + 6
			ExecuteCommandMultMatrixToPos,		  // 0x87: OrbisAnim::CommandBlock::kCmdCustomBase + 7
			ExecuteCommandMatrixToEuler,		  // 0x88: OrbisAnim::CommandBlock::kCmdCustomBase + 8
			ExecuteCommandNormalizeRange,		  // 0x89: OrbisAnim::CommandBlock::kCmdCustomBase + 9
			ExecuteCommandAddFloat,				  // 0x8A: OrbisAnim::CommandBlock::kCmdCustomBase + 10
			ExecuteCommandMultFloat,			  // 0x8B: OrbisAnim::CommandBlock::kCmdCustomBase + 11
			ExecuteCommandMatrixToExpMap,		  // 0x8C: OrbisAnim::CommandBlock::kCmdCustomBase + 12
			ExecuteCommandAngleDriver,			  // 0x8D: OrbisAnim::CommandBlock::kCmdCustomBase + 13
			ExecuteCommandBendTwistDriver,		  // 0x8E: OrbisAnim::CommandBlock::kCmdCustomBase + 14
			ExecuteCommandDecomposeMatrix,		  // 0x8F: OrbisAnim::CommandBlock::kCmdCustomBase + 15
			ExecuteCommandInterpolateMatrix1D,	  // 0x90: OrbisAnim::CommandBlock::kCmdCustomBase + 16
			ExecuteCommandInterpolateMatrixArray, // 0x91: OrbisAnim::CommandBlock::kCmdCustomBase + 17
			ExecuteCommandMultMatrix,			  // 0x92: OrbisAnim::CommandBlock::kCmdCustomBase + 18
			ExecuteCommandNormalizeRange,		  // 0x93: OrbisAnim::CommandBlock::kCmdCustomBase + 19
			ExecuteCommandPairBlend,			  // 0x94: OrbisAnim::CommandBlock::kCmdCustomBase + 20
			ExecuteCommandWachspress,			  // 0x95: OrbisAnim::CommandBlock::kCmdCustomBase + 21
			ExecuteCommandInterpolateMatrixArray16 // 0x96 OrbisAnim::CommandBlock::kCmdCustomBase + 22
		};
		size_t g_numCustomCommands = sizeof(CommandBlock::g_pfnCustomCommand) / sizeof(CommandBlock::g_pfnCustomCommand[0]);

	}	//namespace CommandBlock
}	//namespace OrbisAnim
