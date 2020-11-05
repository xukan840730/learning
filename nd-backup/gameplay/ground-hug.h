/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef GROUND_HUG_H
#define GROUND_HUG_H

#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/render/particle/particle-handle.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/render/ngen/meshraycaster.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/util/tracker.h"
#include "ndlib/water/water-mgr.h"

class NdGameObject;
namespace DC {
struct GroundHugInfo;
}  // namespace DC
namespace DMENU {
class Menu;
}  // namespace DMENU

// ----------------------------------------------------------------------------
// GroundHugController
// ----------------------------------------------------------------------------
class GroundHugController
{
public:
	static const U32 kMaxWheelRayCasts = 4;

	enum WheelIndex
	{
		// first four wheels must appear in this order in the hug-ground-info
		kWheelFrontLeft,
		kWheelRearLeft,
		kWheelRearRight,
		kWheelFrontRight,
		kWheelAdditional1,
		kWheelAdditional2,
		kWheelAdditional3,
		kWheelAdditional4,
		kMaxNumWheels
	};

	struct Wheel
	{
		int					m_wheelIndex;
		Transform			m_wheelToLocal;
		Transform			m_localToWheel;
		I32					m_jointIndex;
		F32					m_rotationSense;	// +1.0f or -1.0f
		F32					m_angle_rad;
		U32					m_rayIndex;			// for async physics ray casts
		Point				m_contactPos;
		Vector				m_contactNormal;
		ParticleHandle		m_hParticle;
		StringId64			m_particleGroupId;
		bool				m_bTrackValid;
		bool				m_bOnGround;
		bool				m_bParticleSpawnFailed;
		bool				m_updateOffset;
		Pat					m_lastContactPat;
		int					m_splasherIndex;

		void				UpdateParticles(GroundHugController& pGroundHugController, const DC::GroundHugInfo* pGroundHugInfo);
	};


protected:
	MutableNdGameObjectHandle							m_hObject;

	ScriptPointer<DC::GroundHugInfo>	m_pGroundHugInfo;
	StringId64									m_groundHugId;

	Wheel										m_aWheel[kMaxNumWheels];
	U32											m_numWheels;
	F32											m_wheelRadius_m;

	U32											m_numGroundHugWheels;
	Vector										m_wheelRotateAxis;
	int											m_finalizeWheelXfmsCounter;

	float										m_ySpring;
	float										m_ySpringStationary;
	float										m_upSpring;
	float										m_upSpringStationary;

	U32											m_singleRayIndex;
	U32											m_singleRayAnimStateInstanceIndex;
	bool										m_useSingleRayAtLocator;
	bool										m_useSingleRayAtLocatorLastFrame;
	bool										m_hasUsedSingleRayAtLocator;

	bool										m_enableExtraWaterHeightProbes;
	bool										m_disabled;

	MeshProbe::SurfaceTypeResult								m_surfaceInfo;

public:
	GroundHugController(NdGameObject *pObject, StringId64 hugGroundId);

	void SetEnabled(bool enabled) { m_disabled = !enabled; }
	bool IsEnabled() const { return !m_disabled; }

	void SetUseSingleRayAtLocator(bool use, U32 animStateInstanceIndex) { m_useSingleRayAtLocator = use; m_singleRayAnimStateInstanceIndex = animStateInstanceIndex; }
	bool HasUsedSingleRayAtLocator() const { return m_hasUsedSingleRayAtLocator; }

	const NdGameObject* GetGameObject() const
	{
		return m_hObject.ToProcess();
	}

	NdGameObject* GetGameObject()
	{
		return m_hObject.ToMutableProcess();
	}

	const DC::GroundHugInfo* GetGroundHugInfo() const { return &(*m_pGroundHugInfo); }
	StringId64 GetGroundHugId() const { return m_groundHugId; }

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	virtual void OnKillProcess() = 0;

	int GetNumWheels() { return m_numWheels; }
	const Wheel& GetWheel(int i) { return m_aWheel[i]; }

