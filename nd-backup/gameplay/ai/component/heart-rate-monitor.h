/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _HEART_RATE_MONITOR_H_
#define _HEART_RATE_MONITOR_H_

#include "ndlib/script/script-manager.h"

#include "gamelib/scriptx/h/heart-rate-defines.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"

// -------------------------------------------------------------------------------------------------------
// Simulated Heart Rate
// -------------------------------------------------------------------------------------------------------

class Character;
class SfxProcess;
class ScreenSpaceTextPrinter;

class HeartRateMonitor

{

public:
	struct HeartRateUpdateParams
	{
		HeartRateUpdateParams() : m_inExertion(false), m_allowTrackDown(true), m_stateName(INVALID_STRING_ID_64), m_maxHeartRate(1.0f) {}

		StringId64 m_stateName;
		F32  m_maxHeartRate;
		bool m_inExertion;
		bool m_allowTrackDown;
	};

	void Init(StringId64 heartRateStatesListId, StringId64 dcNamespace);
	void Reset();
	void Update(Character* pOwner);
	void SetCcVars(SfxProcess* pSfx);

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);
	void SetBreathAnimTable(StringId64 tableId);
	void OverrideBreathAnimTableFrame(StringId64 tableId);

	void UpdateDc();

	void SetHeartRate(float hr, StringId64 heartRateStateId = SID("SCRIPT"))
	{
		m_heartRate		   = Limit01(hr);
		m_heartRateStateId = heartRateStateId;
	}

	// When exiting certain states (e.g. swimming, vertigo) we want to artificially create the ramp down effect.
	void ElevateHeartRateToTensionMax();

	const F32 GetCurrentHeartRate() const { return m_heartRate; }
	const F32 GetTargetHeartRate() const { return m_pHeartRateState ? m_pHeartRateState->m_targetValue : m_heartRate; }

	const DC::HeartRateState* GetCurrentHeartRateState() const { return m_pHeartRateState; }

	void OverrideHeartRateState(HeartRateUpdateParams updateParams) { m_heartRateUpdateOverride = updateParams; }
	void OverrideHeartRateTensionMode(U32 tension) { m_tensionModeOverride = tension; }

	void SuppressHeartRateGesturesOneFrame() { m_suppressGesturesOneFrame = true; }

	void DebugDraw(const Character* pOwner, ScreenSpaceTextPrinter* pPrinter) const;

	const DC::HeartRateStateList* GetHeartRateStates() const { return m_pHeartRateStates; }

	const DC::BreathAnim* GetCurrentBreathAnim() const { return m_pCurBreathAnim; }

	void HandleBreathStartEvent(Character* pOwner, F32 breathLength, int context);

private:
	void ComputeHeartRate(Character* pOwner);
	bool TryGesture(Character* pOwner);
	const DC::BreathAnim* GetValidBreathAnim(const Character* pOwner, bool* pIsScriptOverrideOut, bool debug = false) const;

private:
	float m_heartRate;
	float m_prevStateHeartRate;
	TimeFrame m_lastStateTime;

	HeartRateUpdateParams m_heartRateUpdateOverride;
	U32 m_tensionModeOverride;

	StringId64 m_heartRateStateId;
	StringId64 m_heartRateStateListId;
	StringId64 m_lastPlayedGesture;
	const DC::HeartRateState* m_pHeartRateState; // Points to static mem, don't need to relocate

	ScriptPointer<DC::HeartRateStateList> m_pHeartRateStates;
	ScriptPointer<DC::HeartRateGestureList> m_pGestures;
	ScriptPointer<DC::BreathAnimTable> m_pBreathAnims;


	StringId64 m_breathAnimTableOverride;
	ScriptPointer<DC::BreathAnimTable> m_pBreathAnimsOverride;
	const DC::BreathAnim* m_pCurBreathAnim;

	TimeFrame m_nextGestureTime;

	bool m_curBreathAnimFromScriptOverride;
	bool m_inExertion;
	bool m_suppressGesturesOneFrame;
};

#endif // _HEART_RATE_MONITOR_H_
