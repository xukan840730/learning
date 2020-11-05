/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef PROCESS_CHAR_CLOTH_H
#define PROCESS_CHAR_CLOTH_H

#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-multi-attachable.h"
#include "ndlib/fx/fx-handle.h"

FWD_DECL_PROCESS_HANDLE(ProcessCharClothBg);
class ProcessSpawnInfo;
class OParBody;

/// --------------------------------------------------------------------------------------------------------------- ///
struct CharClothInfo : public NdAttachableInfo
{
	StringId64	m_clothCollider = INVALID_STRING_ID_64;
	float		m_externalWindMul = 1.0f;
	StringId64	m_baseMovementJoint = INVALID_STRING_ID_64;
	bool		m_enableSkinning = true;
	bool		m_enableSkinMoveDist = false;
	float		m_disableBendingForceDist = 7.0f;
	float		m_disableSimulationMinDist = 5.0f;
	float		m_disableSimulationMaxDist = 15.0f;
	bool		m_orientedParticles = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessCharCloth : public NdMultiAttachable
{
public:
	typedef NdMultiAttachable ParentClass;
	FROM_PROCESS_DECLARE(ProcessCharCloth);

	ProcessCharCloth();
	~ProcessCharCloth() override;

	virtual Err	Init(const ProcessSpawnInfo& info) override;
	virtual Err InitCollision(const ProcessSpawnInfo& info) override;
	virtual void OnKillProcess() override;
	virtual void UpdateRoot(bool postCameraAdjust = false) override;
	void PostJointUpdate_Async() override;
	virtual void PostAnimBlending_Async() override;
	virtual void ClothUpdate_Async() override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual I32 GetMudMaskIndex() const override { return GetWetMaskIndex(); }
	virtual I32	GetDynamicMaskType() const override { return kDynamicMaskTypeNone; }
	virtual void EventHandler(Event& event) override;

	virtual FxRenderMaskHandle	GetBloodMaskHandle() const override					{ return m_bloodMaskHandle;	}
	virtual void				SetBloodMaskHandle(FxRenderMaskHandle hdl) override	{ m_bloodMaskHandle = hdl;	}

	virtual bool IsProcessCharClothBg() {return false;}

	bool IsPrerollActive() {return m_cameraCutPrerollCloth.Valid();}
	virtual void SpawnPreroll(const ArtItemAnim* pAnim, StringId64 apSpawner, U32 frameNum, Vector_arg windVec);
	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const override;

	void SetCameraCutsUpdated() {m_cameraCutsUpdatedThisFrame = true;}
	bool GetCameraCutsUpdated() {return m_cameraCutsUpdatedThisFrame;}

	virtual void Show(int screenIndex, bool secondary) override;

private:
	NdGameObjectHandle	m_hAdditionalAttachObject = nullptr;
	StringId64			m_additionalAttachObjectJoint = INVALID_STRING_ID_64;
	StringId64			m_additionalAttachClothJoint = INVALID_STRING_ID_64;
	float				m_additionalAttachBlend = 0.0f;
	Point				m_additionalAttachBlendPos = kZero;
	bool				m_additionalAttachDetaching = false;
	float				m_additionalOffsetY = 0.0f;

	StringId64			m_externalColliderObjectId = INVALID_STRING_ID_64;
	StringId64			m_externalColliderProtoId = INVALID_STRING_ID_64;
	bool				m_externalColliderObjectInitialized = false;

	float				m_externalWindMul = 1.0f;
	StringId64			m_baseMovementJoint = INVALID_STRING_ID_64;
	bool				m_enableSkinning = false;
	bool				m_enableSkinMoveDist = false;
	float				m_charDisableBendingForceDist = 7.0f;
	float				m_charDisableSimulationMinDist = 5.0f;
	float				m_charDisableSimulationMaxDist = 15.0f;
	float				m_windLerpCurrTime = 0.0f;
	float				m_windLerpTime = 0.0f;
	Vector				m_windLerpStart = kZero;
	Vector				m_windLerpEnd = kZero;

	FxRenderMaskHandle	m_bloodMaskHandle = kFxRenderMaskHandleInvalid;

	// Save some info for spawning bg process
	StringId64			m_clothCollider = INVALID_STRING_ID_64;
	StringId64			m_attachJointId = INVALID_STRING_ID_64;
	StringId64			m_parentAttachId = INVALID_STRING_ID_64;

	static const int	kJointSuffixLen = 64;
	char				m_jointSuffix[kJointSuffixLen];

	MutableProcessCharClothBgHandle	m_cameraCutPrerollCloth;
	bool	m_cameraCutsUpdatedThisFrame = false;

	OParBody*			m_pOParBody;

	void UseParentsWetMask();
};

PROCESS_DECLARE(ProcessCharCloth);

extern bool IsCharClothEvent(const Event& event);

#endif
