/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/display.h"
#include "ndlib/render/util/text.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/scriptx/h/anim-character-defines.h"

struct CharacterDynamicStateField
{
	static const int kMaxEntries = 32;

	StringId64 m_name;
	bool m_valid;

	const ArtItemAnim* m_pPhaseAnim;
	const EffectAnim* m_pEffectAnim;
	
	DC::BoxableType m_type;
	BoxedValue m_defaultVal;

	int m_numEntriesState;
	float m_stateCond[kMaxEntries];
	DC::DynamicStateInfoCondType m_condTypes[kMaxEntries];
	BoxedValue m_stateValues[kMaxEntries];

	int m_numEntriesEff;
	float m_effFrameNums[kMaxEntries];
	BoxedValue m_effValues[kMaxEntries];

	float GetStatePhase(int iEntry) const
	{
		float phase = 0.0f;
		switch (m_condTypes[iEntry])
		{
			case DC::kDynamicStateInfoCondTypePhase:
				phase = MinMax01(m_stateCond[iEntry]);
				break;

			case DC::kDynamicStateInfoCondTypeFrame:
				phase = MinMax01(m_stateCond[iEntry] * m_pPhaseAnim->m_pClipData->m_phasePerFrame * m_pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f);
				break;

			case DC::kDynamicStateInfoCondTypeFrameFromEnd:
				phase = MinMax01(1.0f - m_stateCond[iEntry] * m_pPhaseAnim->m_pClipData->m_phasePerFrame * m_pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f);
				break;

			case DC::kDynamicStateInfoCondTypeTime:
				phase = MinMax01(m_stateCond[iEntry] * m_pPhaseAnim->m_pClipData->m_phasePerFrame * m_pPhaseAnim->m_pClipData->m_framesPerSecond);
				break;

			case DC::kDynamicStateInfoCondTypeTimeFromEnd:
				phase = MinMax01(1.0f - m_stateCond[iEntry] * m_pPhaseAnim->m_pClipData->m_phasePerFrame * m_pPhaseAnim->m_pClipData->m_framesPerSecond);
				break;
		}

		return phase;
	}

	float GetStateFrameNum(int iEntry) const
	{
		return GetStatePhase(iEntry) * m_pPhaseAnim->m_pClipData->m_fNumFrameIntervals * 30.0f * m_pPhaseAnim->m_pClipData->m_secondsPerFrame;
	}

	float GetEffPhase(int iEntry) const
	{
		return m_effFrameNums[iEntry] * m_pPhaseAnim->m_pClipData->m_phasePerFrame * m_pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f;
	}

	float GetEffFrameNum(int iEntry) const
	{
		return m_effFrameNums[iEntry];
	}
};

