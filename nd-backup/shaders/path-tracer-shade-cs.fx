#include "global-funcs.fxi"
#include "path-tracer-raycast.fxi"
#include "path-tracer-kd-tree.fxi"
#include "path-tracer-level.fxi"

struct PtGlobals
{
	float4x4						m_screenToWorldMat;
	float4							m_screenSize;
	float4							m_cameraWS;
	float4							m_skyColor;

	uint4							m_pixelScaleOffset;
	uint							m_timerCount;
	uint							m_numThreadsX;
	uint							pad[2];
};

struct PtDebug
{
	uint			m_enable;
	uint			m_useMailbox;
	uint			m_castShadows;
	uint			m_defaultLighting;
	uint			m_useShadowRay;
	uint			m_traversalMode;
	uint			m_testMode;
	uint			m_shaderId;

	uint			m_accelType;
	uint			m_debugMode;
	uint			m_selBounce;
	uint			m_nodeScale;
	uint			m_leafScale;
	uint			m_sphereScale;
	uint			m_instScale;
	uint			m_triScale;
	float			m_debugScale;

	uint			m_kdTreeLevel;
	uint			m_kdLeafSize;
	uint			m_kdTraversalIndex;
	uint			m_kdStackDepth;

	int				m_kdStartPrim;
	int				m_kdEndPrim;

	float			m_shadowOffset;
};

struct PtPickerData
{
	float4							m_rayPos;
	float4							m_rayDir;
	float4							m_color;
	float4							m_misc;
	float2							m_tbounds;
	uint							m_stackDepth;
	uint							m_stackEntry;
	uint							m_traversalValue;
	int								m_objectIndex;
	int								m_treeIndex;
};

struct SrtData
{
	RWStructuredBuffer<Ray>				m_rayBuffer;
	RWStructuredBuffer<HitResult>		m_hitBuffer;
	RWTexture2D<float3>					m_dest;

	PtGlobals*							m_globals;
	PtDebug*							m_debug;
	PtLevel*							m_level;

	uint2								m_pos;
	uint								m_bounceNum;
	uint								pad;
	RWStructuredBuffer<PtPickerData>	m_outputVal;
};

static uint g_castShadows;
static uint g_useShadowRay;
static float g_shadowOffset;
static float3 g_rayPos;
static float3 g_rayDir;
static uint g_bounceNum;
static float4 g_skyColor;
static uint g_defaultLighting;

// ----------------------------------------------------------------------------
uint g_wseed;
uint g_seed0;
uint g_seed1;

static uint WangHash()
{
	uint seed = g_wseed;

    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);

	g_wseed = seed;
    return seed;
}

static void InitRandom(uint2 seed, PtGlobals gl)
{
	g_wseed = seed.x * (uint)gl.m_screenSize.x + seed.y + gl.m_timerCount;

	g_seed0 = WangHash();
	g_seed1 = WangHash();
}

static uint GetRandom() 
{
	/* hash the seeds using bitwise AND operations and bitshifts */
	g_seed0 = 36969 * (g_seed0 & 65535) + (g_seed0 >> 16);
	g_seed1 = 18000 * (g_seed1 & 65535) + (g_seed1 >> 16);

	unsigned int ires = (g_seed0 << 16) + g_seed1;
	return ires;
}

static float GetRandomFloat() 
{
	/* hash the seeds using bitwise AND operations and bitshifts */
	g_seed0 = 36969 * (g_seed0 & 65535) + (g_seed0 >> 16);
	g_seed1 = 18000 * (g_seed1 & 65535) + (g_seed1 >> 16);

	unsigned int ires = (g_seed0 << 16) + g_seed1;
	return GetRandom() / (float)(0xffffffff);
}

