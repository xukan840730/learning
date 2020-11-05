/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef ARMOR_H
#define ARMOR_H

#include "corelib/system/templatized-identifier.h"

struct NdAttackInfo;

class Character;
class CompositeBody;
class ScreenSpaceTextPrinter;

namespace DC
{
	struct CharacterCollision;
	struct CharacterCollisionSettings;
	struct DamageMultArray;
};

class ArmorDamageResults;
struct ArmorAttackInfo;

//----------------------------------------------------------------------------------------------------------------------
class Armor
{

public:

	enum
	{
		kInvalidPieceIndex = -1
	};

	Armor() : m_numPieces(0), m_pPieceArray(nullptr) {}

	virtual void Init(const DC::CharacterCollision* pCharCollision);
	virtual void DebugReinit(const DC::CharacterCollision* pCharCollision);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	StringId64 GetDamageReceiverClass(StringId64 tagId, StringId64 defaultId, bool* pHit=nullptr) const;
	static bool IsHeadShotAllowed(const DC::CharacterCollisionSettings* pDcSettings);

	// Returns True iff the specified damage type was found (i.e. not the default val).
	static bool GetCollisionMultiplier(const DC::DamageMultArray* pMultArray,
									 float& multiplierOut,
									 StringId64 damageType = INVALID_STRING_ID_64,
									 float defaultValue = 1.0f);

	int GetNumPieces() const;
	I32F FindPieceIndex(const StringId64 tagId, bool outsideHeadshotRange) const;

	float GetHealth(int iPiece) const;
	float GetHealthMax(int iPiece) const;
	void SetHealth(int iPiece, float health);

	virtual float ComputeDamage(const NdGameObject* pGo,
								ArmorAttackInfo* pAttackInfo,
							    ArmorDamageResults* pArmorDamageResults) const = 0;
	virtual float ComputeExplosionDamage(const NdGameObject* pGo,
										 ArmorAttackInfo* pAttackInfo,
										 ArmorDamageResults* pArmorDamageResults) const = 0;
	virtual void ApplyDamage(NdGameObject* pGo,
							 const NdAttackInfo* pNdAttackInfo,
							 ArmorDamageResults* pArmorDamageResults);


	virtual void DebugDraw(const NdGameObject* pGo,
						   ScreenSpaceTextPrinter* pPrinter,
						   StringId64 defaultDamageReceiverClasssId) const;

protected:

	struct ArmorPiece
	{
		const DC::CharacterCollisionSettings* m_pSettings;
		float m_health;
	};

	virtual void OnDestroyed(NdGameObject* pGo,
							 const NdAttackInfo* pNdAttackInfo,
							 I32F pieceIndex,
							 bool allowFx = true,
							 bool isDeadly = false);
	virtual void OnDamaged(NdGameObject* pGo,
						   const NdAttackInfo* pNdAttackInfo,
						   I32F pieceIndex,
						   bool allowFx = true) {}
	virtual const Color GetPieceDebugDrawColor(const NdGameObject* pGo, const ArmorPiece& piece) const;

	bool FindCollisionBodyPositionWs(Point* pBodyPosWs, const CompositeBody* pCompositeBody, StringId64 tagId) const;
	void DisableSetCollision(const DC::CharacterCollisionSettings* pDcSettings, CompositeBody* pCompositeBody) const;

	U32			m_numPieces;
	ArmorPiece*	m_pPieceArray;
};

//----------------------------------------------------------------------------------------------------------------------
class ArmorDamageResults
{

public:

	enum
	{
		kMaxPieceResults = 32
	};

	U32   m_numPiecesDamaged = 0;
	U32   m_numPiecesDestroyed = 0;
	float m_remainingDamage = 0.0f;		// Damage not absorbed by armor

	struct PieceResult
	{
		I32    m_index = Armor::kInvalidPieceIndex;
		float  m_damage = 0.0f;
		float  m_remainingDamage = 0.0f;
		Point  m_impactPoint = kZero;
		Vector m_impactNormal = kZero;
	};

	PieceResult m_pieceResults[kMaxPieceResults];
};

//----------------------------------------------------------------------------------------------------------------------
struct ArmorAttackInfo
{
};


void ArmorInit();

#endif // ARMOR_H