void CharacterDynamicStateDataGetFieldData(CharacterDynamicStateField* pField, const DC::DynamicStateInfoArray* dynamic, const ArtItemAnim* pPhaseAnim,
	const EffectAnim* pEffectAnim, StringId64 name)
{
	pField->m_name = name;
	pField->m_valid = false;
	pField->m_pPhaseAnim = pPhaseAnim;
	pField->m_pEffectAnim = pEffectAnim;

	pField->m_type = DC::kBoxableTypeInvalid;
	pField->m_defaultVal = 0;

	pField->m_numEntriesState = 0;
	pField->m_numEntriesEff = 0;

	for (int iEntry=0; iEntry<dynamic->m_count; iEntry++)
	{
		const DC::DynamicStateInfoEntry* pEntry = &dynamic->m_array[iEntry];
		if (pEntry->m_name != name)
			continue;

		const BoxedValue* pBoxed = PunPtr<const BoxedValue*>(pEntry->m_data);

		if (pField->m_type == DC::kBoxableTypeInvalid)
			pField->m_type = pBoxed->GetType();

		GAMEPLAY_ASSERT(pField->m_type == pBoxed->GetType());

		pField->m_valid = true;
		if (pEntry->m_condType == DC::kDynamicStateInfoCondTypeDefault)
			pField->m_defaultVal = *pBoxed;
		else
		{
			GAMEPLAY_ASSERT(pField->m_numEntriesState < CharacterDynamicStateField::kMaxEntries);
			pField->m_stateCond[pField->m_numEntriesState] = pEntry->m_cond;
			pField->m_condTypes[pField->m_numEntriesState] = pEntry->m_condType;
			pField->m_stateValues[pField->m_numEntriesState] = *pBoxed;
			pField->m_numEntriesState++;
		}
	}

	if (!pField->m_valid)
		return;

	// Bubble sort by phase
	for (int i=1; i<pField->m_numEntriesState; i++)
	{
		for (int j=i; j>0; j--)
		{
			if (pField->GetStatePhase(j) < pField->GetStatePhase(j-1))
			{
				float tempPhase = pField->m_stateCond[j];
				pField->m_stateCond[j] = pField->m_stateCond[j-1];
				pField->m_stateCond[j-1] = tempPhase;

				DC::DynamicStateInfoCondType tempCondType = pField->m_condTypes[j];
				pField->m_condTypes[j] = pField->m_condTypes[j-1];
				pField->m_condTypes[j-1] = tempCondType;

				BoxedValue tempBoxed = pField->m_stateValues[j];
				pField->m_stateValues[j] = pField->m_stateValues[j-1];
				pField->m_stateValues[j-1] = tempBoxed;
			}
			else
				continue;
		}
	}

	if (pEffectAnim)
	{
		for (int iEffect=0; iEffect<pEffectAnim->m_numEffects; iEffect++)
		{
			const EffectAnimEntry* pEffect = &pEffectAnim->m_pEffects[iEffect];
			if (pEffect->GetNameId() != SID("dynamic-state-data"))
				continue;

			const EffectAnimEntryTag* pTag = pEffect->GetTagByName(name);
			if (pTag == nullptr)
				continue;

			GAMEPLAY_ASSERT(pField->m_numEntriesEff < CharacterDynamicStateField::kMaxEntries);

			pField->m_effFrameNums[pField->m_numEntriesEff] = pEffect->GetFrame();

			BoxedValue effVal = 0;
			switch (pField->m_type)
			{
				case DC::kBoxableTypeBool:
				case DC::kBoxableTypeI32:
					effVal = pTag->GetValueAsI32();
					break;
			
				case DC::kBoxableTypeF32:
					effVal = pTag->GetValueAsF32();
					break;
			
				case DC::kBoxableTypeStringId:
					effVal = pTag->GetValueAsStringId();
					break;
			}

			pField->m_effValues[pField->m_numEntriesEff] = effVal;
			pField->m_numEntriesEff++;
		}
	}

	// Bubble sort by frame num
	for (int i=1; i<pField->m_numEntriesEff; i++)
	{
		for (int j=i; j>0; j--)
		{
			if (pField->m_effFrameNums[j] < pField->m_effFrameNums[j-1])
			{
				float tempFrameNum = pField->m_effFrameNums[j];
				pField->m_effFrameNums[j] = pField->m_effFrameNums[j-1];
				pField->m_effFrameNums[j-1] = tempFrameNum;

				BoxedValue tempBoxed = pField->m_effValues[j];
				pField->m_effValues[j] = pField->m_effValues[j-1];
				pField->m_effValues[j-1] = tempBoxed;
			}
			else
				continue;
		}
	}

}

BoxedValue CharacterDynamicStateData(const DC::DynamicStateInfoArray *pInfo, const ArtItemAnim* pPhaseAnim, StringId64 name, float phase, BoxedValue defaultVal)
{
	PROFILE_AUTO(Processes);

	if (pInfo)
	{
		if (pPhaseAnim == nullptr)
			return defaultVal;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, pInfo, pPhaseAnim, pEffectAnim, name);

		if (field.m_valid)
		{
			BoxedValue val = field.m_defaultVal;

			if (field.m_numEntriesEff > 0)
			{
				float phaseAnimFrameNum = phase * pPhaseAnim->m_pClipData->m_fNumFrameIntervals * 30.0f * pPhaseAnim->m_pClipData->m_secondsPerFrame;
				for (int iEff=0; iEff<field.m_numEntriesEff; iEff++)
				{
					if (phaseAnimFrameNum >= field.m_effFrameNums[iEff])
						val = field.m_effValues[iEff];
					else
						break;
				}
			}
			else
			{
				for (int iStateEntry=0; iStateEntry<field.m_numEntriesState; iStateEntry++)
				{
					if (phase >= field.GetStatePhase(iStateEntry))
						val = field.m_stateValues[iStateEntry];
					else
						break;
				}
			}

			return val;
		}
	}

	return defaultVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue CharacterDynamicStateData(const Character* pChar,
									 const DC::AnimState* pState,
									 StringId64 name,
									 float phase,
									 BoxedValue defaultVal)
{
	PROFILE_AUTO(Processes);

	const DC::DynamicStateInfoArray* dynamic = AnimStateLookupStateInfo<DC::DynamicStateInfoArray>(pState, SID("dynamic"));
	const ArtItemAnim* pPhaseAnim = pChar->GetAnimControl()->LookupAnim(pState->m_phaseAnimName).ToArtItem();

	return CharacterDynamicStateData(dynamic, pPhaseAnim, name, phase, defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue CharacterDynamicStateData(const AnimStateInstance* pInst, StringId64 name, float phase, BoxedValue defaultVal)
{
	PROFILE_AUTO(Processes);

	if (pInst == nullptr)
		return defaultVal;

	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pInst, SID("dynamic"));
	const ArtItemAnim* pPhaseAnim = pInst->GetPhaseAnimArtItem().ToArtItem();

	return CharacterDynamicStateData(dynamic, pPhaseAnim, name, phase, defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue CharacterDynamicStateData(const AnimStateInstance* pInst, StringId64 name, BoxedValue defaultVal)
{
	PROFILE_AUTO(Processes);
	if (pInst == nullptr)
		return defaultVal;

	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pInst, SID("dynamic"));
	const ArtItemAnim* pPhaseAnim = pInst->GetPhaseAnimArtItem().ToArtItem();

	return CharacterDynamicStateData(dynamic, pPhaseAnim, name, pInst->GetPhase(), defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue CharacterDynamicStateData(const Character* pChar, StringId64 name, BoxedValue defaultVal)
{
	return CharacterDynamicStateData(pChar->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance(), name, defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterDynamicStateDataEnablePhase(const Character* pChar, const DC::AnimState* pState, StringId64 name, BoxedValue targetVal)
{
	const DC::DynamicStateInfoArray* dynamic = AnimStateLookupStateInfo<DC::DynamicStateInfoArray>(pState, SID("dynamic"));

	if (dynamic)
	{
		const ArtItemAnim* pPhaseAnim = pChar->GetAnimControl()->LookupAnim(pState->m_phaseAnimName).ToArtItem();
		if (pPhaseAnim == nullptr)
			return -1.0f;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, name);

		if (field.m_valid)
		{
			BoxedValue val = field.m_defaultVal.GetAsBool();

			if (field.m_numEntriesEff > 0)
			{
				if (field.m_effFrameNums[0] == 0.0f)
					val = field.m_effValues[0].GetAsBool();

				if (val == targetVal)
					return 0.0f;

				for (int iEff = 0; iEff < field.m_numEntriesEff; iEff++)
				{
					if (targetVal == field.m_effValues[iEff].GetAsBool())
					{
						float effPhase = field.m_effFrameNums[iEff] * pPhaseAnim->m_pClipData->m_phasePerFrame * pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f;
						return effPhase;
					}
				}

				return -1.0f;
			}
			else
			{
				for (int iStateEntry = 0; iStateEntry < field.m_numEntriesState; iStateEntry++)
				{
					if (targetVal == field.m_stateValues[iStateEntry].GetAsBool())
						return field.GetStatePhase(iStateEntry);
				}

				return -1.0f;
			}
		}
	}

	return -1.0f;
}

float CharacterDynamicStateDataEnablePhase(const AnimStateInstance* pInst, StringId64 name, BoxedValue targetVal)
{
	PROFILE_AUTO(Processes);

	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pInst, SID("dynamic"));

	if (dynamic)
	{
		const ArtItemAnim* pPhaseAnim = pInst->GetPhaseAnimArtItem().ToArtItem();
		if (pPhaseAnim == nullptr)
			return -1.0f;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, name);

		if (field.m_valid)
		{
			BoxedValue val = field.m_defaultVal.GetAsBool();

			if (field.m_numEntriesEff > 0)
			{
				if (field.m_effFrameNums[0] == 0.0f)
					val = field.m_effValues[0].GetAsBool();

				if (val == targetVal)
					return 0.0f;

				for (int iEff=0; iEff<field.m_numEntriesEff; iEff++)
				{
					if (targetVal == field.m_effValues[iEff].GetAsBool())
					{
						float effPhase = field.m_effFrameNums[iEff] * pPhaseAnim->m_pClipData->m_phasePerFrame * pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f;
						return effPhase;
					}
				}

				return -1.0f;
			}
			else
			{
				for (int iStateEntry=0; iStateEntry<field.m_numEntriesState; iStateEntry++)
				{
					if (targetVal == field.m_stateValues[iStateEntry].GetAsBool())
						return field.GetStatePhase(iStateEntry);
				}

				return -1.0f;
			}
		}
	}

	return -1.0f;
}

float CharacterDynamicStateDataEnablePhase(const Character* pChar, StringId64 name, BoxedValue targetVal)
{
	return CharacterDynamicStateDataEnablePhase(pChar->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance(), name, targetVal);
}


bool CharacterDynamicStateDataBool(const Character* pChar, StringId64 name, bool defaultVal)
{
	BoxedValue val = CharacterDynamicStateData(pChar, name, defaultVal);
	return val.GetAsBool();
}

float CharacterDynamicStateDataFloat(const Character* pChar, StringId64 name, float defaultVal)
{
	BoxedValue val = CharacterDynamicStateData(pChar, name, defaultVal);
	return val.GetAsF32();
}

float CharacterDynamicStateDataFloatAtPhase(const Character* pChar, StringId64 name, float phase, float defaultVal)
{
	PROFILE_AUTO(Processes);

	const AnimControl* pAnimControl = pChar->GetAnimControl();

	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pAnimControl, SID("dynamic"), INVALID_STRING_ID_64);
	if (dynamic)
	{
		const AnimStateLayer* pBaseStateLayer = pAnimControl->GetStateLayerById(SID("base"));
		const ArtItemAnim* pPhaseAnim = pBaseStateLayer->CurrentStateInstance()->GetPhaseAnimArtItem().ToArtItem();
		if (pPhaseAnim == nullptr)
			return defaultVal;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, name);

		if (field.m_valid)
		{
			float val = field.m_defaultVal.GetAsF32();

			if (field.m_numEntriesEff > 0)
			{
				float phaseAnimFrameNum = phase * pPhaseAnim->m_pClipData->m_fNumFrameIntervals * 30.0f * pPhaseAnim->m_pClipData->m_secondsPerFrame;
				for (int iEff=0; iEff<field.m_numEntriesEff; iEff++)
				{
					if (phaseAnimFrameNum >= field.m_effFrameNums[iEff])
						val = field.m_effValues[iEff].GetAsF32();
					else
						break;
				}
			}
			else
			{
				for (int iStateEntry=0; iStateEntry<field.m_numEntriesState; iStateEntry++)
				{
					if (phase >= field.GetStatePhase(iStateEntry))
						val = field.m_stateValues[iStateEntry].GetAsF32();
					else
						break;
				}
			}

			return val;
		}
	}

	return defaultVal;
}

float CharacterDynamicStateDataBoolEnablePhase(const DC::DynamicStateInfoArray* pInfo, const ArtItemAnim* pPhaseAnim, StringId64 name, bool targetVal)
{
	PROFILE_AUTO(Processes);

	if (pInfo)
	{
		if (pPhaseAnim == nullptr)
			return -1.0f;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, pInfo, pPhaseAnim, pEffectAnim, name);

		if (field.m_valid)
		{
			bool val = field.m_defaultVal.GetAsBool();

			if (field.m_numEntriesEff > 0)
			{
				if (field.m_effFrameNums[0] == 0.0f)
					val = field.m_effValues[0].GetAsBool();

				if (val == targetVal)
					return 0.0f;

				for (int iEff=0; iEff<field.m_numEntriesEff; iEff++)
				{
					if (targetVal == field.m_effValues[iEff].GetAsBool())
					{
						float effPhase = field.m_effFrameNums[iEff] * pPhaseAnim->m_pClipData->m_phasePerFrame * pPhaseAnim->m_pClipData->m_framesPerSecond / 30.0f;
						return effPhase;
					}
				}

				return -1.0f;
			}
			else
			{
				for (int iStateEntry=0; iStateEntry<field.m_numEntriesState; iStateEntry++)
				{
					if (targetVal == field.m_stateValues[iStateEntry].GetAsBool())
						return field.GetStatePhase(iStateEntry);
				}

				return -1.0f;
			}
		}
	}

	return -1.0f;
}

float CharacterDynamicStateDataBoolEnablePhase(const Character* pChar, StringId64 name, bool targetVal)
{
	const DC::DynamicStateInfoArray* pInfo = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pChar->GetAnimControl(), SID("dynamic"), INVALID_STRING_ID_64);
	const ArtItemAnim* pPhaseAnim = pChar->GetAnimControl()->GetStateLayerById(SID("base"))->CurrentStateInstance()->GetPhaseAnimArtItem().ToArtItem();

	return CharacterDynamicStateDataBoolEnablePhase(pInfo, pPhaseAnim, name, targetVal);
}

float CharacterDynamicStateDataBoolEnablePhase(const AnimStateInstance* pInst, StringId64 name, bool targetVal)
{
	const DC::DynamicStateInfoArray* pInfo = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pInst, SID("dynamic"));
	const ArtItemAnim* pPhaseAnim = pInst->GetPhaseAnimArtItem().ToArtItem();

	return CharacterDynamicStateDataBoolEnablePhase(pInfo, pPhaseAnim, name, targetVal);
}

float CharacterDynamicStateDataBoolEnablePhase(const Character* pChar, const DC::AnimState* pState, StringId64 name, bool targetVal)
{
	const DC::DynamicStateInfoArray* pInfo = AnimStateLookupStateInfo<DC::DynamicStateInfoArray>(pState, SID("dynamic"));
	const ArtItemAnim* pPhaseAnim = pChar->GetAnimControl()->LookupAnim(pState->m_phaseAnimName).ToArtItem();

	return CharacterDynamicStateDataBoolEnablePhase(pInfo, pPhaseAnim, name, targetVal);
}

float CharacterDynamicStateDataBlendVal(const AnimStateInstance* pInst, StringId64 name, float defaultVal)
{
	PROFILE_AUTO(Processes);

	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pInst, SID("dynamic"));
	if (dynamic)
	{
		const ArtItemAnim* pPhaseAnim = pInst->GetPhaseAnimArtItem().ToArtItem();
		if (pPhaseAnim == nullptr)
			return defaultVal;

		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		CharacterDynamicStateField field;
		CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, name);

		const float phase = pInst->GetPhase();

		if (field.m_valid)
		{
			float val = field.m_defaultVal.GetAsF32();
			

			if (field.m_numEntriesEff > 0)
			{
				const float phaseAnimFrameNum = phase * pPhaseAnim->m_pClipData->m_fNumFrameIntervals * 30.0f * pPhaseAnim->m_pClipData->m_secondsPerFrame;
				int iValA=-1, iValB=-1;
				for (int iEff=0; iEff<field.m_numEntriesEff; iEff++)
				{
					iValB = iEff;

					if (phaseAnimFrameNum >= field.m_effFrameNums[iEff])
						iValA = iEff;
					else
						break;
				}

				const float frameNumA = (iValA == -1) ? 0.0f : field.m_effFrameNums[iValA];
				const float frameNumB = field.m_effFrameNums[iValB];
				const float valA = (iValA == -1) ? field.m_defaultVal.GetAsF32() : field.m_effValues[iValA].GetAsF32();
				const float valB = field.m_effValues[iValB].GetAsF32();

				val = LerpScaleClamp(frameNumA, frameNumB, valA, valB, phaseAnimFrameNum);
			}
			else if (field.m_numEntriesState > 0)
			{
				int iValA=-1, iValB=-1;
				for (int iStateEntry=0; iStateEntry<field.m_numEntriesState; iStateEntry++)
				{
					iValB = iStateEntry;

					if (phase >= field.GetStatePhase(iStateEntry))
						iValA = iStateEntry;
					else
						break;
				}


				const float phaseA = (iValA == -1) ? 0.0f : field.GetStatePhase(iValA);
				const float phaseB = field.GetStatePhase(iValB);
				const float valA = (iValA == -1) ? field.m_defaultVal.GetAsF32() : field.m_stateValues[iValA].GetAsF32();
				const float valB = field.m_stateValues[iValB].GetAsF32();

				val = LerpScaleClamp(phaseA, phaseB, valA, valB, phase);
			}

			return val;
		}
	}

	return defaultVal;
}