// ----------------------------------------------------------------------------
float3 CalcTriPos(uint3 tri, DataBuffer<float4> pos, float2 uv)
{
	float3	v0 = pos[tri[0]].xyz;
	float3	v1 = pos[tri[1]].xyz;
	float3	v2 = pos[tri[2]].xyz;

	float	w = 1.f - uv.x - uv.y;

	return w * v0 + uv.x * v1 + uv.y * v2;
}

float3 CalcTriNormal(uint3 tri, DataBuffer<float4> norm, float2 uv)
{
	float3	v0 = norm[tri[0]].xyz;
	float3	v1 = norm[tri[1]].xyz;
	float3	v2 = norm[tri[2]].xyz;

	float	w = 1.f - uv.x - uv.y;

	return w * v0 + uv.x * v1 + uv.y * v2;
}

struct MeshAttribs
{
	bool	m_valid;
	float3	m_color;
	float3	m_pos;
	float3	m_norm;
	float2	m_uv;
};

MeshAttribs		InitMeshAttribs()
{
	MeshAttribs retval;

	retval.m_valid = false;
	retval.m_color = g_skyColor;
	retval.m_pos = float3(0.f, 0.f, 0.f);
	retval.m_norm = float3(0.f, 0.f, 0.f);

	return retval;
}

MeshAttribs		CalcMeshAttribs(HitResult hres, Ray ray, PtLevel lv)
{
	MeshAttribs	a = InitMeshAttribs();
	
	if (hres.iInst == -1)
		return a;

	if (hres.isLight)
	{
		PtLight light = lv.m_lights[hres.iInst];
		a.m_color = light.m_color;
	}
	else
	{
		PtInstance inst = lv.m_inst[hres.iInst];
		uint iTri = hres.iTri;

		ulong exec = __s_read_exec();		
		do
		{
			// Pick the first active lane index
			uint firstActiveLane = __s_ff1_i32_b64(exec);

			// Get the tile index in that lnae
			int iMeshLane = __v_readlane_b32((int)hres.iMesh, firstActiveLane);

			// Create a mask for all lanes that occur in that tile
			ulong laneMask = __v_cmp_eq_u32((int)hres.iMesh, iMeshLane);

			// Only execute the code on this tile index
			if (__v_cndmask_b32(0, 1, laneMask))
			{
				if (iMeshLane == -1)
				{
					// calc sphere info
					Ray lr = ray;
					lr.pos = mul(float4(ray.pos, 1.f), inst.m_worldToObj).xyz;
					lr.dir = mul(float4(ray.dir, 0.f), inst.m_worldToObj).xyz;

					a.m_pos = lr.pos + hres.tuv.x * lr.dir;
					a.m_norm = a.m_pos;
				}
				else
				{
					PtMesh mesh = lv.m_meshArray->m_meshes[iMeshLane];
					uint3 tri = uint3(mesh.m_idx[iTri+0], mesh.m_idx[iTri+1], mesh.m_idx[iTri+2]);

					a.m_pos = CalcTriPos(tri, mesh.m_pos, hres.tuv.yz);
					a.m_norm = CalcTriNormal(tri, mesh.m_norm, hres.tuv.yz);
				}
			}

			// Since we are done with this lane (i.e this tile index), update the execution mask
			exec &= ~laneMask;

		// When all lanes are processed, exec will be zero and we can exit
		} while (exec != 0);

		a.m_valid = true;
		a.m_pos = mul(float4(a.m_pos, 1.f), inst.m_objToWorld).xyz;
		a.m_norm = normalize(mul(float4(a.m_norm, 0.f), inst.m_objToWorld).xyz);
	}
	
	// Some instance matrices have zero scales in them that produce 0 length normals...
	// this becomes a NaN when you normalize. Substitute a proper normal in that case.
	a.m_norm = isnan(a.m_norm) ? float3(0.f, 1.f, 0.f) : a.m_norm;

	return a;
}

