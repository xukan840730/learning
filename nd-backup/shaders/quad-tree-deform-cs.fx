//
// Compute Shader - Quad Tree instancing block and sampling
//

#include "global-funcs.fxi"
#include "water-funcs.fxi"
#include "atomics.fxi"

#define kMaxThreadFilterX 1
#define kMaxThreadFilterY 1
#define kMaxThreadFilterZ 1

#define kMaxThreadDeformX 17
#define kMaxThreadDeformY 17
#define kMaxThreadDeformZ 1

/// --------------------------------------------------------------------------------------------------------------- ///
// If the ray intersects the AABB, tMin and tMax are set to the t value of the closest
// and furthest intersection with the AABBs planes. Note that a t of 1.0f represents
// the entire distance from start to end.
// From aabb.h
bool IntersectRay(float3 bbMin, float3 bbMax, float3 start, float3 end, out float tMin, out float tMax)
{
	float3 dir = end - start;

	float3 invDir = 1.0 / dir;

	// v128 invDirRaw = Recip(dir).QuadwordValue();
	// v128 notFiniteMask = SMATH_VEC_NOTFINITE(invDirRaw);
	// // substitute infinities with values that are very large (but still leave plenty of precision for the multiplication below)
	// Vector invDir(SMATH_VEC_SEL(invDirRaw, SMATH_VEC_REPLICATE_FLOAT(1.0e19f), notFiniteMask));

	float3 minMinusStart = (bbMin - start);
	float3 maxMinusStart = (bbMax - start);
	float3 t0 = minMinusStart * invDir;
	float3 t1 = maxMinusStart * invDir;

	float3 minT0T1 = min(t0, t1);
	float3 maxT0T1 = max(t0, t1);

	tMin = max(minT0T1.x, max(minT0T1.y, minT0T1.z));
	tMax = min(maxT0T1.x, min(maxT0T1.y, maxT0T1.z));

	return (tMin <= tMax)? true : false;
}

