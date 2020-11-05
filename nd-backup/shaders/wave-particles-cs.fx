#include "global-funcs.fxi"
#include "water-funcs.fxi"
//
// Compute Shader - wave particles
//

//#define kMaxVertexBlocks	1024
// #define kMaxThreadX 32
// #define kMaxThreadY 32

#define kMaxThreadX 8
#define kMaxThreadY 8
#define kMaxThreadZ 4

#define kMaxThreadNormalX 32
#define kMaxThreadNormalY 32
#define kMaxThreadNormalZ 1

// Most of the wave-particles will be 32x32

// same as in water/wave-particle.h
#define kMaxParticles  300

struct WPInfo
{
	int   m_active;
	int   m_numParticles; // grid side size (num cols)
	int   m_side; // grid side size (num cols)
	int   m_kernel; // radius of kernel in grid units (ie 1,2,3..7 is too big)

	float m_time;	// Global time
	float m_velocity;
	float m_velocityMult; // useless
	float m_amplitudeXZMult;

	float m_amplitudeY;
	float m_amplitudeXZ;
	float m_amplitudeMaxY;
	int   m_offsetY;

	int   m_pad00;
	float m_blendValue;
	float m_velocity2;
	float m_velocityMultBlend;

	float m_scale1GridX;
	float m_scale1GridZ;
	float m_scale2GridX;
	float m_scale2GridZ;

	float m_strain;
	float m_pad1;
	float m_pad2;
	float m_pad3;

	float4 m_particles[kMaxParticles];  // position (x,y) + direction (z,w)
};

struct WaveParticlesSrtData
{
	WPInfo  * m_consts;
	RWStructuredBuffer<WPDisplacement>  m_displacementGrid;
	RWStructuredBuffer<uint4>			m_wpDataGrid;				// compressed displacement, strain, dx, dz
};

// Displacement data is:
// <x,y,z>  = displacement vector
// w  = Encoded Normal!!

StructuredBuffer<float4> displacementGrid : register(u0);


//groupshared float4  localParticle[kMaxParticles];

groupshared float3  localDisplacement[kMaxThreadX * kMaxThreadY * kMaxThreadZ];
groupshared float3  localNormal[kMaxThreadX * kMaxThreadY * kMaxThreadZ];
// groupshared float3  localNormalDx[kMaxThreadX * kMaxThreadY * kMaxThreadZ];
// groupshared float3  localNormalDz[kMaxThreadX * kMaxThreadY * kMaxThreadZ];