// ----------------------------------------------------------------------------
float2 ToUnitDisk(float2 sq)
{
	// adapted from mitsuba code and
	// http://psgraphics.blogspot.ch/2011/01/improved-code-for-concentric-map.html

	float a = 2.f*sq.x - 1.f;
	float b = 2.f*sq.y - 1.f;

	float phi, r;

	if (a > -b)
	{
		if (a > b)
		{
			r = a;
			phi = (kPi / 4.f) * (b / a);
		}
		else
		{
			r = b;
			phi = (kPi / 4.f) * (2.f - a / b);
		}
	}
	else
	{
		if (a < b)
		{
			r = -a;
			phi = (kPi / 4.f) * (4.f + b / a);
		}
		else
		{
			r = -b;
			if (b != 0.f)
			{
				phi = (kPi / 4.f) * (6.f - a / b);
			}
			else
			{
				phi = 0.f;
			}
		}
	}

	return float2(r * cos(phi), r * sin(phi));
}

float3 RandomDiffuseVec(float3 norm, inout float pdf)
{
	// compute local orthonormal basis uvw at hitpoint to use for calculation random ray direction 
	// first vector = normal at hitpoint, second vector is orthogonal to first, third vector is orthogonal to first two vectors
	float3 w = norm;
	float3 u = normalize(cross((abs(w.x) > .1 ? float3(0.f,1.f,0.f) : float3(1.f, 0.f, 0.f)), w));
	float3 v = cross(w, u);

#if 0
	// adapted from http://raytracey.blogspot.com/2015/10/gpu-path-tracing-tutorial-1-drawing.html

	float r1 = 2.f * kPi * GetRandomFloat();
	float r2 = GetRandomFloat();
	float r2s = sqrt(r2);

	// compute random ray direction on hemisphere using polar coordinates
	// cosine weighted importance sampling (favours ray directions closer to normal direction)

	float3 rvec = float3(cos(r1)*r2s, sin(r1)*r2s, sqrt(1.f - r2));

	pdf = cos(rvec.z) / kPi;
#elif 1
	float2	d2 = ToUnitDisk(float2(GetRandomFloat(), GetRandomFloat()));
	float	z = sqrt(1.f - d2.x*d2.x - d2.y*d2.y);

	float3 rvec = float3(d2, z);

	pdf = cos(rvec.z) / kPi;
#else
	float u1 = GetRandomFloat();
	float u2 = GetRandomFloat();

    float r = sqrt(u1);
    float theta = 2 * kPi * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);

	float3 rvec = float3(x, y, sqrt(max(0.f, 1 - u1)));
	pdf = 1.f / (2.f * kPi);
#endif
	float3	d = normalize(u * rvec.x + v * rvec.y + w * rvec.z);
	return d;
}

Ray	CalcBounce(MeshAttribs attr, Ray ray)
{
	// Diffuse bounce
	Ray retval;
	float pdf;

	retval.dir = RandomDiffuseVec(attr.m_norm, pdf);

	//retval.dir = float3(0.f, 1.f, 0.f);
	retval.pos = attr.m_pos + kPathTracerEpsilon * retval.dir;
	//retval.attenuation = ray.attenuation * dot(attr.m_norm, retval.dir) * invpdf / kPi;
	//retval.attenuation = ray.attenuation * dot(attr.m_norm, retval.dir) / kPi * pdf;
	retval.attenuation = ray.attenuation * dot(attr.m_norm, retval.dir) / (kPi * pdf);
	retval.shadowRay = ray.shadowRay;
	retval.killed = false;

	return retval;
}

