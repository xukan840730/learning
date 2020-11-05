/**
 * \file nd-particle-tracker.h
 *
 * Defintion of particle tracker class.
 *
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_PARTICLE_TRACKER_H
#define ND_PARTICLE_TRACKER_H

#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/render/particle/particle-handle.h"
#include "gamelib/state-script/ss-action.h"
#include "ndlib/process/process-handles.h"
#include "ndlib/process/process.h"

class NdGameObject;
class ProcessSpawnInfo;
struct ParticleMeshAttachmentSpawnInfo;

namespace DC
{
	struct ParticleSpawnTable;
	struct ParticleSpawnTableEntry;
};

typedef ScriptPointer<DC::ParticleSpawnTable> EffectTableScriptPointer;

class NdParticleTracker : public Process
{
public:
	typedef Process ParentClass;

	STATE_DECLARE_OVERRIDE(Active);

	StringId64					m_type;
	StringId64					m_partName;
	NdGameObjectHandle			m_objHandle;
	StringId64					m_attachId;
	StringId64					m_jointId;
	StringId64					m_rootId;
	float						m_endFrame;
	U32F						m_refreshCounter;
	ParticleHandle				m_hParticle;
	ParticleHandle				*m_hParticles;
	MutableProcessHandle		m_eventHandle;
	Locator						m_currentLoc;
	ProcessHandle				m_hProcess;			// process we're tracking
	BoundFrame					m_boundFrame;
	bool						m_linger;
	U32							m_numParticles;
	float						m_elapsedTime;
	bool						m_bKillWhenPhysicalized;
	bool						m_bObjHandleValid;
	bool						m_bPlayAtOrigin;
	bool						m_visibilityBeforePhotoModeSet : 1;
	bool						m_visibilityBeforePhotoMode : 1;

	EffectControlSpawnInfo		m_spawnInfo;
	EffectTableScriptPointer m_effTableScriptPointer;

	// Polymorphic means of finding out if the tracker is done tracking.
	// The TrackerIsDoneFunc only used by cutscene system right now.
	// The controlling process and callback data are used by state scripts.
	SsAction					m_ssAction;

public:
	inline const ParticleHandle& GetParticle() const	{ return m_hParticle; }

	virtual	void EventHandler(Event& event) override;
	virtual void ProcessDispatchError(const char* strMsg) override;
	const NdGameObject* GetOwner() const				{ return m_objHandle.ToProcess(); }
	void ChangeOwner(NdGameObject* pNewOwner);

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;
	void SetFloat(StringId64 rootName, float rootVal);

	static bool GetCurrentLocator(Locator* locOut, const NdGameObject *pGameObj, const BoundFrame &boundFrame, const DC::ParticleSpawnTableEntry *pEntry);
protected:
	virtual void OnKillProcess() override;

	void KillParticle();
	void SpawnParticleEffect(const Locator& loc, ParticleMeshAttachmentSpawnInfo *pMeshAttach = nullptr);
	void SpawnParticleTable();
	virtual void SpawnParticleInternal(BoundFrame& bf, ParticleMeshAttachmentSpawnInfo *pMeshAttach = nullptr);
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	void UpdateParticle();
	void UpdateParticleTable();
	bool GetCurrentLocator (Locator* locOut);
	U32F GetMaxStateAllocSize() override;
};

PROCESS_DECLARE(NdParticleTracker);


FWD_DECL_PROCESS_HANDLE(NdParticleTracker);


class NdParticleTracker::Active : public State
{
public:
	BIND_TO_PROCESS(NdParticleTracker);

	virtual void EventHandler(Event& event) override;
	virtual void Update() override;
};

#endif // #ifndef ND_PARTICLE_TRACKER_H
