/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>

#include "corelib/util/timer.h"
#include "corelib/util/float16.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"


static float GaussKernel(float d, float e) 
{ 
	return exp(-(d*d)/e);
}

static float Gauss2Kernel(float d, float e) 
{ 
	float f = (d/e);
	return exp(-(f*f));
}

static float LinearKernel(float d, float e) 
{ 
	return d;
}

static float HardyKernel(float d, float e) 
{ 
	return sqrt(d*d + e*e);
}

static float MultiQuadraticKernel(float d, float e)
{
	return sqrt(1.0f + d*d);
}

static float InverseMultiQuadraticKernel(float d, float e)
{
	return 1.0f / (1.0f + d*d);
}

static float InverseNormKernel(float d, float e)
{
	return 1.0f / sqrt(1.0f + d*d);
}

static float CubicKernel(float d, float e)
{
	return d*d*d + 1.0f;
}

static float ThinPlateKernel(float d, float e)
{
	if (d < e) {
		return e;
	} else {
		return d*d*log(d);
	}
}


// definition of RBF Kernel with an epsilon
typedef float (*RBFKernel)(float d, float e);

static const __m128i s_remainderMasks[4] = {
	_mm_setr_epi32(0, 0, 0, 0),
	_mm_setr_epi32(-1, 0, 0, 0),
	_mm_setr_epi32(-1, -1, 0, 0),
	_mm_setr_epi32(-1, -1, -1, 0)
};


namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateRbfImpl(
			const HierarchyHeader* pHierarchyHeader,
			const InterpolateRbfParams* pParams,
			const Location* pInputLocs,
			const Location* pOutputLocs,
			const float* pInputPoses,
			const float* pWeights)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
			const I64 numInputs = pParams->m_numInputs;
			const I64 numOutputs = pParams->m_numOutputs;
			const I64 numPoses = pParams->m_numPoses;
			const float epsilon = pParams->m_epsilon;

			RBFKernel kernelFunc = LinearKernel;