// ----------------------------------------------------------------------------
float3	CalcLighting(MeshAttribs attr, Ray ray, PtLevel lv)
{
	float3 color = float3(0.f, 0.f, 0.f);

	float3 dir;
	float4 lcolor;
	float4 lpos;

	dir = normalize(float3(1.f, -0.7f, 0.5f));
	//dir = normalize(float3(0.1f, -1.f, 0.05f));
	//dir = normalize(float3(0.f, -1.f, 0.f));
	lcolor = float4(1.f, 0.f, 0.f, 0.f);
	lpos = float4(attr.m_pos - 1.f * dir, 1.f);

	bool shadowed = false;
	bool useShadowRay = true;
	bool doShadow = true;
	float shadowOffset = kPathTracerEpsilon;
#ifdef DEBUG
	useShadowRay = g_useShadowRay;
	doShadow = g_castShadows;
	shadowOffset = g_shadowOffset;
#endif
#ifndef NOSHADOW
	if (doShadow)
	{
		// cast ray to light
		Ray sray;
		sray.pos = attr.m_pos - shadowOffset * dir;
		sray.dir = -dir;
		sray.shadowRay = useShadowRay;

		g_rayPos = sray.pos;
		g_rayDir = sray.dir;

		g_numNode = 0;
		g_numLeaf = 0;
		g_numTri = 0;
		g_kdTraversalIdx = 0;
		HitResult hres = RayVsScene(sray, lv, !g_defaultLighting);
		shadowed = (hres.tuv.x < kMaxT);
	}
#endif

	if (!shadowed)
	{
		// calculate lighting
		float3	v = attr.m_pos - lpos;
		//float	dist = (1.f / dot(v, v));
		float	dist = 1.f;

		float3	ldir = normalize(v);
		float	ldotn = saturate(dot(-ldir, attr.m_norm));
		color = ray.attenuation * ldotn * lcolor.xyz * dist;
	}

	return color;
}

// ----------------------------------------------------------------------------
float3	TestRandom()
{
	float f = GetRandomFloat();

	if (f < 0.33f)
		return float3(1.f, 0.f, 0.f);
	else if (f < 0.66f)
		return float3(0.f, 1.f, 0.f);
	else
		return float3(0.f, 0.f, 1.f);
}

float3	CalcDebug(MeshAttribs attr, HitResult hres, Ray ray, PtDebug db)
{
	uint debugMode = db.m_debugMode;
	float3	color = float3(0.1f, 0.1f, 0.1f);

	if (debugMode > kDebugNone)
	{
		switch (debugMode)
		{
			case kDebugNodes:
				color = GetHeatmapColor((float)g_numNode / db.m_nodeScale);
				break;
			case kDebugLeafs:
				color = GetHeatmapColor((float)g_numLeaf / db.m_leafScale);
				break;
			case kDebugSphere:
				color = GetHeatmapColor((float)g_numSph / db.m_sphereScale);
				break;
			case kDebugInst:
				color = GetHeatmapColor((float)g_numInst / db.m_instScale);
				break;
			case kDebugTri:
				color = GetHeatmapColor((float)g_numTri / db.m_triScale);
				break;
			case kDebugNormals:
				color = (attr.m_valid) ? 0.5f + 0.5f*attr.m_norm : 0.1f;
				break;
			case kDebugRayDir:
				color = (ray.killed) ? 0.f : 0.5f + 0.5f*ray.dir;
				break;
			case kDebugTValue:
				color = GetHeatmapColor((ray.killed) ? 0.f : hres.tuv.x / db.m_debugScale);
				break;
			case kDebugDebug:
				//color = GetHeatmapColor((float)g_debugCount / db.m_debugScale);
				//color = GetHeatmapColor(GetRandom());
				//color = GetRandomFloat();
				color = TestRandom();
				break;
		}
	}

	return color;
}

// ----------------------------------------------------------------------------
float3	ShadeRay_Old(MeshAttribs attr, inout Ray ray, PtLevel lv, PtDebug db)
{
	if (!attr.m_valid)
		return attr.m_color;

	// calculate normal
	float3 color = CalcLighting(attr, ray, lv);
	ray.killed = true; // one pass the old way

	return color;
}