bool CharacterDynamicStateDataBool(const NdGameObject* pGo, StringId64 name)
{
	const Character* pChar = Character::FromProcess(pGo);
	return CharacterDynamicStateDataBool(pChar, name);
}

void CharacterDynamicStateDataDebugPrintType(char* buf, int maxLen, DC::BoxableType type)
{
	switch (type)
	{
		case DC::kBoxableTypeBool:
			snprintf(buf, maxLen, "boolean");
			break;
			
		case DC::kBoxableTypeI32:
			snprintf(buf, maxLen, "int32");
			break;
			
		case DC::kBoxableTypeF32:
			snprintf(buf, maxLen, "float");
			break;
			
		case DC::kBoxableTypeStringId:
			snprintf(buf, maxLen, "symbol");
			break;
	}
}

void CharacterDynamicStateDataDebugPrintValue(char* buf, int maxLen, float sample, bool isFrameNum, DC::BoxableType type, BoxedValue val)
{
	switch (type)
	{
		case DC::kBoxableTypeBool:
		case DC::kBoxableTypeI32:
			if (sample == 0.0f && !isFrameNum)
				snprintf(buf, maxLen, "default (%d)", val.GetAsI32());
			else if (isFrameNum)
				snprintf(buf, maxLen, "frame %.1f (%d)", sample, val.GetAsI32());
			else
				snprintf(buf, maxLen, "phase %.2f (%d)", sample, val.GetAsI32());
			break;
			
		case DC::kBoxableTypeF32:
			if (sample == 0.0f && !isFrameNum)
				snprintf(buf, maxLen, "default (%.2f)", val.GetAsF32());
			else if (isFrameNum)
				snprintf(buf, maxLen, "frame %.1f (%.2f)", sample, val.GetAsF32());
			else
				snprintf(buf, maxLen, "phase %.2f (%.2f)", sample, val.GetAsF32());
			break;
			
		case DC::kBoxableTypeStringId:
			if (sample == 0.0f && !isFrameNum)
				snprintf(buf, maxLen, "default (%s)", DevKitOnly_StringIdToString(val.GetAsStringId()));
			else if (isFrameNum)
				snprintf(buf, maxLen, "frame %.1f (%s)", sample, DevKitOnly_StringIdToString(val.GetAsStringId()));
			else
				snprintf(buf, maxLen, "phase %.2f (%s)", sample, DevKitOnly_StringIdToString(val.GetAsStringId()));
			break;
	}
}

void CharacterDynamicStateDataDebugPrintRowState(const CharacterDynamicStateField& field, int maxNameLen, float fontCharWidth, int row, float currPhase)
{
	char buf[256];

	bool defaultValPrinted = false;
	bool effValid = field.m_numEntriesEff > 0;

	float posX = 640.0f;
	float posY = 100.0f + 12*row;

	Color color = effValid ? kColorGray : kColorYellow;
	g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, StringBuilder<32>("%s:", DevKitOnly_StringIdToString(field.m_name)).c_str(), color, 0.6f) );
	posX += fontCharWidth*(maxNameLen + 3);


	color = effValid ? kColorGray : kColorWhite;
	CharacterDynamicStateDataDebugPrintType(buf, 256, field.m_type);
	g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, buf, color, 0.6f) );
	posX += fontCharWidth*(strlen(buf) + 1);

	if (!effValid)
	{
		color = kColorWhite;
		if (field.m_numEntriesState == 0 || currPhase < field.GetStatePhase(0))
			color = kColorGreen;
	}

	CharacterDynamicStateDataDebugPrintValue(buf, 256, 0.0f, false, field.m_type, field.m_defaultVal);
	g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, buf, color, 0.6f) );
	posX += fontCharWidth*(strlen(buf) + 3);

	for (int iEntry=0; iEntry<field.m_numEntriesState; iEntry++)
	{
		if (!effValid)
		{
			color = kColorWhite;
			if (currPhase >= field.GetStatePhase(iEntry) && (field.m_numEntriesState == iEntry+1 || currPhase < field.GetStatePhase(iEntry+1)))
				color = kColorGreen;
		}

		CharacterDynamicStateDataDebugPrintValue(buf, 256, field.GetStatePhase(iEntry), false, field.m_type, field.m_stateValues[iEntry]);
		g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, buf, color, 0.6f) );
		posX += fontCharWidth*(strlen(buf) + 3);
	}
}

void CharacterDynamicStateDataDebugPrintRowEff(const CharacterDynamicStateField& field, int maxNameLen, float fontCharWidth, int row, float currFrameNum)
{
	char buf[256];

	bool defaultValPrinted = false;

	float posX = 640.0f;
	float posY = 100.0f + 12*row;

	Color color = kColorYellow;
	g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, StringBuilder<32>("%s:", DevKitOnly_StringIdToString(field.m_name)).c_str(), color, 0.6f) );
	posX += fontCharWidth*(maxNameLen + 3);

	color = kColorWhite;
	if (field.m_numEntriesEff == 0 || currFrameNum < field.m_effFrameNums[0])
		color = kColorGreen;

	CharacterDynamicStateDataDebugPrintValue(buf, 256, 0.0f, false, field.m_type, field.m_defaultVal);
	g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, buf, color, 0.6f) );
	posX += fontCharWidth*(strlen(buf) + 3);

	for (int iEntry=0; iEntry<field.m_numEntriesEff; iEntry++)
	{
		color = kColorWhite;
		if (currFrameNum >= field.m_effFrameNums[iEntry] && (field.m_numEntriesEff == iEntry+1 || currFrameNum < field.m_effFrameNums[iEntry+1]))
			color = kColorGreen;

		CharacterDynamicStateDataDebugPrintValue(buf, 256, field.m_effFrameNums[iEntry], true, field.m_type, field.m_effValues[iEntry]);
		g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, buf, color, 0.6f) );
		posX += fontCharWidth*(strlen(buf) + 3);
	}
}