	F32 GetFrontWheelZ();
	F32 GetRearWheelZ();

	void InitWheelTransforms();
	void InitWheels();

	virtual void UpdateSurfaceInfo(const MeshProbe::SurfaceTypeResult& surfaceInfo) { m_surfaceInfo = surfaceInfo; }
	virtual void UpdateWheels();

	virtual bool GroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug) { return false; }

	virtual int ContactCount() const { return 0; }
	virtual U32F GetRayCastCount() { return 0; }
	virtual U32F GetSphereCastCount() { return 0; }
	virtual void PopulateRayCastJobNoLock(int * pRayIndex, Locator objectLoc) {}
	virtual void PopulateSphereCastJobNoLock(int * pSphereIndex, Locator objectLoc) {}

	virtual const Vector OldUp() const { return kUnitYAxis; }
	virtual const Vector NewUp() const { return kUnitYAxis; }

	virtual const Vector GetRayDir() const { return -Vector(kUnitYAxis); }

	virtual void SetSpringConstants(float ySpring, float upSpring, float ySpringStationary, float upSpringStationary)
	{
		if (ySpring >= 0.0f)			m_ySpring = ySpring;
		if (upSpring >= 0.0f)			m_upSpring = upSpring;
		if (ySpringStationary >= 0.0f)	m_ySpringStationary = ySpringStationary;
		if (upSpringStationary >= 0.0f)	m_upSpringStationary = upSpringStationary;
	}

	virtual void SetExtraWaterHeightProbes(bool enable) { m_enableExtraWaterHeightProbes = enable; }

	static GroundHugController* CreateGroundHugController(NdGameObject *pObject, StringId64 groundHugId);

	F32 GetWheelRadius_m() const { return m_wheelRadius_m; }

	Pat GetPat(int wheelIndex) const { return m_aWheel[wheelIndex].m_lastContactPat; }

	virtual void Reset(const Vector* pNewUp = nullptr) {};

protected:
	float GetObjectSpeed();
};

// ----------------------------------------------------------------------------
// SolidGroundHugController - Ground hug controller for land based vehicles
// ----------------------------------------------------------------------------
class SolidGroundHugController : public GroundHugController
{
public:
	struct Contact
	{
		Point				m_position;
		Vector				m_normal;
		U32					m_iWheel;
		const NdGameObject*		m_pDebugGo;
	};

private:
	Vector					m_groundHugRayDir;

	SpringTracker<Vector>	m_groundHugUpSpring;	// for smoothing out vehicle "up" for ground hug
	SpringTracker<F32>		m_groundHugYSpring;
	Vector					m_lastGoodNewUp;

	bool					m_bGroundHugRaysCast;

	I32						m_contactCount;
	Vector					m_oldUp;
	Vector					m_newUp;

	bool SphereGroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug); //called by groundHug if useSphereProbes is enabled

public:
	SolidGroundHugController(NdGameObject *pObject, StringId64 hugGroundId);

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;

	virtual bool GroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug) override;
	void CalcVehicleUpVector_1(Vector & newVehicleUp, Point & pointOnPlane, Contact aContact[], Vector_arg railUp);
	void CalcVehicleUpVector_2(Vector & newVehicleUp, Point & pointOnPlane, Contact aContact[], Vector_arg railFront, Vector_arg railLeft, Vector_arg railUp);
	void CalcVehicleUpVector_3(Vector & newVehicleUp, Point & pointOnPlane, Contact aContact[], Vector_arg railUp, U32 i0 = 0, U32 i1 = 1, U32 i2 = 2);
	void CalcVehicleUpVector_4(Vector & newVehicleUp, Point & pointOnPlane, Contact aContact[], Vector_arg railUp);

	virtual U32F GetRayCastCount() override;
	virtual U32F GetSphereCastCount() override;
	virtual void PopulateRayCastJobNoLock(int * pRayIndex, Locator objectLoc) override;
	virtual void PopulateSphereCastJobNoLock(int * pSphereIndex, Locator objectLoc) override;

	virtual int ContactCount() const override	{ return m_contactCount; }
	virtual const Vector OldUp() const override	{ return m_oldUp; }
	virtual const Vector NewUp() const override	{ return m_newUp; }

	virtual const Vector GetRayDir() const override { return m_groundHugRayDir; }

	virtual void Reset(const Vector* pNewUp = nullptr) override;
};