// ----------------------------------------------------------------------------
float3	ShadeRay(MeshAttribs attr, inout Ray ray, PtLevel lv, PtDebug db)
{
	if (!attr.m_valid)
	{
		ray.killed = true;
		return attr.m_color * ray.attenuation;
	}

	ray = CalcBounce(attr, ray);
	return float3(0.f, 0.f, 0.f);
}

// ----------------------------------------------------------------------------
float3	WorldFromScreen(uint2 screenCoord, PtGlobals gl)
{
	float2 ndc = ((float2)screenCoord + float2(0.5f, 0.5f)) * gl.m_screenSize.zw;
	ndc.x = ndc.x * 2.f - 1.f;
	ndc.y = (1 - ndc.y) * 2.f - 1.f;

	float4 positionWS = mul(float4(ndc, 0.f, 1.f), gl.m_screenToWorldMat);
	positionWS.xyz /= positionWS.w;
	return positionWS.xyz;
}

Ray	GenerateRay(uint2 screenCoord, PtGlobals gl)
{
	float3 posWS = WorldFromScreen(screenCoord, gl);

	Ray ray;
	ray.pos = gl.m_cameraWS.xyz;
	ray.dir = normalize(posWS - ray.pos);
	ray.attenuation = float3(1.f, 1.f, 1.f);
	ray.shadowRay = false;
	ray.killed = false;

	return ray;
}

// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void Cs_PathTracer(uint2 dispatchThreadId : SV_DispatchThreadId,
				   uint groupIdx : SV_GroupIndex,
				   SrtData* pSrt : S_SRT_DATA)
{
	PtGlobals	gl = *pSrt->m_globals;
	PtDebug		db = *pSrt->m_debug;
	PtLevel		lv = *pSrt->m_level;

	uint2	scale = gl.m_pixelScaleOffset.xy;
	uint2	offset = gl.m_pixelScaleOffset.zw;
	uint2	pixel = dispatchThreadId.xy * scale + offset;

	InitRandom(pixel, gl);

	InitRaycast(groupIdx, db.m_accelType, db.m_useMailbox, db.m_kdStartPrim, db.m_kdEndPrim);
	InitKdTree(groupIdx, db.m_traversalMode, db.m_testMode, db.m_kdTreeLevel, db.m_kdTraversalIndex, db.m_kdStackDepth);

	g_castShadows = db.m_castShadows;
	g_useShadowRay = db.m_useShadowRay;
	g_shadowOffset = db.m_shadowOffset;
	g_rayPos = float3(0.f, 0.f, 0.f);
	g_rayDir = float3(0.f, 0.f, 0.f);
	g_skyColor = gl.m_skyColor;
	g_defaultLighting = db.m_defaultLighting;

	// generate ray
	Ray ray = GenerateRay(pixel, gl);

	// cast ray
	HitResult hres = RayVsScene(ray, lv, !g_defaultLighting);
	MeshAttribs attr = CalcMeshAttribs(hres, ray, lv);

	// shade result

	float3 color = float3(0.f, 0.f, 0.f);
#ifdef DEBUG
	if (db.m_debugMode == kDebugNone)
	{
		color = ShadeRay_Old(attr, ray, lv, db);
		if (ray.killed)
			pSrt->m_dest[pixel] += color;
	}
	else
	{
		color = CalcDebug(attr, hres, ray, db);
		pSrt->m_dest[pixel] += color;
	}
#else
	color = ShadeRay_Old(attr, ray, lv, db);
	if (ray.killed)
		pSrt->m_dest[pixel] += color;
#endif

#ifdef DEBUG
	if (all(pixel == pSrt->m_pos))
	{
		pSrt->m_outputVal[0].m_rayPos = float4(g_rayPos, 1.f);
		pSrt->m_outputVal[0].m_rayDir = float4(g_rayDir, 1.f);
		pSrt->m_outputVal[0].m_color = float4(color, 1.f);
		pSrt->m_outputVal[0].m_misc = g_misc;
		pSrt->m_outputVal[0].m_tbounds = g_tboundsOut;
		pSrt->m_outputVal[0].m_stackDepth = g_kdStackDepthOut;
		pSrt->m_outputVal[0].m_stackEntry = g_kdStackEntryOut;
		pSrt->m_outputVal[0].m_traversalValue = g_kdTraversalValue;
		pSrt->m_outputVal[0].m_objectIndex = g_objectIndex;
		pSrt->m_outputVal[0].m_treeIndex = g_treeIndex;
	}
#endif
}

