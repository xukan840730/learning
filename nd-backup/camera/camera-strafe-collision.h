/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/ndphys/simple-collision-cast.h"
#include "gamelib/camera/camera-arc.h"
#include "gamelib/gameplay/character.h"

#include "gamelib/scriptx/h/nd-camera-settings.h"

class CameraControlStrafeBase;

class StrafeCameraCollision
{
public:
	enum ProbeGroupIndex { kHorizontal, kVertical, kProbeGroupCount };
	enum SmallObjectProbes
	{
		kCameraToSafePos = 0,
		kSafePosToCamera,
		kSmallObjectProbesCount
	};
	struct ProbeGroup
	{
		I32				m_iStartProbe;
		I32				m_probeCount;
		F32				m_fProbeSpacing;
		F32				m_fCurrentArc;

		enum StickDir { kBeforeCenterProbe, kAfterCenterProbe, kStickFactorCount };
		float			m_stickFactor[kStickFactorCount];
		TwoWaySpringTracker<F32>	m_stickFactorSpring[kStickFactorCount];

		F32				m_pullInPercent;	// pull-in percent as calculated by this probe group
		F32				m_urgency;			// urgency of the pull-in, range [0, 1] -- i.e. how "close" to the actual camera is the collision (measured along the arc)
		F32				m_pullInDistMult;	// multiplier to affect how far away from wall before pull in starts (lower number means it has to get closer to wall before pull in)		

		struct ProbeDebug
		{
			F32			m_actualPullInPercent;
			F32			m_distOnlyPullInPercent;
			F32			m_pullInPercent;
			F32			m_normDistFromCenter;
		};
		ProbeDebug*		m_aProbeDebug;
	};

	enum VerticalSafeDir { kBothDirSafe, kUpDirSafe, kDownDirSafe };

	void OnCameraStart(const DC::CameraStrafeCollisionSettings* pSettings, const NdGameObject* pFocusObj, float nearPlaneDist);
	void InitState(float pullInPercent);
	void ResetNearDist(float nearDist);

	void KickProbes(const CameraControlStrafeBase* pCamera,
					CameraArc& verticalArc,			// vertical arc calculator
					float currentArc,				// current position along vertical arc
					const CollideFilter& collFilter,
					float nearPlaneHalfWidthWs,		// 1/2 width of near plane in world space
					float nearPlaneHalfHeightWs,	// 1/2 height of near plane in world space
					Vector cameraShakePosOffsetCs = Vector(0.0f, 0.0f, 0.0f));		// In camera space

	float ComputePullInPercent(CameraControlStrafeBase* pCamera, 
							   const DC::CameraStrafeCollisionSettings* pSettings,
							   float dt, 
							   bool bTopControl,
							   const float defaultNearDist, 
							   const CamWaterPlane& waterPlane, 
							   bool collideWaterPlane, 
							   bool ignoreCeilingProbe, 
							   bool photoMode);

	void GatherSmallObjectsJob();
	void CloseSmallObjectsJob();
	bool IsDebugDrawingSmallObjectsJob(const CameraControlStrafeBase *pCamera) const;
	bool IsClippingWithSmallObjects(const CameraControlStrafeBase *pCamera, float pullInPercent);
	float ComputePullInPercentToAvoidSmallObjects(float deltaTime);

	Point GetFinalSafePos() const;
	void OnKillProcess();
	void DebugDraw(const CameraControlStrafeBase* pCamera) const;
	//SimpleCastProbe *GetProbes(int &numHorizontal, int &numVertical, float &horizontalProbeFanHalfAngleDeg);
	//const SimpleCastProbe* GetProbes(ProbeGroupIndex groupIndex, I32* outNumProbes) const;
	void ResetPullInSpring(float newPullInPercent) { m_pullInSpring.Reset(); m_pullInPercent = newPullInPercent; }
	void UpdateVerticalDirection();
	void SetKeepInvalid() { m_keepInvalid = true; }

	void GatherBlockingCharacterProbe();
	bool IsCharacterBlockingCamera(const Character* pCharacter) const;

	void SetSafePointProbeIgnoreWater(bool f) { m_safePointProbeIgnoreWater = f; }
	void SetNearPlaneProbeIgnoreWater(bool f) { m_nearPlaneProbeIgnoreWater = f; }

	// CONSTANTS

	static const int kNumProbesCeiling = 1;
	static const int kNumProbesSafePoint = 1;
	static const int kNumProbesNearDistPoint = 1;
	static const int kNumProbesPhotoMode = 4;
	static const int kNumProbesOthers = kNumProbesCeiling + kNumProbesSafePoint + kNumProbesNearDistPoint + kNumProbesPhotoMode;

	static const int kNumProbesHoriz = 11;
	static const int kNumProbesVert = 17;
	static const int kNumProbesHoriVert = kNumProbesHoriz + kNumProbesVert;

