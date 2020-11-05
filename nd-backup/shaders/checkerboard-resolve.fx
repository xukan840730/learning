#pragma argument(target=neo)
#pragma argument(realtypes)

/************************************************************************/
/* Checkerboard Resolve                                                 */
/************************************************************************/

// Changed cb resolve to use f32 because blacks were being crushed with f16 [MR]

//#define USE_HALFS
struct CheckerboardResolveSrt
{
	Texture2D<float3>		m_color;
	Texture2DMS<ushort, 2>	m_objectId;

	RWTexture2D<float3>		m_outColor;

	// When using temporal, alternate between 0 and 1
	ushort					m_frameId;
};

static const uint UP = 0;
static const uint LEFT = 1;
static const uint DOWN = 2;
static const uint RIGHT = 3;
static const uint SAMPLE_COUNT = 4;

#ifdef USE_HALFS
#define FLOAT half
#define FLOAT3 half3
#else
#define FLOAT float
#define FLOAT3 float3
#endif


FLOAT3 rgb_to_ycocg(FLOAT3 col)
{
	return FLOAT3(
		 0.25 * col.r + 0.5 * col.g +  0.25 * col.b,
		 0.5  * col.r                + -0.5  * col.b,
        -0.25 * col.r + 0.5 * col.g + -0.25 * col.b);
}

FLOAT3 ycocg_to_rgb(FLOAT3 v)
{
	return FLOAT3(
		v.x  + v.y - v.z,
        v.x 	   + v.z,
        v.x  - v.y - v.z);
}

ushort id_load(ushort2 uv, CheckerboardResolveSrt *pSrt, bool bIgnore)
{
	ushort2 uv0 = ushort2(uv.x/2, uv.y);
	ushort frag = (uv.x + uv.y + pSrt->m_frameId) & 1;
	return bIgnore ? 0 : pSrt->m_objectId.Load(uv0, frag);
}

FLOAT get_colour_difference_blend(FLOAT3 a, FLOAT3 b)
{
    auto differential = a - b;
    auto len = sqrt(dot(differential,differential)); // TODO: See if we need the sqrt or not.
    return 1.0 / (len + 0.01); // [VM] 0.001 could easily cause the half precision colors to overflow when applying the weigth
}

FLOAT3 full_diff_blend(FLOAT3 colours[SAMPLE_COUNT], FLOAT weights[2])
{
	const auto vertical_weight = weights[0];
	const auto horizontal_weight = weights[1];

	return ((colours[UP] + colours[DOWN]) * vertical_weight + (colours[LEFT] + colours[RIGHT]) * horizontal_weight) / (2.0 * (vertical_weight + horizontal_weight));
}

// Copy-pasted from sample
void object_check(ushort obj_id, FLOAT3 colours[SAMPLE_COUNT], ushort ids[SAMPLE_COUNT], FLOAT weights[2], out FLOAT3 colour, out FLOAT colour_weight, out ushort colour_count)
{
	colour = 0;
	colour_weight = 0;
	colour_count = 0;

	bool    up_in_obj = ids[UP]    == obj_id;
	bool  down_in_obj = ids[DOWN]  == obj_id;
	bool  left_in_obj = ids[LEFT]  == obj_id;
	bool right_in_obj = ids[RIGHT] == obj_id;
	
	if(up_in_obj || down_in_obj)
	{
		auto weight = weights[0];
		if(up_in_obj)
		{
			colour += colours[UP] * weight;
			colour_weight += weight;
			++colour_count;
		}
		if(down_in_obj)
		{
			colour += colours[DOWN] * weight;
			colour_weight += weight;
			++colour_count;
		}
	}

	if(left_in_obj || right_in_obj)
	{
		auto weight = weights[1];
		if(left_in_obj)
		{
			colour += colours[LEFT] * weight;
			colour_weight += weight;
			++colour_count;
		}
		if(right_in_obj)
		{
			colour += colours[RIGHT] * weight;
			colour_weight += weight;
			++colour_count;
		}
	}
}

void id_bbox_check(ushort id, FLOAT3 bbox_min, FLOAT3 bbox_max, FLOAT3 colours[SAMPLE_COUNT], ushort ids[SAMPLE_COUNT], FLOAT weights[2], out FLOAT3 bbox_colours, out FLOAT bbox_colour_weight, out ushort bbox_colour_count)
{
	bbox_colours = 0;
	bbox_colour_weight = 0;
	bbox_colour_count = 0;

	bool up_in_bbox    = (id == ids[UP])    || (all(colours[UP]    >= bbox_min) && all(colours[UP]    <= bbox_max));
	bool down_in_bbox  = (id == ids[DOWN])  || (all(colours[DOWN]  >= bbox_min) && all(colours[DOWN]  <= bbox_max));
	
	if(up_in_bbox || down_in_bbox)
	{
		auto weight = weights[0];
		if(up_in_bbox)
		{
			bbox_colours += colours[UP] * weight;
			bbox_colour_weight += weight;
			++bbox_colour_count;
		}
		if(down_in_bbox)
		{
			bbox_colours += colours[DOWN] * weight;
			bbox_colour_weight += weight;
			++bbox_colour_count;
		}
	}

	bool left_in_bbox  = (id == ids[LEFT])  || (all(colours[LEFT]  >= bbox_min) && all(colours[LEFT]  <= bbox_max));
	bool right_in_bbox = (id == ids[RIGHT]) || (all(colours[RIGHT] >= bbox_min) && all(colours[RIGHT] <= bbox_max));

	if(left_in_bbox || right_in_bbox)
	{
		auto weight = weights[1];
		if(left_in_bbox)
		{
			bbox_colours += colours[LEFT] * weight;
			bbox_colour_weight += weight;
			++bbox_colour_count;
		}
		if(right_in_bbox)
		{
			bbox_colours += colours[RIGHT] * weight;
			bbox_colour_weight += weight;
			++bbox_colour_count;
		}
	}
}