// ----------------------------------------------------------------------------

enum Stage
{
	kStageGenerate,
	kStageCast,
	kStageShade,
};

bool ShouldDebug(Stage s, PtDebug db)
{
#ifdef DEBUG
	uint selBounce = (db.m_debugMode == kDebugNormals) ? 0 : db.m_selBounce;

	if (g_bounceNum != selBounce)
		return false;

	uint debugMode = db.m_debugMode;
	switch (debugMode)
	{
		case kDebugNodes:
		case kDebugLeafs:
		case kDebugSphere:
		case kDebugInst:
		case kDebugTri:
		case kDebugRayDir:
		case kDebugTValue:
			return s == kStageCast;

		case kDebugNormals:
		case kDebugDebug:
			return s == kStageShade;

		case kDebugNone:
		default:
			return false;
	}
#else
	return false;
#endif
}

[numthreads(8, 8, 1)]
void Cs_PathTracer_GenerateRays(uint2 dispatchThreadId : SV_DispatchThreadId,
								uint groupIdx : SV_GroupIndex,
								SrtData* pSrt : S_SRT_DATA)
{
	PtGlobals gl = *pSrt->m_globals;
	PtDebug db = *pSrt->m_debug;

	uint2	scale = gl.m_pixelScaleOffset.xy;
	uint2	offset = gl.m_pixelScaleOffset.zw;
	uint2	pixel = dispatchThreadId.xy * scale + offset;
	uint	rayIdx = dispatchThreadId.x + dispatchThreadId.y * gl.m_numThreadsX;

	InitRandom(pixel, gl);

	g_numNode = 0;
	g_numLeaf = 0;
	g_numSph = 0;
	g_numInst = 0;
	g_numTri = 0;
#ifdef DEBUG
	g_bounceNum = pSrt->m_bounceNum;
#endif

	// generate ray
	Ray ray = GenerateRay(pixel, gl);

	if (ShouldDebug(kStageGenerate, db))
	{
		float3 color = CalcDebug(InitMeshAttribs(), InitHitResult(), ray, db);
		pSrt->m_dest[pixel] += color;
	}

	pSrt->m_rayBuffer[rayIdx] = ray;
}

[numthreads(8, 8, 1)]
void Cs_PathTracer_Cast(uint2 dispatchThreadId : SV_DispatchThreadId,
						uint groupIdx : SV_GroupIndex,
						SrtData* pSrt : S_SRT_DATA)
{
	PtGlobals	gl = *pSrt->m_globals;
	PtDebug		db = *pSrt->m_debug;
	PtLevel		lv = *pSrt->m_level;

	uint2	scale = gl.m_pixelScaleOffset.xy;
	uint2	offset = gl.m_pixelScaleOffset.zw;
	uint2	pixel = dispatchThreadId.xy * scale + offset;
	uint	rayIdx = dispatchThreadId.x + dispatchThreadId.y * gl.m_numThreadsX;

	InitRandom(pixel, gl);
	InitRaycast(groupIdx, db.m_accelType, db.m_useMailbox, db.m_kdStartPrim, db.m_kdEndPrim);
	InitKdTree(groupIdx, db.m_traversalMode, db.m_testMode, db.m_kdTreeLevel, db.m_kdTraversalIndex, db.m_kdStackDepth);

	g_skyColor = gl.m_skyColor;
	g_defaultLighting = db.m_defaultLighting;
#ifdef DEBUG
	g_bounceNum = pSrt->m_bounceNum;
#endif

	// cast ray
	Ray		ray = pSrt->m_rayBuffer[rayIdx];

	if (!ray.killed)
	{
		HitResult hres = RayVsScene(ray, lv, !g_defaultLighting);

		if (ShouldDebug(kStageCast, db))
		{
			float3 color = CalcDebug(InitMeshAttribs(), hres, ray, db);
			pSrt->m_dest[pixel] += color;
		}

		pSrt->m_hitBuffer[rayIdx] = hres;

#ifdef DEBUG
		if (all(pixel == pSrt->m_pos))
		{
			pSrt->m_outputVal[0].m_tbounds = g_tboundsOut;
			pSrt->m_outputVal[0].m_stackDepth = g_kdStackDepthOut;
			pSrt->m_outputVal[0].m_stackEntry = g_kdStackEntryOut;
			pSrt->m_outputVal[0].m_traversalValue = g_kdTraversalValue;
			pSrt->m_outputVal[0].m_objectIndex = g_objectIndex;
			pSrt->m_outputVal[0].m_treeIndex = g_treeIndex;
		}
#endif
	}
}

