#include "atomics.fxi"
#include "oit.fxi"

#ifndef MAX_NUM_OIT_LAYERS
	#define MAX_NUM_OIT_LAYERS 1
#endif

struct ForwardCsSrt
{
	Texture2D<uint2> m_objectId[kOitMaxNumLayers];
	Texture2D<uint> m_layerCount;
	RW_Texture2D<uint> m_rw_layerCount;
	RW_Texture2D<uint> m_motionVectorLayer;
	RW_RegularBuffer<uint> m_pixelLists;
	RegularBuffer<uint> m_pixelStartOffsets;
	RW_RegularBuffer<uint4> m_indirectArgs;
	RW_RegularBuffer<uint4> m_indirectOitCommitArgs;

	uint m_firstGdsCounter;
	uint m_numObjectIds;
	uint m_numLayers;

	bool m_hasLayerCount;
};

[NUM_THREADS(8, 8, 1)]
void Cs_CountPixelsPerObjectId(uint2 dispatchId : S_DISPATCH_THREAD_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	uint objectId = (pSrt->m_objectId[0][dispatchId.xy].x >> 17);

	if (objectId >= pSrt->m_numObjectIds)
		return;

	ulong exec = __s_read_exec();

	do
	{
		uint firstBit = __s_ff1_i32_b64(exec);

		uint uniformObjectId = __v_readlane_b32(objectId, firstBit);

		ulong laneMask = __v_cmp_eq_u32(objectId, uniformObjectId);

		uint counter = pSrt->m_firstGdsCounter + ((uniformObjectId*4)<<16);

		counter = ReadFirstLane(counter);

		if (__v_cndmask_b32(0, 1, laneMask))
		{
			NdAtomicIncrement(counter);
		}

		exec &= ~laneMask;
	}
	while (exec != 0);
}

[NUM_THREADS(8, 8, 1)]
void Cs_CountPixelsPerObjectIdOit(uint2 dispatchId : S_DISPATCH_THREAD_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	uint numLayers = 0;
	uint objectIds[MAX_NUM_OIT_LAYERS] = 0;

	if (pSrt->m_hasLayerCount)
	{
		numLayers = pSrt->m_layerCount[dispatchId.xy];

		if (numLayers == 0)
			return;

		for (uint i = 0; i < numLayers; i++)
		{
			objectIds[i] = OitKeyGetObjId(pSrt->m_objectId[i][dispatchId.xy]);
		}
	}
	else
	{
		for (uint i = 0; i < pSrt->m_numLayers; i++)
		{
			uint objectId = OitKeyGetObjId(pSrt->m_objectId[i][dispatchId.xy]);
			if (objectId == 0)
				break;

			objectIds[numLayers] = objectId;
			numLayers++;
		}

		pSrt->m_rw_layerCount[dispatchId.xy] = numLayers;

		if (numLayers == 0)
			return;
	}

	ulong exec = __s_read_exec();
	do
	{
		uint firstBit = __s_ff1_i32_b64(exec);

		uint uniformObjectId = 0;
		uint layers = __v_readlane_b32(numLayers, firstBit);
		for (uint i = 0; i < layers; i++)
		{
			uniformObjectId = __v_readlane_b32(objectIds[i], firstBit);
			if (uniformObjectId)
			{
				break;
			}
		}

		exec = 0;

		ulong laneMask = 0;
		for (uint i = 0; i < pSrt->m_numLayers; i++)
		{
			ulong mask = __v_cmp_eq_u32(objectIds[i], uniformObjectId);
			laneMask |= mask;

			objectIds[i] *= __v_cndmask_b32(1, 0, mask);

			exec |= __v_cmp_ne_u32(objectIds[i], 0);
		}

		uint counter = pSrt->m_firstGdsCounter + ((uniformObjectId * 4) << 16);
		counter = ReadFirstLane(counter);

		if (__v_cndmask_b32(0, 1, laneMask))
		{
			NdAtomicIncrement(counter);
		}
	}
	while (__v_cndmask_b32(0, 1, exec));
}

[NUM_THREADS(64, 1, 1)]
void Cs_AllocatePixelLists(uint dispatchId : S_DISPATCH_THREAD_ID, uint groupId : S_GROUP_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	uint objectId = dispatchId;
	if (objectId >= pSrt->m_numObjectIds)
		return;

	uint counter = (pSrt->m_firstGdsCounter >> 16) + (objectId * 4);
	uint pixelCounts = __ds_read_u32(counter, __kDs_GDS);

#if MAX_NUM_OIT_LAYERS > 1
	pSrt->m_indirectArgs[objectId] = uint4((pixelCounts + 63) / 64, pSrt->m_numLayers, 1, 0);
	pSrt->m_indirectOitCommitArgs[objectId] = uint4((pixelCounts + 63) / 64, 1, 1, 0);
#else
	pSrt->m_indirectArgs[objectId] = uint4((pixelCounts + 63) / 64, 1, 1, 0);
#endif

	uint swizzle = 0;
	uint prefixSum = pixelCounts;
	uint total = pixelCounts;

	swizzle = LaneSwizzle(total, 0x1F, 0x00, 0x01);
	prefixSum += __v_cndmask_b32(swizzle, 0, 0x5555555555555555);
	total += swizzle;

	swizzle = LaneSwizzle(total, 0x1F, 0x00, 0x02);
	prefixSum += __v_cndmask_b32(swizzle, 0, 0x3333333333333333);
	total += swizzle;

	swizzle = LaneSwizzle(total, 0x1F, 0x00, 0x04);
	prefixSum += __v_cndmask_b32(swizzle, 0, 0x0F0F0F0F0F0F0F0F);
	total += swizzle;

	swizzle = LaneSwizzle(total, 0x1F, 0x00, 0x08);
	prefixSum += __v_cndmask_b32(swizzle, 0, 0x00FF00FF00FF00FF);
	total += swizzle;

	swizzle = LaneSwizzle(total, 0x1F, 0x00, 0x10);
	prefixSum += __v_cndmask_b32(swizzle, 0, 0x0000FFFF0000FFFF);
	total += swizzle;

	prefixSum += __v_cndmask_b32(__v_readlane_b32(total, 31), 0, 0x00000000FFFFFFFF);

	__ds_write_u32(counter, prefixSum, __kDs_GDS);
}