	enum PhotModeProbeIndex
	{
		kProbePhotoModeXzLeft = 0,
		kProbePhotoModeXzRight,
		kProbePhotoModeMinY,
		kProbePhotoModeMaxY,
	};

	CharacterHandle			m_hCharacter;			// targetCharacter
	Point					m_cameraPosWs;			// camera position WITHOUT collision
	Point					m_targetPosWs;			// current target pos
	Point					m_horizontalPivotWs;	// point about which camera rotates horizontally
	Point					m_arcCenterWs;			// sideways ofset relative to horizontal pivot
	Point					m_safePosWs;			// safe pos for camera when in full collision
	Point					m_pushedInSafePosWs;	// pushed in safe pos to move to when backed against wall and safe pos is in collision
	Vector					m_cameraUpDirWs;		// camera "up" unit dir -- not always world-y!
	AttachIndex				m_neckAttachIndex;
	float					m_dampedNeckY;

	//PHOTOMODE BEGIN
	float					m_dcOffsetMinY; // initialized
	float					m_dcOffsetMaxY; // initialized
	float					m_dcOffsetMaxXzRadius; // initialized
												   //PHOTOMODE END

	float					m_cameraNearDist;
	SpringTracker<F32>		m_cameraNearDistSpring;

	bool					m_disablePushedInSafePos = false;
	VerticalSafeDir			m_verticalSafeDir;

	ProbeGroupIndex			m_winningGroup;

	// OUTPUT

	ProbeGroup				m_aProbeGroup[kProbeGroupCount];
	float					m_pullInPercent;
	float					m_desiredPullInPercent;
	float					m_ceilingYAdjust;
	float					m_ceilingYAdjustExtra;
	float					m_collisionPullInH;
	float					m_collisionPullInV;
	float					m_nearPlaneCloseFactor;
	float					m_nearestApproach;
	SpringTracker<F32>		m_nearestApproachSpring;

private:
	I32 PopulatePullInProbe(const CameraControlStrafeBase* pCamera, SimpleCastProbe probes[], const int iStartProbe, F32 radius);
	I32 PopulateSmallObjectProbes(SimpleCastProbe probes[], F32 radius);
	I32 PopulateCeilingProbe(const CameraControlStrafeBase* pCamera, SimpleCastProbe probes[], const int iStartProbe, F32 radius);
	I32 PopulateSafePointProbe(SimpleCastProbe probes[], const int iStartProbe, F32 radius);
	I32 PopulateNearDistProbe(SimpleCastProbe probes[], const int iStartProbe);
	I32 PopulatePhotoModeProbe(SimpleCastProbe probes[], const int iStartProbe, F32 fRadius, const CameraControlStrafeBase* pCamera);
	I32 PopulateProbesHorizontal(SimpleCastProbe probes[], const int iStartProbe, const int numProbes, F32 radius);
	I32 PopulateProbesVertical(SimpleCastProbe probes[], const int iStartProbe, const int numProbes, F32 radius, CameraArc& verticalArc, float currentArc);

	void ComputeGroupPullInPercent(SimpleCastProbe probes[], ProbeGroup& group, ProbeGroupIndex iGroup, const CamWaterPlane* pWaterPlane);
	void ComputeGroupPullInPercentPhotoMode(SimpleCastProbe probes[], ProbeGroup& group, ProbeGroupIndex iGroup);

	void KickBlockingCharacterProbe();

	mutable SphereCastJob	m_hvJob;				// horizontal and vertical probes
	mutable SphereCastJob	m_otherJob;				// the rest jobs
	mutable SphereCastJob	m_smallObjectsJob;		
	SimpleCastProbe			m_aHoriVertProbes[kNumProbesHoriVert];
	SimpleCastProbe			m_aOtherProbes[kNumProbesOthers];
	SimpleCastProbe			m_smallObjectProbes[kSmallObjectProbesCount];

	static CONST_EXPR int	kMaxBlockingCharacters = 4;
	RayCastJob				m_blockingCharacterJob;
	int						m_numBlockingCharacters;
	CharacterHandle			m_ahBlockingCharacters[kMaxBlockingCharacters];

	// INPUT

	Vector					m_cameraForwardWs;		// camera "forward" unit dir	

	float					m_pushedInSafePosBlend;	// how much we're blended into the pushed in safe pos
	SpringTracker<F32>		m_pushedInSafePosSpring;

	TwoWaySpringTracker<F32>	m_pullInSpring;
	TimeFrame				m_lastPullInTime;
	float					m_pullOutSpringConst;

	bool					m_probesValid = false;
	F32						m_timeSinceFadeIn;
	bool					m_initialized = false;
	bool					m_keepInvalid = false;

	bool					m_safePointProbeIgnoreWater = false;
	bool					m_nearPlaneProbeIgnoreWater = false;
};