[numthreads(8, 8, 1)]
void Cs_PathTracer_Shade(uint2 dispatchThreadId : SV_DispatchThreadId,
						 uint groupIdx : SV_GroupIndex,
						 SrtData* pSrt : S_SRT_DATA)
{
	PtGlobals	gl = *pSrt->m_globals;
	PtDebug		db = *pSrt->m_debug;
	PtLevel		lv = *pSrt->m_level;

	uint2	scale = gl.m_pixelScaleOffset.xy;
	uint2	offset = gl.m_pixelScaleOffset.zw;
	uint2	pixel = dispatchThreadId.xy * scale + offset;
	uint	rayIdx = dispatchThreadId.x + dispatchThreadId.y * gl.m_numThreadsX;

	InitRandom(pixel, gl);

	g_castShadows = db.m_castShadows;
	g_useShadowRay = db.m_useShadowRay;
	g_shadowOffset = db.m_shadowOffset;
	g_rayPos = float3(0.f, 0.f, 0.f);
	g_rayDir = float3(0.f, 0.f, 0.f);
	g_skyColor = gl.m_skyColor;
	g_defaultLighting = db.m_defaultLighting;

	g_numNode = 0;
	g_numLeaf = 0;
	g_numSph = 0;
	g_numInst = 0;
	g_numTri = 0;
#ifdef DEBUG
	g_bounceNum = pSrt->m_bounceNum;
#endif

	// shade result
	HitResult	hres = pSrt->m_hitBuffer[rayIdx];
	Ray			ray = pSrt->m_rayBuffer[rayIdx];

	if (!ray.killed)
	{
		MeshAttribs attr = CalcMeshAttribs(hres, ray, lv);

		float3 color = ShadeRay(attr, ray, lv, db);
#ifdef DEBUG
		if (db.m_debugMode == kDebugNone)
		{
			if (ray.killed)
				pSrt->m_dest[pixel] += color;
		}
		else if (ShouldDebug(kStageShade, db))
		{
			color = CalcDebug(attr, hres, ray, db);
			pSrt->m_dest[pixel] += color;
		}
#else
		if (ray.killed)
			pSrt->m_dest[pixel] += color;
#endif
		// Write back ray with updated info
		pSrt->m_rayBuffer[rayIdx] = ray;

#ifdef DEBUG
		if (all(pixel == pSrt->m_pos))
		{
			pSrt->m_outputVal[0].m_rayPos = float4(g_rayPos, 1.f);
			pSrt->m_outputVal[0].m_rayDir = float4(g_rayDir, 1.f);
			pSrt->m_outputVal[0].m_color = float4(color, 1.f);
			pSrt->m_outputVal[0].m_misc = g_misc;
		}
#endif
	}
}