[NUM_THREADS(64, 1, 1)]
void Cs_FinalizeAllocatedPixelLists(uint dispatchId : S_DISPATCH_THREAD_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	for (uint i = 64; i < pSrt->m_numObjectIds; i += 64)
	{
		uint lastLaneCounter = (pSrt->m_firstGdsCounter >> 16) + (i - 1) * 4;
		uint groupSum = __ds_read_u32(lastLaneCounter, __kDs_GDS);

		uint objectId = i + dispatchId;
		uint counter = (pSrt->m_firstGdsCounter >> 16) + (objectId * 4);

		__ds_add_u32(counter, groupSum, __kDs_GDS);
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_FillPixelLists(uint2 dispatchId : S_DISPATCH_THREAD_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	uint objectId = pSrt->m_objectId[0][dispatchId.xy].x >> 17;

	if (objectId >= pSrt->m_numObjectIds)
		return;

	ulong exec = __s_read_exec();

	do
	{
		uint firstBit = __s_ff1_i32_b64(exec);

		uint uniformObjectId = __v_readlane_b32(objectId, firstBit);

		ulong laneMask = __v_cmp_eq_u32(objectId, uniformObjectId);

		uint counter = pSrt->m_firstGdsCounter + ((uniformObjectId*4)<<16);

		counter = ReadFirstLane(counter);

		if (__v_cndmask_b32(0, 1, laneMask))
		{
			uint startOffset = pSrt->m_pixelStartOffsets[uniformObjectId];

			uint pixelIndex = NdAtomicIncrement(counter);

			pSrt->m_pixelLists[startOffset + pixelIndex] = (dispatchId.y << 16) + dispatchId.x;
		}

		exec &= ~laneMask;
	}
	while (exec != 0);
}

[NUM_THREADS(8, 8, 1)]
void Cs_FillPixelListsOit(uint2 dispatchId : S_DISPATCH_THREAD_ID, ForwardCsSrt* pSrt : S_SRT_DATA)
{
	uint numLayers = pSrt->m_layerCount[dispatchId.xy];
	if (numLayers == 0)
		return;

	uint objectIds[MAX_NUM_OIT_LAYERS] = 0;
	uint flags[MAX_NUM_OIT_LAYERS] = 0;

	for (uint i = 0; i < numLayers; i++)
	{
		uint2 key = pSrt->m_objectId[i][dispatchId.xy];
		objectIds[i] = OitKeyGetObjId(key);
		flags[i] = OitKeyGetFlags(key);
	}

	uint outputMotionVectorLayer = 0;
	for (uint i = 0; i < numLayers; i++)
	{
		if (flags[i] & kOitShouldOutputMotionVectorFlag)
		{
			outputMotionVectorLayer = i + 1;
			break;
		}
	}

	pSrt->m_motionVectorLayer[dispatchId.xy] = outputMotionVectorLayer;

	ulong exec = __s_read_exec();
	do
	{
		uint firstBit = __s_ff1_i32_b64(exec);

		uint uniformObjectId = 0;
		uint layers = __v_readlane_b32(numLayers, firstBit);
		for (uint i = 0; i < layers; i++)
		{
			uniformObjectId = __v_readlane_b32(objectIds[i], firstBit);
			if (uniformObjectId)
			{
				break;
			}
		}

		exec = 0;

		uint layerMask = 0;
		ulong laneMask = 0;
		for (uint i = 0; i < pSrt->m_numLayers; i++)
		{
			ulong mask = __v_cmp_eq_u32(objectIds[i], uniformObjectId);
			laneMask |= mask;

			objectIds[i] *= __v_cndmask_b32(1, 0, mask);
			layerMask |= __v_cndmask_b32(0, 1, mask) << i;

			exec |= __v_cmp_ne_u32(objectIds[i], 0);
		}

		uint counter = pSrt->m_firstGdsCounter + ((uniformObjectId * 4) << 16);
		counter = ReadFirstLane(counter);

		if (__v_cndmask_b32(0, 1, laneMask))
		{
			uint startOffset = pSrt->m_pixelStartOffsets[uniformObjectId];
			uint pixelIndex = NdAtomicIncrement(counter);

			pSrt->m_pixelLists[startOffset + pixelIndex] = OitPackPixelIndex(dispatchId.x, dispatchId.y, layerMask);
		}
	}
	while (__v_cndmask_b32(0, 1, exec));
}
