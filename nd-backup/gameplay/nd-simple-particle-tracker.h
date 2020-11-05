/*
* Copyright (c)2008 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#ifndef ND_SIMPLE_PARTICLE_TRACKER_H
#define ND_SIMPLE_PARTICLE_TRACKER_H

#include "gamelib/render/particle/particle-handle.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/process/process.h"

class ProcessSpawnInfo;

//--------------------------------------------------------------------------------------
// Class NdSimpleParticleTracker
// Simple particle tracker that merely keeps a particle at the same local-space
// position and orientation that it was when it first hit a game object.  No fancy
// joint or attach system attachments going on, no sound tracking or anything else.
//--------------------------------------------------------------------------------------
class NdSimpleParticleTracker : public Process
{
private: typedef Process ParentClass;
public:

	struct SpawnData
	{
		SpawnData();
		StringId64				m_particleGroupId;
		Locator					m_particleLocator;
		NdGameObjectHandle	m_particleOwner;
		RigidBodyHandle			m_rigidBodyBind;
		int						m_saveType = 0;
		bool					m_inheritVelocity;
		bool					m_waitForRaycastResult;
	};

	NdSimpleParticleTracker() {}
	virtual ~NdSimpleParticleTracker() override;

	const char* GetPidString() const;
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void ProcessDispatchError(const char* strMsg) override;
	void ProcessUpdate() override;

	bool SpawnParticleFromInfo(SpawnData *pInfo);

	void SpawnParticleOnRaycastResult(float u, float v, StringId64 jointSid);

	inline const ParticleHandle& GetParticle() const	{ return m_hParticle; }
	U32F GetMaxStateAllocSize() override							{ return 0; } // stateless

	bool GetCurrentLocator(Locator* locOut);


private:
	ParticleHandle m_hParticle;
	BoundFrame m_frame;

	SpawnData m_cachedSpawnData;

	int m_waitingForRaycastResult; 
	bool m_firstFrame;
	
	bool m_inheritVelocity;
};

#endif //ND_SIMPLE_PARTICLE_TRACKER_H
