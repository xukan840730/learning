/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#if ENABLE_NAV_LEDGES

#include "ndlib/anim/ik/ik-defs.h"

#include "gamelib/gameplay/ai/controller/nd-climb-controller.h"

 /// --------------------------------------------------------------------------------------------------------------- ///
class IAiClimbController : public INdAiClimbController
{
	typedef INdAiClimbController ParentClass;

public:

	struct IkTargetInfo
	{
		IkTargetInfo()
		{
			for (U32F ii = 0; ii < kArmCount; ++ii)
			{
				m_handEdgeVert0Ws[ii]        = kZero;
				m_handEdgeVert1Ws[ii]        = kZero;
				m_handEdgeWallNormalWs[ii]   = kUnitZAxis;
				m_handEdgeWallBinormalWs[ii] = kUnitZAxis;
				m_handIkStrength[ii]         = 1.0f;
			}

			for (U32F ii = 0; ii < kLegCount; ++ii)
			{
				m_footWallPosWs[ii]    = kZero;
				m_footWallNormalWs[ii] = kUnitZAxis;
				m_footIkStrength[ii]   = 0.0f;
			}
		}

		Point  m_handEdgeVert0Ws[kArmCount];
		Point  m_handEdgeVert1Ws[kArmCount];
		Vector m_handEdgeWallNormalWs[kArmCount];
		Vector m_handEdgeWallBinormalWs[kArmCount];

		F32 m_handIkStrength[kArmCount];

		Point  m_footWallPosWs[kLegCount];
		Vector m_footWallNormalWs[kLegCount];

		F32 m_footIkStrength[kLegCount];
	};

	virtual void RequestIdle() { }
	virtual void BlendForceDefaultIdle(bool blend) { }
	virtual bool IsBlocked(Point_arg posWs, Point_arg nextPosWs, bool isSameEdge) const { return false; }

	virtual bool GetIkTargetInfo(IkTargetInfo* pTargetInfoOut) const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiClimbController* CreateAiClimbController();

#endif // ENABLE_NAV_LEDGES