//			RBFKernel kernelFunc = MultiQuadraticKernel;

			// 1. Read in the current inputs
			float* pInputs = STACK_ALLOC(float, numInputs);
			for (I64 i = 0; i < numInputs; ++i)
			{
				const float* pInputPtr = HierarchyFloat(pInputLocs[i], pHierarchyHeader);
				pInputs[i] = *pInputPtr;
			}

			// 2. Calculate the distance between each one of the input poses and our current inputs
			float* pRawDistances = STACK_ALLOC(float, numPoses);
			for (I64 i = 0; i < numPoses; ++i)
			{
#if 1
				VF32 distanceVec = Simd::GetVecAllZero();
				const U32 numInputs4 = numInputs / 4;
				const U32 remainder = numInputs - numInputs4*4;
				VF32 remainderMask = Simd::VecFromMask(s_remainderMasks[remainder]);

				for (U64 j4 = 0; j4 < numInputs4*4; j4 += 4)
				{
					VF32 inputs = Simd::LoadVec(pInputs + j4);
					VF32 inputPoses = Simd::ULoadVec(pInputPoses + i*numInputs + j4);
					VF32 diff = Simd::Sub(inputs, inputPoses);
					VF32 diffSqr;
					SMATH_VEC_DOT4(diffSqr, diff, diff);
					distanceVec = _mm_add_ss(distanceVec, diffSqr);
				}

				if (remainder > 0)
				{
					U64 j4 = numInputs4*4;
					VF32 inputs = Simd::LoadVec(pInputs + j4);
					VF32 inputPoses = Simd::ULoadVec(pInputPoses + i*numInputs + j4);
					VF32 diff = Simd::Sub(inputs, inputPoses);
					diff = _mm_and_ps(diff, remainderMask);
					VF32 diffSqr;
					SMATH_VEC_DOT3(diffSqr, diff, diff);
					distanceVec = _mm_add_ss(distanceVec, diffSqr);
				}

				// Do the safe sqrt
				distanceVec = Simd::Select(_mm_sqrt_ss(distanceVec), Simd::GetVecAllZero(),
					Simd::CompareLE(distanceVec, Simd::GetVecAllZero()));

				_mm_store_ss(pRawDistances + i, distanceVec);
#else
				float distance = 0.0f;
				for (I64 j= 0; j < numInputs; ++j)
				{
					const float diff = pInputs[j] - pInputPoses[i * numInputs + j];
					distance += diff * diff;
				}
				pRawDistances[i] = SafeSqrt(distance);
#endif
			}

			// 3. Transform all distances through a Gaussian kernel
			float* pTransformedDistances = STACK_ALLOC(float, numPoses);
			for (I64 i = 0; i < numPoses; ++i)
			{
				pTransformedDistances[i] = kernelFunc(pRawDistances[i], epsilon);
			}

			// 4. Multiply the generated 'distance input matrix' with our 'weights' matrix to generate our '1 x m_numOutputs' output matrix
			float* pOutputs = STACK_ALLOC(float, numOutputs);
			for (I64 iOutput = 0; iOutput < numOutputs; ++iOutput)
			{
				float result = 0.0f;

				// SSE vectorized version
#if 1
				const U32 numPoses4 = numPoses / 4;
				const U32 remainder = numPoses - numPoses4*4;
				VF32 remainderMask = Simd::VecFromMask(s_remainderMasks[remainder]);
				VF32 resultVec = Simd::GetVecAllZero();

				for (I64 iPose4 = 0; iPose4 < numPoses4*4; iPose4 += 4)
				{
					VF32 distances = Simd::LoadVec(pTransformedDistances + iPose4);
					VF32 weights = Simd::MakeVec(
						pWeights[numOutputs*(iPose4 + 0) + iOutput],
						pWeights[numOutputs*(iPose4 + 1) + iOutput],
						pWeights[numOutputs*(iPose4 + 2) + iOutput],
						pWeights[numOutputs*(iPose4 + 3) + iOutput]);

					VF32 res;
					SMATH_VEC_DOT4(res, distances, weights);
					resultVec = _mm_add_ss(resultVec, res);
				}

				if (remainder > 0)
				{
					VF32 distances = Simd::LoadVec(pTransformedDistances + numPoses4*4);
					VF32 weights = Simd::MakeVec(
						pWeights[numOutputs*(numPoses4*4 + 0) + iOutput],
						pWeights[numOutputs*(numPoses4*4 + 1) + iOutput],
						pWeights[numOutputs*(numPoses4*4 + 2) + iOutput],
						0.f);

					distances = _mm_and_ps(distances, remainderMask);
					weights = _mm_and_ps(weights, remainderMask);
					VF32 res;
					SMATH_VEC_DOT3(res, distances, weights);
					resultVec = _mm_add_ss(resultVec, res);
				}

				_mm_store_ss(&result, resultVec);
#else
				for (I64 iPose = 0; iPose < numPoses; ++iPose)
				{
					result += pTransformedDistances[iPose] * MakeF32(pWeights[numOutputs * iPose + iOutput]);
				}
#endif

				pOutputs[iOutput] = result;
			}

			// 5. Write out the output values
			for (I64 i = 0; i < numOutputs; ++i)
			{
				float* pOutputPtr = HierarchyFloat(pOutputLocs[i], pHierarchyHeader);

				*pOutputPtr = pOutputs[i];
			}

			RIG_NODE_TIMER_END(RigNodeType::kInterpolateRbf);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("InterpolateRbf (%s)\n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				for (I64 i = 0; i < numOutputs; ++i)
					MsgAnim("   out: %.4f\n", pOutputs[i]);
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateRbf(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			//			const U16 numControlDrivers						= param_qw0[0];
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InterpolateRbfParams const* pParams = (InterpolateRbfParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			const Location* pInputLocs = (Location*)(((U8*)pParams) + pParams->m_inputLocsOffset);
			const Location* pOutputLocs = (Location*)(((U8*)pParams) + pParams->m_outputLocsOffset);
			const float* pInputPoses = (float*)(((U8*)pParams) + pParams->m_inputPosesOffset);
			const float* pWeights = (float*)(((U8*)pParams) + pParams->m_weightsOffset);

			ExecuteCommandInterpolateRbfImpl(pHierarchyHeader, pParams, pInputLocs, pOutputLocs, pInputPoses, pWeights);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim
