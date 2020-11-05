/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/armor.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-attack-info.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/scriptx/h/characters-collision-settings-defines.h"
#include "ndlib/render/util/prim.h"

#ifndef FINAL_BUILD

///////////////////////////////////////////////////////////////////////////////
// ArmorObserver
///////////////////////////////////////////////////////////////////////////////
// static
bool ArmorDebugWalkReinit(Process *pProc, void*)
{
	NdGameObject *pGo = NdGameObject::FromProcess(pProc);
	if (!pGo)
		return true;

	SendEvent(SID("debug-reinit-armor"), pGo);

	return true;
}

struct ArmorObserver : public ScriptObserver
{
	ArmorObserver() : ScriptObserver(FILE_LINE_FUNC, INVALID_STRING_ID_64)
	{
	}

	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) override
	{
		// Reload whenever any module is loaded since we don't necessarily know the name(s) of the module(s) that contain splashers
		if (moduleId == SID("characters-collision-settings"))
		{
			EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pRootTree, ArmorDebugWalkReinit, nullptr);
		}
	}

};

ArmorObserver		s_armorObserver;

#endif

void ArmorInit()
{
#ifndef FINAL_BUILD
	ScriptManager::RegisterObserver(&s_armorObserver);
#endif
}

//----------------------------------------------------------------------------------------------------------------------
// Armor
//----------------------------------------------------------------------------------------------------------------------

//--[virtual]-----------------------------------------------------------------------------------------------------------
void Armor::Init(const DC::CharacterCollision* pCharCollision)
{
	PROFILE(Processes, Armor_Init);

	m_numPieces   = 0;
	m_pPieceArray = nullptr;

	if (!pCharCollision || !pCharCollision->m_settings)
	{
		ASSERTF( false, ("Invalid character collision") );
		return;
	}

	m_numPieces = pCharCollision->m_settings->GetSize();
	if (m_numPieces == 0)
	{
		ASSERTF(false, ("Empty character collision"));
		return;
	}

	m_pPieceArray = NDI_NEW ArmorPiece[m_numPieces];
	ALWAYS_ASSERT(m_pPieceArray);

	for (U32F ii = 0; ii < m_numPieces; ++ii)
	{
		ArmorPiece& piece = m_pPieceArray[ii];
		piece.m_pSettings = pCharCollision->m_settings->At(ii);
		piece.m_health    = piece.m_pSettings ? piece.m_pSettings->m_health : 0.0f;
	}
}

//--[virtual]-----------------------------------------------------------------------------------------------------------
void Armor::DebugReinit(const DC::CharacterCollision* pCharCollision)
{
	STRIP_IN_FINAL_BUILD;

	if (m_pPieceArray)
		NDI_DELETE [] m_pPieceArray;

	Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);
	Init(pCharCollision);
	Memory::PopAllocator();
}

//--[virtual]-----------------------------------------------------------------------------------------------------------
void Armor::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pPieceArray, deltaPos, lowerBound, upperBound);
}

//----------------------------------------------------------------------------------------------------------------------
StringId64 Armor::GetDamageReceiverClass(StringId64 tagId, StringId64 defaultId, bool* pHit) const
{
	PROFILE(Processes, Armor_GetDamageRecClass);

	if (pHit)
		*pHit = false;

	const I32F index = FindPieceIndex(tagId, false);
	if (index == kInvalidPieceIndex)
	{
		return defaultId;
	}

	const ArmorPiece& piece = m_pPieceArray[index];
	if (piece.m_health == 0.0f)
	{
		return defaultId;
	}

	if (!piece.m_pSettings->m_absorbDamage)
	{
		return defaultId;
	}

	if (pHit)
		*pHit = true;

	return piece.m_pSettings->m_damageReceiverClass;
}

//--[static]------------------------------------------------------------------------------------------------------------
bool Armor::IsHeadShotAllowed(const DC::CharacterCollisionSettings* pDcSettings)
{
	PROFILE(Processes, Armor_IsHeadShotAllowed);

	if (pDcSettings)
	{
		return pDcSettings->m_allowHeadShot;
	}

	return false;
}

//--[static]------------------------------------------------------------------------------------------------------------
bool Armor::GetCollisionMultiplier(const DC::DamageMultArray* pMultArray,
								 float& multiplierOut,
								 StringId64 damageType,
								 float defaultValue)
{
	PROFILE(Processes, Armor_GetDamageMult);

	if (!pMultArray)
	{
		multiplierOut = defaultValue;
		return false;
	}

	const StringId64 defaultDamageType = SID("default");

	if (damageType == INVALID_STRING_ID_64)
	{
		damageType = defaultDamageType;
	}

	for (U32F ii = 0; ii < pMultArray->m_count; ++ii)
	{
		if (pMultArray->m_array[ii].m_damageType == damageType)
		{
			multiplierOut = pMultArray->m_array[ii].m_damageMultiplier;
			return damageType != defaultDamageType;
		}
		else if (pMultArray->m_array[ii].m_damageType == defaultDamageType)
		{
			defaultValue = pMultArray->m_array[ii].m_damageMultiplier;
		}
	}

	multiplierOut = defaultValue;
	return false;
}