// Filter all deformation objects by number of boxes
[numthreads(kMaxThreadFilterX, kMaxThreadFilterY, kMaxThreadFilterZ)]
void Cs_QuadTreeDeformFilter( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId   = groupIndex.x;
	//int gridIdX = groupThread.x;   // dispatchThreadId
	//int gridIdZ = groupThread.y;   // dispatchThreadId

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;

	QTDeformation * deformation    = srt.m_data->m_deformation;
	//	uint * compact = srt.m_data->m_compactList;

	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
		return;

	// Box
	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[boxId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[boxId].m_lerp;

	float2 xzOffset      = instanceInfo.xy + float2(.00001, .00001);
	float size           = instanceInfo.z;

	// center of box
	float3 minBox = float3(xzOffset.x, consts->m_offset, xzOffset.y);
	float3 maxBox = minBox + float3(size, consts->m_terrainMag, size);

	// grow box by extend of water
	minBox -= float3(consts->m_extendXZ, 0,                 consts->m_extendXZ);
	maxBox += float3(consts->m_extendXZ, consts->m_extendY, consts->m_extendXZ);

	// Get a compacted list
	uint intersectionObjects = 0;
	bool intersects = false;
	for (int objId=0; objId < deformation->m_numObjects; objId++) {
		
		// grow box by extend of capsule
		float3 testMinBox = minBox - float3(deformation->m_object[objId].m_extent);
		float3 testMaxBox = maxBox + float3(deformation->m_object[objId].m_extent);

		float tMin, tMax;
		// create a capsule per segment
		float3 pt0 = deformation->m_object[objId].m_pos0.xyz;

		if ( IntersectRay(testMinBox, testMaxBox, deformation->m_object[objId].m_pos0.xyz, deformation->m_object[objId].m_pos1.xyz, tMin, tMax) ) {

			// We want to know if the infinitely long ray from pos0 to pos1 intersects the box
			// That means that the interval tMin and tMax has to contain [0...1] (ie. the line)
			if (tMax >= 0.0 && tMin <= 1.0)
			{
				intersectionObjects |= (1 << objId);
				intersects = true;
			}
		}
	}

	if (intersects) {
		// Atomically add to the counter of boxes to test further in the compacted list

		uint boxCounter = NdAtomicIncrement(srt.m_data->m_gdsNumBoxesCounter);

		// In the compacted list, we store the boxId and the bit field of which objects to test against
		// The value is encoded into a compacted list
		uint  boxObj = (boxId << 16) | (intersectionObjects & 0xFFFF);
		srt.m_data->m_compactList[boxCounter] = boxObj;
	}


	// ThreadGroupMemoryBarrier();
	// Wait for all the threads to finish
	// store the number of boxes 
	// if (boxId == 0)
	// {
	// 	//uint numBoxes = NdAtomicIncrement(srt.m_data->m_gdsNumBoxesCounter);
	// 	// Need to store the num boxes someplace!!!!
	// 	//consts->m_numBoxesCompactedList = numBoxes;
	// }
}


[numthreads(kMaxThreadDeformX, kMaxThreadDeformY, kMaxThreadDeformZ)]
void Cs_QuadTreeDeform( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int entryId = groupIndex.x;    // entry
	int gridIdX = groupThread.x;   // dispatchThreadId  (grid row)
	int gridIdZ = groupThread.y;   // dispatchThreadId  (grid col)

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	QTDeformation *deformation    = srt.m_data->m_deformation;

	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// Use the atomic to determine how many boxes we have to process
	int numCompactBoxes = NdAtomicGetValue(srt.m_data->m_gdsNumBoxesCounter);
	
	// one thread per block
	if (entryId >= numCompactBoxes || numCompactBoxes == 0 || deformation->m_numObjects == 0) 
		return;

	// Box id is the upper 16 bits, the bitfield correspond to each deformation object
	uint compactData = srt.m_data->m_compactList[entryId]; 
	uint boxId  = compactData >> 16;
	uint intersectionObjects = compactData & 0xFFFF; 

	// Vertex index
	int index  = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	float3  pt = srt.m_data->m_output[index].m_position.xyz;

	float   uvi = 1.0 / deformation->m_numUvSlots;

	float n = 0;
	float3 qt = 0;
	float ppg = 0;

	float3 color = srt.m_data->m_output[index].m_color1.xyz;
	float3 deformPt = srt.m_data->m_output[index].m_position.xyz;
	float3 deformPrevPt = srt.m_data->m_output[index].m_positionPrev.xyz;
	float  deformVal = 0;

	int numObj =deformation->m_numObjects;
	int pushIn = 0;	
	for (int objId=0; objId < numObj; objId++) 
	{
		DeformationObject obj = deformation->m_object[objId];
		
		float3	opos0		= deformation->m_object[objId].m_pos0.xyz;
		float3	opos1		= deformation->m_object[objId].m_pos1.xyz;
		float3	up			= deformation->m_object[objId].m_up.xyz;
		float	startLength = deformation->m_object[objId].m_startLength;
		float	endLength	= deformation->m_object[objId].m_endLength;
		float	width		= deformation->m_object[objId].m_width;

		float	uoffset		= deformation->m_object[objId].m_uOffset;
		float	uscale		= deformation->m_object[objId].m_uScale;

		float	vscale		= deformation->m_object[objId].m_vScale;
		float	voffset		= deformation->m_object[objId].m_vOffset;
		float	depth		= deformation->m_object[objId].m_depth;
		float   blend       = deformation->m_object[objId].m_blend;
		float   uvSlot      = deformation->m_object[objId].m_uvSlot;
		float   attachOffsetBack  = deformation->m_object[objId].m_attachmentOffsetBack;
		float   attachOffsetFront = deformation->m_object[objId].m_attachmentOffsetFront;
		uint    pushUp     = deformation->m_object[objId].m_invert;
		float3 pos0 = opos0 + up * attachOffsetBack;
		float3 pos1 = opos1 + up * attachOffsetFront;

		// float dcd = length(consts->m_center.xyz - pos0);
		// float factor = lerp(1.0, deformation->m_increase0, saturate(linearparam(deformation->m_increase1, deformation->m_increase2, dcd)));
		// width *= factor;
		float maskPush = 1;
		if ((intersectionObjects & (1 << objId)) != 0) 
		{
			color *= .5;

			float3 dir  = pos1 - pos0;
			float3 dirn = normalize(dir);
			float3 rightn = normalize(cross(dir, up));
			float3 upn = normalize(up);

			float   ld = endLength - startLength; // length(dir);
			float3  dv = pt - pos0;

			float  dd = dot(dv, dirn);
			float  ud = dot(dv, upn);
			float  rd = dot(dv, rightn);			

			// normalized param along boat length
			float v = saturate((dd - startLength) / ld); //   dd / ld);
			float vn = saturate(v);  // normalized parameterization along the length

			float u = saturate(rd / (2.0*width) + .5); 
			u = uoffset + uscale * u;
			// offset and scale by uv-slot
			//u = u * uvi;
				
			v = vscale * v + voffset;
			v = v * uvi + uvSlot;
				
			float2 depthMask = 
				srt.m_data->m_texturesAndSamplers->m_boatMapTx.SampleLevel(srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
																		   float2(u, v), 0).rg;

			// depth of original vertex
			float pdepth = ud;

			// mask depth
			float mdepth = depthMask.r * depth;

			// check if we should pushup (1) or pushdown (0) the water
			// We pushup for cases like the camera when is very close to the surface and we are still underwater
			mdepth = (pushUp) ?  max(mdepth, pdepth) :  min(mdepth, pdepth);

			float3 newPt = lerp(pt,  pos0 + dd * dirn + rd * rightn + mdepth * upn , depthMask.g);

			// blend out if normal is too far away from vertical
			// dot(upn.y, Y) > .5  for 30 degrees or greater
			blend = lerp(1, blend, linearstep(.4, .5, upn.y));

			bool inside = false;
			if (dd >= startLength && dd <= endLength && abs(rd) < width) 
			{
				color = float3(0,1,0);
				
				float3 newPt2 = lerp(newPt, pt, blend);
				
				float nl = length(newPt2 - pt);

				
				if (nl > deformVal) {
					deformPt = newPt2;
					deformPrevPt = newPt2;
					deformVal = nl;
					color.xy = depthMask.xy;
					maskPush = 0;
					pushIn = 1;
				}

			}

			float3 wupn = float3(0,1,0);
			
			// compute closest point on segment on water line
			// locators are at water line
			float3 wpos0 = pos0 + obj.m_startLengthForPlane * dirn; // startLength    start length rad
			float3 wpos1 = pos0 + obj.m_endLengthForPlane   * dirn; // endLength    end length rad

			float3 seg = wpos1 - wpos0;
			float3 nseg = normalize(seg);
			float  lseg = length(wpos1 - wpos0); //  (endLength - startLength);

			float  sd = min(1,max(0, dot(pt - wpos0,  nseg) / lseg));  // param of closest pt over segment: normalized

			float3 ptSeg = wpos0 + sd * seg; // closest pt on segment
			float  r  = length(pt - ptSeg);  // distance from pt to segment

			float  above = dot(pt - ptSeg, wupn); // height above water line (0 == boat water line)
			
			// rounded distance around segment
			float roundist = lerp( obj.m_radiusBack, obj.m_radiusFront, sd );

			 // if (r < roundist) {
			 // 	color = float3(1,0,0);
			 // 	qt += pt;
			 // 	n++;
			 // }

			// we need to compute height from point to water point
			float useg = dot(pt - wpos0, wupn);


			//color = sd;
			
			if (obj.m_useSecondary && r < roundist) {
				//color = float3(1,0,0);

				color = (sd == 0) ? float3(1,0,0) : ((sd >= 1.0) ? float3(0,1,0) : sd );


				// Create a plane  and project all points under water
				float3 wlpt0 = wpos0 + obj.m_heightBack  * upn;
				float3 wlpt1 = wpos0 + obj.m_heightFront * upn;

				float3 qw = pt - wlpt0;
				float  drdw = dot(qw, nseg);    // dir
				float  updw = dot(qw, dirn);    // upn
				float  rgdw = dot(qw, rightn);  // rightn

				float3 qpplane = wlpt0 + drdw * nseg + rgdw * rightn;
				
				float3  updwl = pt - qpplane;
				float   push = dot(updwl, upn);

				// 
				//push = min(0, push); // push factor 

				// modulate by distance (not great)
				//float factor = 1.0-saturate(r/roundist);

				float f1 = obj.m_radiusBackFactor;
				float f2 = obj.m_radiusFrontFactor; // 1.15469885
			
				float exponent = lerp(f1 , f2, sd);
				float powfn = saturate(pow(exponent, -(r*r))); // fade to push
				float factor = powfn;

				float3 newPt2 = lerp(pt, qpplane, factor);
				
				if (push > 0) 
				{
					//float3 newPt2 = qpplane;
					float nl = length(newPt2 - pt);
					if (nl > deformVal) {
						deformPt = newPt2;
						deformPrevPt = newPt2;
						deformVal = nl;
						color = float3(0,1,factor); // r / roundist;
					}
				}
			}
		}
	}

	float3 diff = deformPt - srt.m_data->m_output[index].m_position.xyz;
	srt.m_data->m_output[index].m_position.xyz = deformPt;
	srt.m_data->m_output[index].m_positionPrev.xyz = deformPt;
	//srt.m_data->m_output[index].m_color1.xyz   = color;  // DO NOT MODIFY in FINAL color1 !!!! is important for Shader
}


[numthreads(kMaxThreadDeformX, kMaxThreadDeformY, kMaxThreadDeformZ)]
void Cs_QuadTreeDeformRipples( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
}

#define kMaxThreadX 17
#define kMaxThreadY 17
#define kMaxThreadZ 1

			
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeDeformWaterProperties( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId   = groupIndex.x;
	int gridIdX = groupThread.x;   // dispatchThreadId
	int gridIdZ = groupThread.y;   // dispatchThreadId

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	int	sideBlock = consts->m_sideBlock;
	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
		return;

	int index  = boxId * numVerticesPerBlock; 
	index     += (gridIdZ * consts->m_sideBlock) + gridIdX;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;

	float2 uvOffset = consts->m_waterPropertiesScaleOffset.xy;
	float  uvScale  = consts->m_waterPropertiesScaleOffset.z;
	float invTexSize = consts->m_waterPropertiesDelta.w;
	float texScale   = consts->m_waterPropertiesDelta.z;
	float texSize    = 1.0f / invTexSize;
	float texelsPerMeter  = texSize / (2.0f * texScale);
	float2 camDeltaMeters = consts->m_waterPropertiesDelta.xy;
	float2 camDeltaTexels = camDeltaMeters * texelsPerMeter;
	float2 camDeltaUv     = camDeltaTexels * invTexSize;

	// float timeDelta = consts->m_flowInterval;


	float2 uv;	
	// need a center and a scale for the water properties buffer
	uv = 1.0 - (((xz - (uvOffset - uvScale)) / (2.0 * uvScale) ));
	//uv -= camDeltaUv; //this isnt needed -popka

	// get water properties offset and normal
	float3 offset  = textures->m_displacementTx.SampleLevel(textureSampler, uv, 0).rgb;	
	float3 normal  = textures->m_displacementNormalTx.SampleLevel(textureSampler, uv, 0).xyz;	

	// blend out on the edges
	float2 uvDist = (2.* uv) - float2(1,1);
	float  uvMask = saturate((1.0 - dot(uvDist, uvDist))) ;

	// Rerange the offset by the offset minimum -> [0, 0, 0]
	offset = (offset - float3(consts->m_waterPropertiesOffsetMinimum)) * float3(consts->m_waterPropertiesOffsetScale);

	
	// The normal needs to be deranged: [0..2] -> [-1..1]
	normal.rgb = normal.rgb - 1;
	
	//normal.rgb *= normal.a;
	//normal.rgb *= uvMask;
	normal.rgb = lerp(float3(0,0,1),normal.rgb, uvMask);
	normal.r = -normal.r;

	offset *= uvMask;

	// Add the offset
	srt.m_data->m_output[index].m_position.xyz += offset; //current pos
	// REMOVE!!!
	//srt.m_data->m_output[index].m_position.xyz += float3(offset.x, consts->m_debug0 * offset.y, offset.z); //current pos
	srt.m_data->m_output[index].m_positionPrev.xyz += offset; //last pos


	
	//normal.xyz = normalize(srt.m_data->m_output[index].m_normal + normal.xzy); //yz swap for 'tangent space'

	normal.xyz = normalBlendUnity(srt.m_data->m_output[index].m_normal, normal.xyz);

	srt.m_data->m_output[index].m_normal = normal;

}