// ----------------------------------------------------------------------------
// WaterDisplacementHugController - Hug ground controller for displacement water based vehicles
// ----------------------------------------------------------------------------
class WaterDisplacementHugController : public GroundHugController
{
private:
	static const U32 kMaxAdditionalRayCasts = 8;

	WaterMgr::DisplacementQueryInfo	m_waterDisplacementIndex;

	Vec3							m_wheelPos[kMaxWheelRayCasts+kMaxAdditionalRayCasts];
	Vec3							m_queryPoints[kMaxWheelRayCasts+kMaxAdditionalRayCasts+1];
	Vec3							m_queryOffsets[kMaxWheelRayCasts+kMaxAdditionalRayCasts];

	bool							m_initialized;

	int								m_additionalHeightContacts;
	int								m_numQueries;

	SpringTracker<Vector>			m_groundHugUpSpring;	// for smoothing out vehicle "up" for ground hug
	SpringTracker<F32>				m_groundHugYSpring;

	float							m_oldPosY;
	float							m_newPosY;
	Vector							m_oldUp;
	Vector							m_newUp;

	float							m_debugOldVelY;
	float							m_debugMaxAccel;

	Vector							m_localDisplacementXZ;

public:
	WaterDisplacementHugController(NdGameObject *pObject, StringId64 hugGroundId);

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;

	virtual bool GroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug) override;

	virtual const Vector OldUp() const override { return m_oldUp; }
	virtual const Vector NewUp() const override { return m_newUp; }

	virtual void Reset(const Vector* pNewUp = nullptr) override;
};

// ----------------------------------------------------------------------------
// GroundHugManager
// ----------------------------------------------------------------------------
class GroundHugManager
{
public:
	GroundHugManager();

	void AddGroundHugger(GroundHugController *pHGC);
	void RemoveGroundHugger(GroundHugController *pHGC);
	void Relocate(GroundHugController *pHGC, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	RayCastJob & GetRayCastJob();
	SphereCastJob & GetSphereCastJob();

	void PostRenderUpdate();			// Kicks the ground casts... call this after object position has been updated this frame if possible
	void SetUpCollCasts();

	NdAtomicLock			m_hugGroundLock;

private:
	static const int kMaxGroundHuggers = 64;

	GroundHugController *	m_aGroundHugControllers[kMaxGroundHuggers];
	int						m_numGroundHuggers;

	RayCastJob				m_rayCastJob;
	SphereCastJob           m_sphereCastJob;
};

extern GroundHugManager g_hugGroundMgr;

// ----------------------------------------------------------------------------
// GroundHugOptions
// ----------------------------------------------------------------------------

struct GroundHugOptions
{
	F32 m_speedThresh;
	F32 m_minSpeedForTireParticles_mps;
	F32 m_wheelRotationSpeedScale;
	F32 m_upSpringOverride;
	F32 m_upSpringStationaryOverride;
	F32 m_ySpringOverride;
	F32 m_ySpringStationaryOverride;

	bool m_disable; // disable all ground hugging
	bool m_disableWheelParticles;
	bool m_debugWheelParticles;
	bool m_hugToSnow;
	bool m_hugToWater;
	bool m_disableProceduralWheels;

	bool m_drawContacts;
	bool m_drawContactsDetailed;
	bool m_drawContactsOnTop;
	bool m_drawWheels;
	bool m_drawWheelsPreHug;
	bool m_drawRays;
	bool m_drawSmoothedUpDir;

	GroundHugOptions();
	void PopulateDevMenu(DMENU::Menu* pMenu);
};

extern GroundHugOptions g_groundHugOptions;

#endif // #ifndef GROUND_HUG_H