//----------------------------------------------------------------------------------------------------------------------
int Armor::GetNumPieces() const
{
	return m_numPieces;
}

//----------------------------------------------------------------------------------------------------------------------
float Armor::GetHealth(int iPiece) const
{
	return m_pPieceArray[iPiece].m_health;
}

//----------------------------------------------------------------------------------------------------------------------
float Armor::GetHealthMax(int iPiece) const
{
	return m_pPieceArray[iPiece].m_pSettings->m_health;
}

//----------------------------------------------------------------------------------------------------------------------
void Armor::SetHealth(int iPiece, float health)
{
	m_pPieceArray[iPiece].m_health = health;
}

//--[virtual]-----------------------------------------------------------------------------------------------------------
void Armor::ApplyDamage(NdGameObject* pGo, const NdAttackInfo* pNdAttackInfo, ArmorDamageResults* pResults)
{
	PROFILE(Processes, Armor_ApplyDamage);

	if (!pGo || !pNdAttackInfo || !pResults)
	{
		ASSERTF( false, ("Invalid character, attack info or results") );
		return;
	}

	Character* pChar = Character::FromProcess(pGo);
	const IHealthSystem* pHealthSystem = pChar ? pChar->GetHealthSystem() : nullptr;
	const bool isDeadly = pHealthSystem
							  ? pHealthSystem->DeadlyDamage(pResults->m_remainingDamage,
															false,
															false,
															pNdAttackInfo->m_type == DC::kAttackTypeProjectile)
							  : true;

	for (U32F ii = 0; ii < pResults->m_numPiecesDamaged; ++ii)
	{
		const ArmorDamageResults::PieceResult& pieceResult = pResults->m_pieceResults[ii];
		const I32F pieceIndex = pieceResult.m_index;
		if (pieceIndex == kInvalidPieceIndex || pieceIndex >= m_numPieces)
		{
			ASSERTF( false, ("Invalid armor piece index") );
			continue;
		}

		ArmorPiece& piece = m_pPieceArray[pieceIndex];
		// Accessories (e.g. hats/gasmasks) have zero health, but don't absorb damage either
		if (piece.m_health <= 0.0f && pieceResult.m_damage > 0.0f)
		{
			ASSERTF( piece.m_health == 0.0f, ("Armor piece with negative health") );
			continue;
		}

		//if (pieceResult.m_damage <= 0.0f)
		//{
		//	ASSERTF( false, ("No damage included in damage results - Are there settings for the weapon in weapon-damages.dc?") );
		//	continue;
		//}

		piece.m_health -= Min(piece.m_health, pieceResult.m_damage);

		// Account for floating point accumulation error
		if (piece.m_health <= 0.1f)
		{
			++pResults->m_numPiecesDestroyed;

			piece.m_health = 0.0f;
			OnDestroyed(pGo, pNdAttackInfo, pieceIndex, true, isDeadly);
		}
		else
		{
			OnDamaged(pGo, pNdAttackInfo, pieceIndex);
		}
	}
}

//----------------------------------------------------------------------------------------------------------------------
void Armor::DebugDraw(const NdGameObject* pGo,
					  ScreenSpaceTextPrinter* pPrinter,
					  StringId64 defaultDamageReceiverClasssId) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pGo)
	{
		ASSERTF( false, ("Invalid character") );
		return;
	}

	if (!m_pPieceArray)
	{
		ASSERTF( false, ("Armor not initialized") );
		return;
	}

	const CompositeBody* pCompBody = pGo->GetCompositeBody();

	for (U32F ii = 0; ii < m_numPieces; ++ii)
	{
		const ArmorPiece& piece = m_pPieceArray[ii];

		const DC::CharacterCollisionSettings* pDcSettings = piece.m_pSettings;
		if (!pDcSettings)
		{
			continue;
		}

		Point setPos;
		char buf[128];
		const Color color   = GetPieceDebugDrawColor(pGo, piece);
		const char* setName = pDcSettings->m_setId != INVALID_STRING_ID_64         ?
							  DevKitOnly_StringIdToString(pDcSettings->m_setId) :
							  "<no set>";

		for (U32F jj = 0; jj < pDcSettings->m_numTags; ++jj)
		{
			Point pos;
			if (FindCollisionBodyPositionWs(&pos, pCompBody, pDcSettings->m_tags[jj]))
			{
				const StringId64 tagId               = pDcSettings->m_tags[jj];
				const StringId64 damageReceiverClasssId = GetDamageReceiverClass(tagId, defaultDamageReceiverClasssId);
				const char* tag							= DevKitOnly_StringIdToString(tagId);
				const char* damageReceiverClassName   = DevKitOnly_StringIdToString(damageReceiverClasssId);

				if (jj == 0)
				{
					setPos = pos;
					sprintf(buf, "%s (%s) %s %3.1f/%d",
							setName,
							tag,
							damageReceiverClassName,
							piece.m_health,
							pDcSettings->m_health);
				}
				else
				{
					sprintf(buf, "%s (%s) %s",
							setName,
							tag,
							damageReceiverClassName);

					g_prim.Draw( DebugArrow(setPos, pos, kColorWhite, 0.25f, kPrimEnableHiddenLineAlpha) );
				}

				g_prim.Draw( DebugString(pos, buf, color, 0.75f) );
			}
		}
	}
}

//--[virtual]-----------------------------------------------------------------------------------------------------------
void Armor::OnDestroyed(NdGameObject* pGo,
						const NdAttackInfo* pNdAttackInfo,
						I32F pieceIndex,
						bool allowFx,
						bool isDeadly)
{
	PROFILE(Processes, Armor_OnDestroyed);

	const ArmorPiece& piece = m_pPieceArray[pieceIndex];
	const DC::CharacterCollisionSettings* pDcSettings = piece.m_pSettings;
	if (!pDcSettings)
	{
		return;
	}

	if (pDcSettings->m_disableOnDestroy)
	{
		DisableSetCollision(pDcSettings, pGo->GetCompositeBody());
	}
}

//--[virtual]-----------------------------------------------------------------------------------------------------------
const Color Armor::GetPieceDebugDrawColor(const NdGameObject* pGo, const ArmorPiece& piece) const
{
	return piece.m_health > 0.0f ? kColorWhite : kColorDarkGray;
}

//----------------------------------------------------------------------------------------------------------------------
I32F Armor::FindPieceIndex(const StringId64 tagId, bool outsideHeadshotRange) const
{
	PROFILE(Processes, Armor_FindPieceIndex);

	if (tagId == INVALID_STRING_ID_64)
	{
		return kInvalidPieceIndex;
	}

	if (!m_pPieceArray)
	{
		return kInvalidPieceIndex;
	}

	I32F fallbackIdx = kInvalidPieceIndex;

	for (I32F ii = 0; ii < m_numPieces; ++ii)
	{
		const ArmorPiece& piece = m_pPieceArray[ii];
		const DC::CharacterCollisionSettings* pDcSettings = piece.m_pSettings;
		if (!pDcSettings)
		{
			continue;
		}

		for (U32F jj = 0; jj < pDcSettings->m_numTags; ++jj)
		{
			if (pDcSettings->m_tags[jj] == tagId)
			{
				// Try to deflect to helmet if applicable.
				if (tagId == SID("targHead") && outsideHeadshotRange && pDcSettings->m_allowHeadShot)
					fallbackIdx = ii;
				else
					return ii;
			}
			else if (tagId == SID("targHead") && pDcSettings->m_tags[jj] == SID("targHelmet") && outsideHeadshotRange && !pDcSettings->m_allowHeadShot)
			{
				// Bullets slide off the face onto the helmet outside of headshot range.
				return ii;
			}
		}
	}

	return fallbackIdx;
}

//----------------------------------------------------------------------------------------------------------------------
bool Armor::FindCollisionBodyPositionWs(Point* pBodyPosWs,
										const CompositeBody* pCompositeBody,
										StringId64 tagId) const
{
	PROFILE(Processes, Armor_LookupColBodyPosWs);

	ASSERTF( pCompositeBody, ("Invalid composite body") );

	for (U32F ii = 0; ii < pCompositeBody->GetNumBodies(); ++ii)
	{
		if (pCompositeBody->GetBody(ii)->GetTag() == tagId)
		{
			*pBodyPosWs = pCompositeBody->GetBody(ii)->GetLocatorCm().GetPosition();
			return true;
		}
	}

	return false;
}

//----------------------------------------------------------------------------------------------------------------------
void Armor::DisableSetCollision(const DC::CharacterCollisionSettings* pDcSettings, CompositeBody* pCompositeBody) const
{
	PROFILE(Processes, Armor_DisableSetCol);

	if (!pDcSettings || !pCompositeBody)
	{
		ASSERTF( false, ("Invalid collision settings or composite body") );
		return;
	}

	// Disable all rigid bodies (collision capsules) in the collision set
	for (U32F ii = 0; ii < pCompositeBody->GetNumBodies(); ++ii)
	{
		RigidBody* pBody = pCompositeBody->GetBody(ii);
		if (pBody->GetMotionType() == kRigidBodyMotionTypeNonPhysical)
		{
			continue;
		}

		for (U32F jj = 0; jj < pDcSettings->m_numTags; ++jj)
		{
			if (pBody->GetTag() == pDcSettings->m_tags[jj])
			{
				pBody->SetMotionType(kRigidBodyMotionTypeNonPhysical);
				break;
			}
		}
	}
}