void CharacterDynamicStateDataDebug(const Character* pChar)
{
	STRIP_IN_FINAL_BUILD;

	StringId64 fieldTable[256];
	int fieldTableCount = 0;

	const AnimControl* pAnimControl = pChar->GetAnimControl();

	int maxNameLen = 1;
	const DC::DynamicStateInfoArray* dynamic = LookupAnimStateInfo<DC::DynamicStateInfoArray>(pAnimControl, SID("dynamic"), INVALID_STRING_ID_64);
	if (dynamic)
	{
		for (int iEntry=0; iEntry<dynamic->m_count; iEntry++)
		{
			const DC::DynamicStateInfoEntry* pEntry = &dynamic->m_array[iEntry];
			StringId64 name = pEntry->m_name;
			const char* nameStr = DevKitOnly_StringIdToString(name);
			maxNameLen = Max(maxNameLen, (int)strlen(nameStr));
		}
	}

	// Measure a single character (font should be fixed width)
	float fontCharWidth = GetDebugTextWidth("a", 0.6f);

	const AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(SID("base"));
	
	
	int currRow = 0;



	if (dynamic)
	{
		const AnimStateLayer* pBaseStateLayer = pAnimControl->GetStateLayerById(SID("base"));
		const ArtItemAnim* pPhaseAnim = pBaseStateLayer->CurrentStateInstance()->GetPhaseAnimArtItem().ToArtItem();
		const EffectAnim* pEffectAnim = pPhaseAnim->m_pEffectAnim;

		for (int iEntry=0; iEntry<dynamic->m_count; iEntry++)
		{
			const DC::DynamicStateInfoEntry* pEntry = &dynamic->m_array[iEntry];
			StringId64 name = pEntry->m_name;

			bool foundInTable = false;
			for (int iTable=0; iTable<fieldTableCount; iTable++)
			{
				if (name == fieldTable[iTable])
				{
					foundInTable = true;
					break;
				}
			}

			if (foundInTable)
				continue;

			if (fieldTableCount >= 256)
			{
				ASSERT(false);
				break;
			}

			fieldTable[fieldTableCount++] = name;
		}

		float posX = 640.0f;
		float posY = 100.0f + 12*currRow;

		const char* stateName = pBaseLayer->CurrentState()->m_name.m_string;
		float statePhase = pBaseLayer->CurrentStateInstance()->GetPhase();
		g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, StringBuilder<256>("AnimState: %s, Phase: %.3f", stateName, statePhase).c_str(), kColorWhite, 0.6f) );
		currRow++;

		for (int iField=0; iField<fieldTableCount; iField++)
		{
			CharacterDynamicStateField field;
			CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, fieldTable[iField]);

			CharacterDynamicStateDataDebugPrintRowState(field, maxNameLen, fontCharWidth, currRow, statePhase);
			currRow++;
		}

		currRow++;

		posY = 100.0f + 12*currRow;

		float phaseAnimFrameNum = statePhase * pPhaseAnim->m_pClipData->m_fNumFrameIntervals * 30.0f * pPhaseAnim->m_pClipData->m_secondsPerFrame;
		g_prim.Draw( DebugString2D(Vec2(posX, posY), kDebug2DLegacyCoords, StringBuilder<256>("Phase Anim: %s, Frame Num: %.2f", pPhaseAnim->GetName(), phaseAnimFrameNum).c_str(), kColorWhite, 0.6f) );
		currRow++;

		for (int iField=0; iField<fieldTableCount; iField++)
		{
			CharacterDynamicStateField field;
			CharacterDynamicStateDataGetFieldData(&field, dynamic, pPhaseAnim, pEffectAnim, fieldTable[iField]);

			if (field.m_numEntriesEff > 0)
			{
				CharacterDynamicStateDataDebugPrintRowEff(field, maxNameLen, fontCharWidth, currRow, phaseAnimFrameNum);
				currRow++;
			}
		}
	}

}


/// --------------------------------------------------------------------------------------------------------------- ///
/// CharacterDynamicStateDataBlender
/// --------------------------------------------------------------------------------------------------------------- ///
CharacterDynamicStateDataBlender::CharacterDynamicStateDataBlender(Character* pChr, StringId64 name, float defaultVal)
	: m_pChr(pChr)
	, m_name(name)
	, m_defaultVal(defaultVal)
{
}

float CharacterDynamicStateDataBlender::GetDefaultData() const
{
	return m_defaultVal;
}

bool CharacterDynamicStateDataBlender::GetDataForInstance(const AnimStateInstance *pInstance, float *pDataOut)
{
	BoxedValue val = CharacterDynamicStateData(pInstance, m_name, m_defaultVal);
	float valF = 0.0f;

	// Only valid for bool or float
	if (val.GetType() == DC::kBoxableTypeBool || val.GetType() == DC::kBoxableTypeF32)
		valF = val.GetAsF32();

	*pDataOut = valF;
	return true;
}

float CharacterDynamicStateDataBlender::BlendData(const float &left, const float &right, float masterFade, float animFade, float motionFade)
{
	return Lerp(left, right, animFade);
}
