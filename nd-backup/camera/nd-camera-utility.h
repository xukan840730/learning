/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/camera/camera-blend.h"

struct SimpleCastProbe;

// -------------------------------------------------------------------------------------------------

struct CameraRemapParams
{
public:
	CameraRemapParams()
		: m_fadeInSec(-1.f)
		, m_fadeOutSec(-1.f)
		, m_fadeInDist(-1.f)
		, m_fadeOutDist(-1.f)
		, m_requestPersistTime(-1.f)
		, m_priority(0)
		, m_allowFadeOutByDistCamControlWhenBlendLessThan70(false)
		, m_allowStack(false)
		, m_blendByScript(false)
		, m_autoGenValid(false)
	{}

	float m_fadeInSec;
	float m_fadeOutSec;
	float m_fadeInDist;
	float m_fadeOutDist;
	float m_requestPersistTime;
	I32 m_priority;
	bool m_allowFadeOutByDistCamControlWhenBlendLessThan70;
	bool m_allowStack;
	bool m_blendByScript; // blend value is controlled by script.

	DistCameraParams m_distParams;

	bool m_autoGenValid;
	AutoGenCamParams m_autoGenParams;
};

struct CameraRemapParamsEx : public CameraRemapParams
{
	CameraRemapParamsEx()
	{}

	CameraRemapParamsEx(const CameraRemapParams& params, bool instantBlend)
		: CameraRemapParams(params)
		, m_instantBlend(instantBlend)
	{}

	bool m_instantBlend = false;
};

bool IsUnitLength(Vector_arg v);
bool ClipProbeAgainstPlane(SimpleCastProbe& probe, const Plane& plane);
void DSphere(Sphere const& sphere, Color const& color, char const* txt = nullptr, float textScale = 1.f);