void CheckerboardResolve(short2 dispatchId, bool bIgnoreObjId, CheckerboardResolveSrt *pSrt)
{
	/*
		+---+---+---+---+		   +---+---+
		| ● | ○ | ● | ○ |		   | ● | ● |
		+---+---+---+---+		   +---+---+
		| ○ | ● | ○ | ● |		   | ● | ● |
		+---+---+---+---+		   +---+---+
		| ● | ○ | ● | ○ |		   | ● | ● |
		+---+---+---+---+		   +---+---+
		| ○ | ● | ○ | ● |		   | ● | ● |
		+---+---+---+---+		   +---+---+
	*/

	// This shader is dispatched on the half-width buffer
	short ox = (dispatchId.y + pSrt->m_frameId) & 1;
	short2 filtered_pos = short2(dispatchId.x * 2 + 1 - ox, dispatchId.y);
	short2 passthru_pos = short2(dispatchId.x * 2 + ox, dispatchId.y);

	// Load the colors
	FLOAT3 colours[SAMPLE_COUNT] = 
	{
		rgb_to_ycocg(pSrt->m_color[dispatchId + short2(0, -1)]),
		rgb_to_ycocg(pSrt->m_color[dispatchId + short2(-ox, 0)]),
		rgb_to_ycocg(pSrt->m_color[dispatchId + short2(0, 1)]),
		rgb_to_ycocg(pSrt->m_color[dispatchId + short2(1 - ox, 0)]),
	};

	// Load the object ids
	const ushort sample_id = id_load(filtered_pos, pSrt, bIgnoreObjId);
	ushort ids[SAMPLE_COUNT] =
	{
		id_load(filtered_pos + short2( 0,-1), pSrt, bIgnoreObjId),
		id_load(filtered_pos + short2(-1, 0), pSrt, bIgnoreObjId),
		id_load(filtered_pos + short2( 0, 1), pSrt, bIgnoreObjId),
		id_load(filtered_pos + short2( 1, 0), pSrt, bIgnoreObjId)
	};

	// each min/max pair is the colour bounding box, built using all surrounding colour samples that are
	// either part of the same primitive, xor the same object.
	FLOAT3 prim_max = -10000.f;
	FLOAT3 prim_min =  10000.f;
	
	// count how many of the shading samples are of the same prim or the same object.
	ushort prim_neighbours = 0;

	for(short i = 0; i < SAMPLE_COUNT; ++i)
	{
		if(sample_id == ids[i])
		{
			prim_max = max(prim_max,colours[i]);
			prim_min = min(prim_min,colours[i]);
			++prim_neighbours;
		}
	}

	FLOAT3  bbox_colours = 0;
	FLOAT   bbox_colour_weight = 0;
	ushort bbox_colour_count = 0;

	FLOAT diff_weights[2] = 
	{
		ids[UP] != ids[DOWN] ? 1.7 : get_colour_difference_blend(colours[UP], colours[DOWN]),
		ids[LEFT] != ids[RIGHT] ? 1.7 : get_colour_difference_blend(colours[LEFT], colours[RIGHT])
	};

	id_bbox_check(sample_id, prim_min, prim_max, colours, ids, diff_weights, bbox_colours, bbox_colour_weight, bbox_colour_count);

	FLOAT3 filtered_col = 0;
	if(bbox_colour_count > 1)
	{
		filtered_col = bbox_colours / bbox_colour_weight;
	}
	else
	{
		auto prim_colour = bbox_colours / bbox_colour_weight;
		auto prim_colour_count = bbox_colour_count;
		const ushort obj_id = sample_id;
		object_check(obj_id, colours, ids, diff_weights, bbox_colours, bbox_colour_weight, bbox_colour_count);
		if(prim_colour_count == 1)
		{
			auto obj_colour = bbox_colours / bbox_colour_weight;
			auto colour_diff = prim_colour - obj_colour;
			auto dist = dot(colour_diff, colour_diff);
			auto scale = min(1.0, dist / 0.025);
			filtered_col = prim_colour * scale + obj_colour * (1.0 - scale);
		}
		else if(bbox_colour_count > 0)
		{
			filtered_col = bbox_colours / bbox_colour_weight;
		}
		else 
		{
			filtered_col = full_diff_blend(colours, diff_weights);
		}
	}

	FLOAT3 passthru_col = ox == 0 ? colours[LEFT] : colours[LEFT+2];

	FLOAT3 filtered_res = ycocg_to_rgb(filtered_col);
	FLOAT3 passthru_res = ycocg_to_rgb(passthru_col);

	pSrt->m_outColor[filtered_pos] = filtered_res;
	pSrt->m_outColor[passthru_pos] = passthru_res;
}

[numthreads(8, 8, 1)]
void Cs_CheckerboardResolve(short2 dispatchId : SV_DispatchThreadID, CheckerboardResolveSrt *pSrt : S_SRT_DATA)
{
	CheckerboardResolve(dispatchId, false, pSrt);
}

[numthreads(8, 8, 1)]
void Cs_CheckerboardResolveIgnoreObjId(short2 dispatchId : SV_DispatchThreadID, CheckerboardResolveSrt *pSrt : S_SRT_DATA)
{
	CheckerboardResolve(dispatchId, true, pSrt);
}