[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_WaveParticles(  uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, WaveParticlesSrtData* pSrt : S_SRT_DATA)
{
	WaveParticlesSrtData& srt = *pSrt;

	int rx = dispatchThreadId.x; // current row (ie x)
	int cz = dispatchThreadId.y; // current col (ie z)

	int gi = dispatchThreadId.z; // particul group

	WPInfo *info = srt.m_consts;

	// Grid index

	int index  = rx * info->m_side + cz;
	int gindex = index % kMaxParticles;

	// is this something like groupThreadId
	int tindex = (rx % kMaxThreadX) * kMaxThreadX + (cz % kMaxThreadX); // thread index


#if 0
	// All threads load particles into local storage
	{
		// Each thread in the thread group will take care of loading some particles into groupshared memory

		// 8 * 8
		int numThreads = kMaxThreadX * kMaxThreadY;
		int numParticlesPerThread = kMaxParticles / numThreads;
		// slop
		for (int i=0; i < numParticlesPerThread; i++) {
			int pindex = tindex * numParticlesPerThread + i;
			// Copy from 
			localParticle[pindex] = info->m_particles[pindex];
		}

		if (tindex == 0 && numParticlesPerThread * numThreads != kMaxParticles) {
		 	for (int i=numParticlesPerThread * numThreads; i < kMaxParticles; i++) {
		 		localParticle[i] = info->m_particles[i];
		 	}
		}		
	}

	// Wait until all intra-thread operations finished
	GroupMemoryBarrierWithGroupSync();
#endif


	float3 disp = 0.0;
	float3 normal = 0.0;

	float3 normalDx = float3(0);
	float3 normalDz = float3(0);
	
	// non wrapped coordinates of grid element
	int srx = rx + info->m_side;  
	int scz = cz + info->m_side;

	int2 gxz = int2(rx, cz); // grid index

	float tstep   = (float) info->m_time * (float) info->m_velocity;
	float tstep2  = (float) info->m_time * (float) info->m_velocity2;

	//	float tstepblend = lerp (tstep, tstep2, info->m_blendValue);

	float tvel1 = (float) info->m_velocity;
	float tvel2 = (float) info->m_velocity2;

	int   side  = (int)   info->m_side;
	float fside = (float) info->m_side;
	int2  side1 = int2(side-1,side-1);

	int2  side2 = int2(side,side);
	int   kernel = info->m_kernel;
	float fkernel = (float) info->m_kernel;

	float kernelRadius = 3.14159265358979f / (float) kernel; 

	float amplitudeY = info->m_amplitudeY;
	float amplitudeXZ = info->m_amplitudeXZ;
	float amplitudeMaxY = info->m_amplitudeMaxY;

	// radius of kernel in grid units!!!!
	// so radius should be around  / params.m_side
	int2 mkernel = int2(-kernel, -kernel);
	int2 kernel2 = int2(kernel, kernel);

	float a = 0;
	float b = 0;

	// Divide the number of particles
	int numParticlesPerWaveFront = info->m_numParticles / kMaxThreadZ;

	for (int ppi=0; ppi < numParticlesPerWaveFront; ppi++) {
		
		// corrected particle index per wavefront
		int pi = numParticlesPerWaveFront * gi + ppi;

		float4 posDir = info->m_particles[pi]; 
		//float4 posDir =  localParticle[pi];      // using local storage

		float2 newPos = posDir.xy + posDir.zw * tstep;

		// Index the position to the grid
		// The grid is normalized from 0..1

		// We use the pattern for indices. Given 22 the closest to the particle position
		// (the order of indices is row major), but the indices are addressed as: (z,x)
		// 00 01 02 03 04
		// 10 11 12 13 14
		// 20 21 22 23 24
		// 30 31 32 33 34
		// 40 41 42 43 44

		// fractional part

		float2 fracXZ  = (newPos - floor(newPos));

		// Integral part
		float2 psXZ = newPos.xy / fside;

		float2 pfracXZ = (psXZ - floor(psXZ)) * fside;

		int2 indexXZ = (int2) pfracXZ;

		// Coarse rejection
		// Compute the extent (by the kernel) of this particle (after frac and pmod)
		int2 minXZ = indexXZ - kernel2; // int2(-kernel, -kernel);
		int2 maxXZ = indexXZ + kernel2; // + int2( kernel,  kernel);

		int2 minXZ2 = minXZ + side1;
		int2 maxXZ2 = maxXZ + side1;

		int2 minXZ3 = minXZ - side1;
		int2 maxXZ3 = maxXZ - side1;

		bool3 xb = bool3(((minXZ.x  <= gxz.x) && (gxz.x <= maxXZ.x)), ((minXZ2.x <= gxz.x) && (gxz.x <= maxXZ2.x)), ((minXZ3.x <= gxz.x) && (gxz.x <= maxXZ3.x)));
		bool3 yb = bool3(((minXZ.y  <= gxz.y) && (gxz.y <= maxXZ.y)), ((minXZ2.y <= gxz.y) && (gxz.y <= maxXZ2.y)), ((minXZ3.y <= gxz.y) && (gxz.y <= maxXZ3.y)));

		// If the grid element is inside any of the regions and wrap around regions of the particle
		if ( any(xb) && any(yb) )
		{
			a += 1;
			// Adjust for wrap around cases (particle wrap in the "left" and "right" of grid

			gxz += bool2(xb.y, yb.y) ? -side : 0;
			gxz += bool2(xb.z, yb.z) ?  side : 0;

			int2 xzi = gxz - indexXZ; // ssxz - indexXZ;
			float2 dd = (float2(xzi.x, xzi.y) - fracXZ);

			// compute the right amount of influence
			dd = ((dd > fkernel) ? fkernel : (dd < -fkernel) ? -fkernel : dd) *  kernelRadius;

			float2 s, c;
			sincos(dd, s, c);

			float2 influenceDXZ = c + float2(1.0f);
			float2 influenceXZ  = -s;

			float influence = influenceDXZ.x * influenceDXZ.y; 
			float3 disp3 = influence * float3(amplitudeXZ * influenceXZ.x, amplitudeY, amplitudeXZ * influenceXZ.y);
			// All expanded
			//float3 disp3 = (c.x + 1) * (c.y + 1) * float3(amplitudeXZ * (-s.x), amplitudeY, amplitudeXZ * (-s.y));

			// float2 cosXZ =  influenceDXZ - 1.0f;
			// float2 sinXZ = -influenceXZ;

			// float dx = .25 * sinXZ.x * ( cosXZ.y + 1.0f );
			// float dy = .25 * sinXZ.y * ( cosXZ.x + 1.0f );

 			// float3 dvx = float3( (c.y + 1) * s.x * s.x * amplitudeXZ - c.x * (c.x + 1) * (c.y + 1) * amplitudeXZ, 
			// 					-s.x * (c.y + 1) * amplitudeY,
			// 					s.x * s.y * (c.y + 1) * amplitudeXZ);
			// float3 dvz = float3(s.x * s.y * (c.x + 1) * amplitudeXZ, 
			// 					-(c.x + 1) * s.y * amplitudeY,
			// 					 (c.x + 1) * s.y * s.y * amplitudeXZ - c.y * (c.x + 1) * (c.y + 1) * amplitudeXZ);

			// kind of works
			// float3 dkx = float3(-1.0, amplitudeY * dx, 0);
			// float3 dkz = float3( 0, amplitudeY * dy, -1.0);
			//float3 nn = normalize(cross(dkz, dkx));

			// Maxima's partial derivatives
			// float3 mdx = float3(1  +amplitudeXZ * (s.x * s.x * (c.y + 1) - c.x * (c.x + 1) * (c.y + 1)), -amplitudeY * s.x * (c.y +1), amplitudeXZ * s.x *(c.y+1) * s.y );
			// float3 mdz = float3(amplitudeXZ * ((c.x +1) * s.x * s.y), -amplitudeY * (c.x + 1) * s.y, 1+amplitudeXZ * ((c.x + 1) * s.y * s.y - (c.x+1) * c.y * (c.y+1)));
			// float3 nn = normalize(cross(mdz, mdx));
			{
				// Accumulate displacement 
				disp += disp3; 

				// normal += nn;
			}
		}
	}

	// blending to other frequency

	if (info->m_blendValue > 0) {

		float3 disp1 = disp;
		//float3 normal1 = normal;

		disp = 0;
		normal = 0;

		for (int ppi=0; ppi < numParticlesPerWaveFront; ppi++) {
		
			// corrected particle index per wavefront
			int pi = numParticlesPerWaveFront * gi + ppi;

			float4 posDir = info->m_particles[pi]; 
			//float4 posDir =  localParticle[pi];      // using local storage

			// bad lerping
			float2 newPos = posDir.xy + posDir.zw * tstep2;

			// Index the position to the grid
			// The grid is normalized from 0..1

			// We use the pattern for indices. Given 22 the closest to the particle position
			// (the order of indices is row major), but the indices are addressed as: (z,x)
			// 00 01 02 03 04
			// 10 11 12 13 14
			// 20 21 22 23 24
			// 30 31 32 33 34
			// 40 41 42 43 44

			// fractional part

			float2 fracXZ  = (newPos - floor(newPos));

			// Integral part
			float2 psXZ = newPos.xy / fside;

			float2 pfracXZ = (psXZ - floor(psXZ)) * fside;

			int2 indexXZ = (int2) pfracXZ;

			// Coarse rejection
			// Compute the extent (by the kernel) of this particle (after frac and pmod)
			int2 minXZ = indexXZ - kernel2; // int2(-kernel, -kernel);
			int2 maxXZ = indexXZ + kernel2; // + int2( kernel,  kernel);

			int2 minXZ2 = minXZ + side1;
			int2 maxXZ2 = maxXZ + side1;

			int2 minXZ3 = minXZ - side1;
			int2 maxXZ3 = maxXZ - side1;

			bool3 xb = bool3(((minXZ.x  <= gxz.x) && (gxz.x <= maxXZ.x)), ((minXZ2.x <= gxz.x) && (gxz.x <= maxXZ2.x)), ((minXZ3.x <= gxz.x) && (gxz.x <= maxXZ3.x)));
			bool3 yb = bool3(((minXZ.y  <= gxz.y) && (gxz.y <= maxXZ.y)), ((minXZ2.y <= gxz.y) && (gxz.y <= maxXZ2.y)), ((minXZ3.y <= gxz.y) && (gxz.y <= maxXZ3.y)));

			// If the grid element is inside any of the regions and wrap around regions of the particle
			if ( any(xb) && any(yb) )
			{
				a += 1;
				// Adjust for wrap around cases (particle wrap in the "left" and "right" of grid

				gxz += bool2(xb.y, yb.y) ? -side : 0;
				gxz += bool2(xb.z, yb.z) ?  side : 0;

				int2 xzi = gxz - indexXZ; // ssxz - indexXZ;
				float2 dd = (float2(xzi.x, xzi.y) - fracXZ);

				// compute the right amount of influence
				dd = ((dd > fkernel) ? fkernel : (dd < -fkernel) ? -fkernel : dd) *  kernelRadius;

				float2 s, c;
				sincos(dd, s, c);

				float2 influenceDXZ = c + float2(1.0f);
				float2 influenceXZ  = -s;

				float influence = influenceDXZ.x * influenceDXZ.y; // influenceDx[i] * influenceDz[j];
				float3 disp3 = influence * float3(amplitudeXZ * influenceXZ.x, amplitudeY, amplitudeXZ * influenceXZ.y);

				// float2 cosXZ =  influenceDXZ - 1.0f;
				// float2 sinXZ = -influenceXZ;
				// float dx = sinXZ.x * ( ( cosXZ.y + 1.0f ) );
				// float dy = ( cosXZ.x + 1.0f ) * sinXZ.y;
				
				// float3 dvx = float3( (c.y + 1) * s.x * s.x * amplitudeXZ - c.x * (c.x + 1) * (c.y + 1) * amplitudeXZ, 
				// 					-s.x * (c.y + 1) * amplitudeY,
				// 					s.x * s.y * (c.y + 1) * amplitudeXZ);
				// float3 dvz = float3(s.x * s.y * (c.x + 1) * amplitudeXZ, 
				// 					-(c.x + 1) * s.y * amplitudeY,
				// 					 (c.x + 1) * s.y * s.y * amplitudeXZ - c.y * (c.x + 1) * (c.y + 1) * amplitudeXZ);
				{
					// Accumulate displacement 
					disp += disp3; // float3(dispX, dispY, dispZ);

					// Accumulate normal
					//normal += float3(dx, 1.f, dy);
				}
			}
		}
		disp = lerp(disp1, disp, info->m_blendValue);
		//normal = lerp(normal1, normal, info->m_blendValue);
	}

	localDisplacement[groupIndex] = disp;
	//localNormal[groupIndex] = normal;

	// Wait until all intra-thread operations finished
	GroupMemoryBarrierWithGroupSync();

	// Now for the first waveFront of every thread, add all the displacements
	if (gi == 0) {
		disp   = 0;
		normal = 0;

		// normalDx = 0;
		// normalDz = 0;

		// 2D group index
		// Su,, all the displacements from the Z-direction of a single thread entry in a ThreadGroup
		for (int i=0; i < kMaxThreadZ; i++) {
			// groupIndex ::== groupThreadId.x +  groupThreadId.y * (kMaxThreadX)  + groupThreadId.z * (kMaxThreadX * kMaxThreadX);
			int ltgindex = groupThreadId.x +  groupThreadId.y * (kMaxThreadX)  + i * (kMaxThreadX * kMaxThreadX);
			disp   += localDisplacement[ltgindex];
			//normal += localNormal[ltgindex];

			// normalDx += localNormalDx[ltgindex];
			// normalDz += localNormalDz[ltgindex];
		}
		disp.y += info->m_offsetY;

#if 0
		normal = 0;
		int checkSize = 16;
		int checkSizeT = checkSize / 2;
		if ((rx % checkSize) < checkSizeT) {
			if ((cz % checkSize) < checkSizeT) {
				disp.xyz = float3(0,1,0);
			} else {
				disp.xyz = float3(0,0,0);
			}
		} else {
			if ((cz % checkSize) < checkSizeT) {
				disp.xyz = float3(0,0,0);
			} else {
				disp.xyz = float3(0,1,0);
			}
		}

#endif
		srt.m_displacementGrid[index].m_disp   = float4(disp.x, disp.y, disp.z,0);

		uint4 compData;
		WPCompressDisp(disp, compData);
		srt.m_wpDataGrid[index] = compData;
	}
}


[numthreads(kMaxThreadNormalX, kMaxThreadNormalY, kMaxThreadNormalZ)]
void Cs_WaveParticlesNormals(  uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, WaveParticlesSrtData *pSrt : S_SRT_DATA)
{
	WaveParticlesSrtData& srt = *pSrt;

	int rx = dispatchThreadId.x; // current row (ie x)
	int cz = dispatchThreadId.y; // current col (ie z)

	int gi = dispatchThreadId.z; // particular group (NOT used)

	WPInfo *info = srt.m_consts;

	// Grid index
	int index  = rx * info->m_side + cz;
	int gindex = index % kMaxParticles;


	// is this something like groupThreadId
	int tindex = (rx % kMaxThreadX) * kMaxThreadX + (cz % kMaxThreadX); // thread index

#define NID(z, x) (( x + info->m_side) % info->m_side) + (( z + info->m_side) % info->m_side) * info->m_side

	int index00 =  NID(rx    , cz);
	int index10 =  NID(rx + 1, cz);
	int index01 =  NID(rx    , cz + 1);
	int index11 =  NID(rx + 1, cz + 1);

	int indexN0 =  NID(rx - 1, cz);
	int index0N =  NID(rx    , cz - 1);

	// blend the scale :
	float scaleX = lerp(info->m_scale1GridX, info->m_scale2GridX, info->m_blendValue) / 32.0;
	float scaleZ = lerp(info->m_scale1GridZ, info->m_scale2GridZ, info->m_blendValue) / 32.0;

	float3 v00 =  srt.m_displacementGrid[index00].m_disp.zyx;
	float3 v10 =  srt.m_displacementGrid[index01].m_disp.zyx + float3(scaleX, 0, 0);
	float3 v01 =  srt.m_displacementGrid[index10].m_disp.zyx + float3(     0, 0, scaleZ);
	float3 vN0 =  srt.m_displacementGrid[index0N].m_disp.zyx - float3(scaleX, 0, 0);
	float3 v0N =  srt.m_displacementGrid[indexN0].m_disp.zyx - float3(     0, 0, scaleZ);
	float3 v11 =  srt.m_displacementGrid[index11].m_disp.zyx - float3(scaleX, 0, scaleZ);

	float3 dx1 = v10 - v00; 
	float3 dx2 = v0N - v00; 
	float3 dx3 = vN0 - v00; 
	float3 dx4 = v01 - v00; 

	float3 dw1 = (dx3 - dx1) / scaleX;
	float3 dw2 = (dx2 - dx4) / scaleZ;
	
	srt.m_displacementGrid[index].m_dx = float4(dw1, 0);
	srt.m_displacementGrid[index].m_dz = float4(dw2, 0);

	//	float3 dz1 = v01 - v00;	
	//	float3 dz2 = v10 - v00;	
	//	float3 dz3 = v0N - v00;	
	//	float3 dz4 = vN0 - v00;	

	// float3 v1 = cross(dz1, dx1);
	// float3 v2 = cross(dz2, dx2);
	// float3 v3 = cross(dz3, dx3);
	// float3 v4 = cross(dz4, dx4);
	// float3 normal = normalize(v1 + v2 + v3 + v4);
	// normal = normal;

	// why normalize it??
	// v1 = normalize(dx3 - dx1);
	// v2 = normalize(dx2 - dx4);
	//	srt.m_displacementGrid[index].m_normal = float4(normal,0);

	// Compute Strain
	// float strainX = 1.0 + ((v00.z - v01.z) /  scaleX);
	// float strainZ = 1.0 + ((v00.x - v10.x) /  scaleZ);
	// srt.m_displacementGrid[index].m_strainX = (strainX > info->m_strainX) ? strainX : 0;
	// srt.m_displacementGrid[index].m_strainZ = (strainZ > info->m_strainZ) ? strainZ : 0;

	// Compute strain as difference of area of the distorted wp element
	// 0 strain if distorted element is the same size as base element
	// 1 strain if element has compressed
	float3 v1 = cross(v10 - v00, v01 - v00);
	//	float3 v2 = cross(v01 - v11, v10 - v11);
	float3 v2 = cross(v01 - v11, v10 - v11);
	float  area = 1.0 - info->m_strain *  (.25 * (length(v1) + length(v2)) / (scaleX * scaleZ));
	srt.m_displacementGrid[index].m_strain = max(0, area);

	// create the compressed versions
	uint4 compData = srt.m_wpDataGrid[index];
	WPCompressData(srt.m_displacementGrid[index], compData);
	srt.m_wpDataGrid[index] = compData;
}
 
