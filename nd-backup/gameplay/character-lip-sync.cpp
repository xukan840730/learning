/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-lip-sync.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/netbridge/command.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process.h"
#include "ndlib/netbridge/redisrpc-server.h"

class AnimSimpleInstance;

static void Vox(const char* voxName)
{
	SendEventFrom(Process::GetContextProcess(), SID("request-vox"), EngineComponents::GetNdGameInfo()->GetPlayerGameObject(), StringToStringId64(voxName));
}
REGISTER_RPC_FUNC(void, Vox, (const char* voxName));

static void cmdVoxCallback(int argc, char** argv)
{
	if (argc == 2)
	{
		Vox(argv[1]);		
	}
}
static Command cmdVox("vox", &cmdVoxCallback);

F32 g_voiceEnergyMaxLevelTalk = 5.0f;
F32 g_voiceEnergyMaxSpeedBlend = 0.5f;
F32 g_voiceEnergyMaxSpeedBlendZero = 5.0f;

LipSync::LipSync()
{
	m_blendIntensity = 0.0f;
	m_prevIntensity = 0.0f;
}

void LipSync::Update(AnimControl* animControl, F32 intensity)
{
	if (!g_ndConfig.m_pNetInfo->IsNetActive())
		return;

	F32 percent = intensity;

	AnimSimpleLayer* pLayer = animControl->GetSimpleLayerById(SID("LipSync"));
	if (!pLayer)
	{
		pLayer = animControl->CreateSimpleLayer(SID("LipSync"), ndanim::kBlendSlerp, 2);
		ALWAYS_ASSERT(pLayer != nullptr);
	}

	F32 intensityTalk = intensity / g_voiceEnergyMaxLevelTalk;
	if (intensityTalk > 1.0f) intensityTalk = 1.0f;
	if (intensityTalk > 0)
	{
		AnimSimpleInstance* pInstance = pLayer->CurrentInstance();
		if (!pInstance)
		{
			pLayer->RequestFadeToAnim(SID("facial-talking-calm"));
		}
	}

	F32 delta = intensity - m_prevIntensity;

	if (intensity < 2.0f)
	{
		Seek(m_blendIntensity, 0.0, FromUPS(g_voiceEnergyMaxSpeedBlendZero));
	}
	else
	{
		Seek(m_blendIntensity, intensityTalk, FromUPS(g_voiceEnergyMaxSpeedBlend) * fabs(delta));
	}
	pLayer->Fade(m_blendIntensity, 0.0f);

	/*if (intensity > 0.01f)
	{
		MsgOut("EnergyLevel = %f %f\n", intensity, delta);
	}*/

	m_prevIntensity = intensity;
}
