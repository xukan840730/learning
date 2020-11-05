/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/anim/motion-matching/motion-matching-manager.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/io/fileio.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/debug-selection.h"

#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/script/script-menu.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#define MAX_MOTION_SETS 512

MotionMatchingManager* g_pMotionMatchingMgr = nullptr;

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingManager::MotionMatchingManager() :
	ScriptObserver(
#if ENABLE_SCRIPT_OBSERVER_FILE_LINE_FUNC
		FILE_LINE_FUNC,
#endif
		SID("motion-matching-set")
	)
{
	AllocateJanitor alloc(kAllocAnimation, FILE_LINE_FUNC);

	m_dcSets.Init(MAX_MOTION_SETS, FILE_LINE_FUNC);
	m_itemTable.Init(MAX_MOTION_SETS, FILE_LINE_FUNC);
	
	ScriptManager::RegisterObserver(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingManager::OnSymbolImported(StringId64 moduleId,
											 StringId64 symbol,
											 StringId64 type,
											 const void* pData,
											 const void* pOldData,
											 Memory::Context allocContext)
{
	if (type == SID("motion-matching-set"))
	{
		AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

		if (!m_dcSets.IsFull())
		{
			m_dcSets.PushBack(MMSetPtr(symbol));
			UpdateMenu();
		}
		else
		{
			MsgErr("Failed to register new motion matching set '%s' because we are full!\n",
				   DevKitOnly_StringIdToString(symbol));
			MsgConErr("Failed to register new motion matching set '%s' because we are full!\n",
					  DevKitOnly_StringIdToString(symbol));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingManager::OnSymbolUnloaded(StringId64 moduleId,
											 StringId64 symbol,
											 StringId64 type,
											 const void* pData,
											 Memory::Context allocContext)
{
	if (type == SID("motion-matching-set"))
	{
		AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

		for (int i = m_dcSets.Size() - 1; i >= 0; i--)
		{
			if (m_dcSets[i].GetId() == symbol)
			{
				m_dcSets.EraseAndMoveLast(&m_dcSets[i]);
				UpdateMenu();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
DMENU::Menu* MotionMatchingManager::CreateMenu()
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Motion Matching Sets");
	m_pDebugMenu = pMenu;

	pMenu->SetCleanCallback(InitSetsMenu);
	pMenu->MarkAsDirty();

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingManager::DoesMotionMatchingSetExist(StringId64 id) const
{
	PROFILE_AUTO(Animation);

	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	for (const MMSetPtr& ptr : m_dcSets)
	{
		if (ptr.GetId() == id)
		{
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemMotionMatchingSet* MotionMatchingManager::LookupArtItemMotionMatchingSetById(StringId64 id) const
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	const MotionMatchingItemTable::ConstIterator it = m_itemTable.Find(id);
	if (it != m_itemTable.End())
	{
		return it->m_data;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const MotionMatchingSet* MotionMatchingManager::LookupMotionMatchingSetById(StringId64 id) const
{
	if (const ArtItemMotionMatchingSet* pItem = LookupArtItemMotionMatchingSetById(id))
	{
		return static_cast<const MotionMatchingSet*>(&pItem->m_set);
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingManager::Register(const ArtItemMotionMatchingSet* pSetItem)
{
	{
		AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

		m_itemTable.Add(pSetItem->GetNameId(), pSetItem);
	}

	const MotionMatchingSet* pSet = (const MotionMatchingSet*)&pSetItem->m_set;

	{
		const size_t numPose = pSet->GetNumPoseDimensions();
		const size_t numGoal = pSet->GetNumGoalDimensions();
		const size_t numExtra = pSet->GetNumExtraDimensions();
		const size_t numTotal = pSet->GetTotalNumDimensions();

		ANIM_ASSERTF(numTotal <= MotionMatchingSet::kMaxVectorDim,
					 ("Motion Matching Set '%s' exceeds our maximum anim vector size (%d > %d)",
					  pSetItem->GetName(),
					  numTotal,
					  MotionMatchingSet::kMaxVectorDim));

		ANIM_ASSERTF(numPose + numGoal + numExtra == numTotal,
					 ("Motion Matching Set '%s' has invalid dimensions (%d %d %d != %d)",
					  pSetItem->GetName(),
					  numPose,
					  numGoal,
					  numExtra,
					  numTotal));
	}

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_validateSampleIndices))
	{
		pSet->ValidateSampleIndices(pSetItem->GetName());
	}

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_validateEffData))
	{
		static_cast<const MotionMatchingSet&>(pSetItem->m_set).ValidateEffData();
	}

	UpdateMenu();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingManager::Unregister(const ArtItemMotionMatchingSet* pSetItem)
{
	{
		AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

		MotionMatchingItemTable::Iterator it = m_itemTable.Find(pSetItem->GetNameId());
		if (it != m_itemTable.End())
		{
			m_itemTable.Erase(it);
		}
	}

	UpdateMenu();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreatePoseMenu(MMPose* pPose)
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Pose");
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Master Weight", &pPose->m_masterWeight, DMENU::FloatRange(0.0f, 100.0f)));
	for (int i = 0; i < pPose->m_numBodies; i++)
	{
		const char* pBodyName = pPose->m_aBodies[i].m_isCenterOfMass
									? "center of mass"
									: DevKitOnly_StringIdToString(pPose->m_aBodies[i].m_jointId);
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat(StringBuilder<256>("%s Pos Weight", pBodyName).c_str(),
											  &pPose->m_aBodies[i].m_positionWeight,
											  DMENU::FloatRange(0.0f, 100.0f)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat(StringBuilder<256>("%s Vel Weight", pBodyName).c_str(),
											  &pPose->m_aBodies[i].m_velocityWeight,
											  DMENU::FloatRange(0.0f, 100.0f)));
	}
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Facing Weight", &pPose->m_facingWeight, DMENU::FloatRange(0.0f, 100.0f)));
	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreateGoalsMenu(MMGoals* pGoals)
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Goals");

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Master Weight", &pGoals->m_masterWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Position Weight", &pGoals->m_positionWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Velocity Weight", &pGoals->m_velocityWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Directional Weight", &pGoals->m_directionalWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Interim Directional Weight", &pGoals->m_interimDirectionalWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Yaw Speed Weight", &pGoals->m_yawSpeedWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Prev Traj Weight", &pGoals->m_prevTrajWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Stopping Face Dist", &pGoals->m_stoppingFaceDist, DMENU::FloatRange(0.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Bias Weight", &pGoals->m_animBiasWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Grouping Weight", &pGoals->m_groupingWeight, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(0.01f, 0.1f)));

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* PopulateToolsDataMenu(DMENU::Menu* pMenu, MotionMatchingSetDef* pSet, const StringId64 setId)
{
	MMSettings* pSettings = pSet->m_pSettings;

	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Pose...", CreatePoseMenu(&pSet->m_pSettings->m_pose)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Goals...", CreateGoalsMenu(&pSet->m_pSettings->m_goals)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Use Normalized Scales", &pSet->m_pSettings->m_useNormalizedScales));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Use Normalized Data", &pSet->m_pSettings->m_useNormalizedData));

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DumpMotionMatchingSetToJson(DMENU::Item& item, DMENU::Message m)
{
	bool res = false;

	if (m == DMENU::kExecute)
	{
		const StringId64 nameId = FromBlindPtr64(item.m_pBlindData);

		if (const MotionMatchingSet* pSet = g_pMotionMatchingMgr->LookupMotionMatchingSetById(nameId))
		{
			char filename[512];
			const char* pName = DevKitOnly_StringIdToString(nameId);
			SYSTEM_ASSERT(sizeof(filename) > strlen(pName));

			// filter out characters to make nicer file names
			std::remove_copy_if(pName, pName + strlen(pName) + 1, filename, [](char c) {
				if (c == '-')
					return false;
				if (c >= '0' && c <= '9')
					return false;
				if (c >= 'a' && c <= 'z')
					return false;
				if (c >= 'A' && c <= 'Z')
					return false;
				if (c == 0)
					return false;
				return true;
			});

			StringBuilder<512> dirPath;
			dirPath.append_format("%s%s/motion-matching-json/",
								  FILE_SYSTEM_ROOT,
								  EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
			StringBuilder<512> filePath;
			filePath.append_format("%s%s.json",
								   dirPath.c_str(),
								   filename);

			Err err = FileIO::makeDirPath(dirPath.c_str());
			if (err.Failed())
			{
				MsgErr("Failed to create directory to write '%s'\n", filePath.c_str());
			}
			else if (pSet->DumpToJson(nameId, filePath.c_str()))
			{
				MsgOut("Wrote MM Set '%s' to %s\n", DevKitOnly_StringIdToString(nameId), filePath.c_str());
			}
			else
			{
				MsgErr("Failed writing MM Set '%s' to %s\n", DevKitOnly_StringIdToString(nameId), filePath.c_str());
			}
			
			res = true;
		}
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void MotionMatchingManager::InitSetMenu(DMENU::Component* pComponent)
{
	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);
	DMENU::Menu* pSetMenu = static_cast<DMENU::Menu*>(pComponent);
	pSetMenu->DeleteAllItems();

	const StringId64 setId = FromBlindPtr64(pComponent->m_pBlindData);

	const MotionMatchingItemTable::ConstIterator itr = g_pMotionMatchingMgr->m_itemTable.Find(setId);
	
	if (itr != g_pMotionMatchingMgr->m_itemTable.End())
	{
		const MotionMatchingItemTable::HashTableNode* pNode = *itr;
		const ArtItemMotionMatchingSet* pArtItemMm = pNode->m_data;
		const MotionMatchingSetDef* pSetDef		   = &pArtItemMm->m_set;

		PopulateToolsDataMenu(pSetMenu, const_cast<MotionMatchingSetDef*>(pSetDef), setId);

		pSetMenu->PushBackItem(NDI_NEW DMENU::ItemDivider);

		U32F numFound = 0;

		for (const MMSetPtr& set : g_pMotionMatchingMgr->m_dcSets)
		{
			DC::MotionMatchingSet* pDcMmSet = const_cast<DC::MotionMatchingSet*>(set.GetTyped());

			if (pDcMmSet->m_motionMatchId != setId)
				continue;

			++numFound;

			const StringId64 dcSetId = set.GetId();

			DMENU::Menu* pScriptMenu = ScriptMenu::MakeDCMenu(DevKitOnly_StringIdToString(dcSetId),
															  pDcMmSet,
															  SID("motion-matching-set"));

			StringBuilder<128> smName("%s...", DevKitOnly_StringIdToString(dcSetId));

			pSetMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu(smName.c_str(), pScriptMenu));
		}

		if (numFound > 0)
		{
			pSetMenu->PushBackItem(NDI_NEW DMENU::ItemDivider);
		}
	}

	pSetMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Export to JSON", DumpMotionMatchingSetToJson, ToBlindPtr(setId)));
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void MotionMatchingManager::InitSetsMenu(DMENU::Component* pComponent)
{
	AtomicLockJanitor accessLock(&g_pMotionMatchingMgr->m_accessLock, FILE_LINE_FUNC);

	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

	DMENU::Menu* pSetsMenu = static_cast<DMENU::Menu*>(pComponent);
	pSetsMenu->DeleteAllItems();

	for (const MotionMatchingItemTable::HashTableNode* pNode : g_pMotionMatchingMgr->m_itemTable)
	{
		const StringId64 setId = pNode->m_key;
		DMENU::Menu* pSetMenu = NDI_NEW DMENU::Menu(DevKitOnly_StringIdToString(setId));
		pSetMenu->m_pBlindData = ToBlindPtr(setId);
		pSetMenu->SetCleanCallback(InitSetMenu);
		pSetMenu->MarkAsDirty();

		pSetsMenu->PushSortedItem(NDI_NEW DMENU::ItemSubmenu(DevKitOnly_StringIdToString(pNode->m_key), pSetMenu));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingManager::UpdateMenu()
{
	STRIP_IN_FINAL_BUILD;

	if (m_pDebugMenu)
	{
		m_pDebugMenu->MarkAsDirty();
	}

	if (m_pSelectAnimMenu)
	{
		m_pSelectAnimMenu->MarkAsDirty();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool OnDebugAnimReset(DMENU::Item& item, DMENU::Message msg)
{
	if (msg != DMENU::kExecute)
		return false;

	g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId = INVALID_STRING_ID_64;
	g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId = INVALID_SKELETON_ID;
	g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimMirror = false;
	g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame	 = -1;

	if (item.m_pParent)
	{
		if (DMENU::Component* pEditFrame = item.m_pParent->FindByName("Debug Anim Frame"))
		{
			pEditFrame->SendMessage(DMENU::kRefresh);
		}
		if (DMENU::Component* pEditMirror = item.m_pParent->FindByName("Debug Anim Mirror"))
		{
			pEditMirror->SendMessage(DMENU::kRefresh);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool OnDebugAnimUseCurrentSample(DMENU::Item& item, DMENU::Message msg)
{
	if (msg != DMENU::kExecute)
		return false;

	const NdGameObject* pSelGo = DebugSelection::Get().GetSelectedGameObject();
	if (!pSelGo)
		return false;

	const AnimControl* pAnimControl = pSelGo->GetAnimControl();
	if (!pAnimControl)
		return false;

	bool res = false;

	if (const AnimStateLayer* pBaseStateLayer = pAnimControl->GetBaseStateLayer())
	{
		const AnimStateInstance* pCurInstance = pBaseStateLayer->CurrentStateInstance();
		if (pCurInstance)
		{
			const ArtItemAnim* pAnim = pCurInstance->GetPhaseAnimArtItem().ToArtItem();

			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId	 = pAnim ? pAnim->GetNameId() : INVALID_STRING_ID_64;
			g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId	 = pAnim ? pAnim->m_skelID : INVALID_SKELETON_ID;
			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimMirror = pCurInstance->IsFlipped();
			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame	 = pCurInstance->GetMayaFrame();
			
			res = true;
		}
	}
	else if (const AnimSimpleLayer* pBaseSimpleLayer = pAnimControl->GetSimpleLayerById(SID("base")))
	{
		if (const AnimSimpleInstance* pCurInstance = pBaseSimpleLayer->CurrentInstance())
		{
			const ArtItemAnim* pAnim = pCurInstance->GetAnim().ToArtItem();

			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId	 = pAnim ? pAnim->GetNameId() : INVALID_STRING_ID_64;
			g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId	 = pAnim ? pAnim->m_skelID : INVALID_SKELETON_ID;
			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimMirror = pCurInstance->IsFlipped();
			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame	 = pCurInstance->GetMayaFrame();

			res = true;
		}
	}

	if (res && item.m_pParent)
	{
		if (DMENU::Component* pEditFrame = item.m_pParent->FindByName("Debug Anim Frame"))
		{
			pEditFrame->SendMessage(DMENU::kRefresh);
		}
		if (DMENU::Component* pEditMirror = item.m_pParent->FindByName("Debug Anim Mirror"))
		{
			pEditMirror->SendMessage(DMENU::kRefresh);
		}
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I64 OnEditDebugAnimFrame(DMENU::Item& item, DMENU::Message msg, I64 desiredVal, I64 prevVal)
{
	if (msg != DMENU::kExecute)
	{
		return g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame;
	}

	I64 updatedVal = desiredVal;

	const ArtItemAnim* pAnim = ResourceTable::LookupAnim(g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId,
														 g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId).ToArtItem();
	if (pAnim)
	{
		const I64 maxFrame = (I64)GetMayaFrameFromClip(pAnim->m_pClipData, 1.0f);
		
		if (updatedVal > maxFrame)
		{
			updatedVal = -1;
		}
		else if (updatedVal < -1)
		{
			updatedVal = maxFrame;
		}
	}
	else
	{
		updatedVal = 0;
	}

	g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimFrame = updatedVal;

	return updatedVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MenuItemSelectMmAnim : public DMENU::ItemBool
{
public:
	using ParentClass = DMENU::ItemBool;

	MenuItemSelectMmAnim(StringId64 animId, SkeletonId skelId, U32 hierarchyId)
		: ParentClass(DevKitOnly_StringIdToString(animId), OnExecute, this)
		, m_animId(animId)
		, m_skelId(skelId)
		, m_hierarchyId(hierarchyId)
	{
	}

	virtual Component* CreateCopy() override
	{
		return NDI_NEW MenuItemSelectMmAnim(m_animId, m_skelId, m_hierarchyId);
	}

private:
	static bool OnExecute(DMENU::Item& item, DMENU::Message msg)
	{
		bool res = false;

		if (msg == DMENU::kExecute)
		{
			MenuItemSelectMmAnim* pMmItem = static_cast<MenuItemSelectMmAnim*>(&item);

			const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(pMmItem->m_skelId,
																   pMmItem->m_hierarchyId,
																   pMmItem->m_animId)
										   .ToArtItem();

			g_motionMatchingOptions.m_drawOptions.m_debugExtraAnimId = pAnim ? pAnim->GetNameId() : INVALID_STRING_ID_64;
			g_motionMatchingOptions.m_drawOptions.m_debugExtraSkelId = pAnim ? pAnim->m_skelID : INVALID_SKELETON_ID;

			res = true;
		}

		return res;
	}

	StringId64 m_animId;
	SkeletonId m_skelId;
	U32 m_hierarchyId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static I32 CompareStringIds(const StringId64& a, const StringId64& b)
{
	return strcmp(DevKitOnly_StringIdToString(a), DevKitOnly_StringIdToString(b));
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreateSelectDebugAnimMenuFromSet(const MotionMatchingSetDef& set, StringId64 setNameId)
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu(DevKitOnly_StringIdToString(setNameId));

	const size_t animCount = set.m_sampleTable->m_numAnims;

	if (animCount <= 0)
	{
		return pMenu;
	}

	StringId64* pAnimIds = NDI_NEW(kAllocSingleFrame) StringId64[animCount];

	for (U32F i = 0; i < animCount; ++i)
	{
		pAnimIds[i] = set.m_sampleTable->m_aAnimIds[i];
	}

	QuickSort(pAnimIds, animCount, CompareStringIds);

	const F32 fAnimCount = static_cast<F32>(animCount);
	static const F32 kAnimToGroupRatio = 2.0f;
	const F32 fIdealAnimsPerGroup = Sqrt(kAnimToGroupRatio * fAnimCount);	// non-integral!
	const F32 fGroupCount = Ceil(fAnimCount / fIdealAnimsPerGroup);
	U32F groupCount = static_cast<U32>(fGroupCount);
	U32F animsPerGroup = static_cast<U32>(Floor(fIdealAnimsPerGroup));

	static const U32 kMinGroupCount = 3;
	static const U32 kMaxGroupCount = 45;
	if (groupCount < kMinGroupCount)
	{
		// Put them all into one group.
		groupCount = 1;
		animsPerGroup = animCount;
	}
	else if (groupCount > kMaxGroupCount)
	{
		groupCount = kMaxGroupCount;
		animsPerGroup = U32(Ceil(fAnimCount / groupCount));
	}

	if (groupCount > 0)
	{
		for (U32F iGroup = 0; iGroup < groupCount; ++iGroup)
		{
			U32F iStart = iGroup * animsPerGroup;
			U32F iEnd = (iGroup == groupCount - 1) ? animCount : iStart + animsPerGroup;
			if (iEnd > animCount)
			{
				iEnd = animCount;
			}

			if (iEnd > iStart)
			{
				StringBuilder<256> desc;
				desc.append_format("%-30.30s <TO> %-30.30s",
								   DevKitOnly_StringIdToString(pAnimIds[iStart]),
								   DevKitOnly_StringIdToString(pAnimIds[iEnd - 1]));

				DMENU::Menu* pGroupMenu = NDI_NEW DMENU::Menu(desc.c_str());
				desc.append(" ...");
				pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu(desc.c_str(), pGroupMenu));

				for (U32F iAnim = iStart; iAnim < iEnd; ++iAnim)
				{
					const StringId64 animId = pAnimIds[iAnim];
					pGroupMenu->PushBackItem(NDI_NEW MenuItemSelectMmAnim(animId, set.m_skelId, set.m_hierarchyId));
				}
			}
		}
	}
	else
	{
		for (U32F iAnim = 0; iAnim < groupCount; ++iAnim)
		{
			const StringId64 animId = pAnimIds[iAnim];
			pMenu->PushBackItem(NDI_NEW MenuItemSelectMmAnim(animId, set.m_skelId, set.m_hierarchyId));
		}
	}

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void MotionMatchingManager::PopulateSelectAnimMenu(DMENU::Menu* pMenu)
{
	AtomicLockJanitor accessLock(&g_pMotionMatchingMgr->m_accessLock, FILE_LINE_FUNC);

	for (const MotionMatchingItemTable::HashTableNode* pNode : g_pMotionMatchingMgr->m_itemTable)
	{
		const MotionMatchingSetDef& set = pNode->m_data->m_set;
		
		DMENU::Menu* pSetMenu = CreateSelectDebugAnimMenuFromSet(set, pNode->m_key);

		pMenu->PushSortedItem(NDI_NEW DMENU::ItemSubmenu(DevKitOnly_StringIdToString(pNode->m_key), pSetMenu));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void MotionMatchingManager::RepopulateSelectAnimMenu(DMENU::Component* pComponent)
{
	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

	DMENU::Menu* pMenu = (DMENU::Menu*)pComponent;
	pMenu->DeleteAllItems();

	PopulateSelectAnimMenu(pMenu);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DMENU::Menu* MotionMatchingManager::CreateSelectDebugAnimMenu()
{
	m_pSelectAnimMenu = NDI_NEW DMENU::Menu("Select Debug Anim");

	m_pSelectAnimMenu->SetCleanCallback(RepopulateSelectAnimMenu);

	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

	PopulateSelectAnimMenu(m_pSelectAnimMenu);

	return m_pSelectAnimMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreateMotionMatchingDebuggerMenu()
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Motion Matching Debugger");
	
	MotionMatchingDrawOptions& opts = g_motionMatchingOptions.m_drawOptions;

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Joint Names", &opts.m_drawJointNames));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Poses", &opts.m_drawPoses));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Trajectories", &opts.m_drawTrajectories));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Print Pose Values", &opts.m_printPoseValues));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Use Stick For Debug Input", &opts.m_useStickForDebugInput));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Print Active Layers", &opts.m_printActiveLayers));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider);

	DMENU::Menu* pSelAnimMenu = g_pMotionMatchingMgr->CreateSelectDebugAnimMenu();

	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Select Debug Anim ...", pSelAnimMenu));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFunction("Debug Anim Use Current Sample", OnDebugAnimUseCurrentSample));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Debug Anim Frame", OnEditDebugAnimFrame, &opts.m_debugExtraAnimFrame, DMENU::IntRange(-2, 4096), DMENU::IntSteps(1, 10)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Anim Mirror", &opts.m_debugExtraAnimMirror));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFunction("Debug Anim Clear Selection", OnDebugAnimReset));

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreateMotionMatchingDrawMenu()
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Motion Matching Draw");

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Trajectory Samples", &g_motionMatchingOptions.m_drawOptions.m_drawTrajectorySamples));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw End Trajectory Position", &g_motionMatchingOptions.m_drawOptions.m_drawTrajEndPos));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Motion Model", &g_motionMatchingOptions.m_drawOptions.m_drawMotionModel));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Match Locator", &g_motionMatchingOptions.m_drawOptions.m_drawMatchLocator));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Path Corners", &g_motionMatchingOptions.m_drawOptions.m_drawPathCorners));

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::Menu* CreateMotionMatchingProceduralMenu()
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Motion Matching Procedural Options");

	MotionMatchingProceduralOptions& opts = g_motionMatchingOptions.m_proceduralOptions;

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Procedural Motion", &opts.m_drawProceduralMotion));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Procedural LookAhead Scale",
												 &opts.m_lookAheadTimeScale,
												 DMENU::FloatRange(-1.0f, 1.0f),
												 DMENU::FloatSteps(0.1f, 0.25f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Procedural Align Rotation", &opts.m_enableProceduralAlignRotation));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Procedural Align Translation", &opts.m_enableProceduralAlignTranslation));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Procedural Anim Speed Scale", &opts.m_enableAnimSpeedScaling));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Strafing IK", &opts.m_enableStrafingIk));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Strafe Lock Angle", &opts.m_disableStrafeLockAngle));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Clamp Distance Reduct. Alpha", &opts.m_clampDistReductionAlpha, DMENU::FloatRange(0.0f, 20.0f), DMENU::FloatSteps(1.0f, 5.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Clamp Distance Reduct. Scale", &opts.m_clampDistReductionScale, DMENU::FloatRange(0.0f, 2.0f), DMENU::FloatSteps(0.1f, 0.25f)));

	return pMenu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool NextMotionMatchingDebugIndex(DMENU::Item&, DMENU::Message m)
{
	if (m == DMENU::kExecute)
	{
		g_motionMatchingOptions.m_drawOptions.m_debugIndex++;
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool PrevMotionMatchingDebugIndex(DMENU::Item&, DMENU::Message m)
{
	if (m == DMENU::kExecute)
	{
		g_motionMatchingOptions.m_drawOptions.m_debugIndex--;
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DMENU::Menu* CreateMotionMatchingMenu()
{
	DMENU::Menu* pMenu = NDI_NEW DMENU::Menu("Motion Matching");

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Locomotion", &g_motionMatchingOptions.m_drawOptions.m_drawLocomotion));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Debug Index", &g_motionMatchingOptions.m_drawOptions.m_debugIndex));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Prev Debug Index", PrevMotionMatchingDebugIndex));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Next Debug Index", NextMotionMatchingDebugIndex));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Num Debug Samples", &g_motionMatchingOptions.m_drawOptions.m_numSamplesToDebug));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Debugger...", CreateMotionMatchingDebuggerMenu()));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Precomputed Indices", &g_motionMatchingOptions.m_disablePrecomputedIndices));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Validate EFF Data", &g_motionMatchingOptions.m_validateEffData));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Validate Sample Indices", &g_motionMatchingOptions.m_validateSampleIndices));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Force External Pose Rating", &g_motionMatchingOptions.m_forceExternalPoseRating));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Match Loc Speed Blending", &g_motionMatchingOptions.m_disableMatchLocSpeedBlending));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Trajectory Sample Resolution", &g_motionMatchingOptions.m_trajectorySampleResolution, DMENU::IntRange(1, 100), DMENU::IntSteps(1, 10)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Mm Nav Mesh Adjustment", &g_motionMatchingOptions.m_disableMmNavMeshAdjust));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Mm Ground Adjustment", &g_motionMatchingOptions.m_disableMmGroundAdjust));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Mm NPC Leg IK", &g_motionMatchingOptions.m_disableMmNpcLegIk));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Target Pose Blending", &g_motionMatchingOptions.m_disableMmTargetPoseBlending));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Allow Proc. Align From Foreign Sets", &g_motionMatchingOptions.m_allowProceduralAlignsFromForeignSets));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Target Pose Blend Factor", &g_motionMatchingOptions.m_targetPoseBlendFactor, DMENU::FloatRange(0.0f, 1.0f), DMENU::FloatSteps(0.1f, 0.3f)));
	
	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Force Off Path", &g_motionMatchingOptions.m_forceOffPath));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Pause When Off Path", &g_motionMatchingOptions.m_pauseWhenOffPath));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Turn Rate Reset Cos Angle", &g_motionMatchingOptions.m_turnRateResetCosAngle, DMENU::FloatRange(0.0f, 1.0f), DMENU::FloatSteps(0.025f, 0.1f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Path Corner Gather Angle (deg)", &g_motionMatchingOptions.m_pathCornerGatherAngleDeg, DMENU::FloatRange(0.0f, 360.0f), DMENU::FloatSteps(1.0f, 10.f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Retarget Scaling", &g_motionMatchingOptions.m_disableRetargetScaling));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Retarget Scale Override", &g_motionMatchingOptions.m_retargetScaleOverride, DMENU::FloatRange(-1.0f, 5.0f), DMENU::FloatSteps(0.1f, 0.25f)));

	static DMENU::ItemEnumPair s_mirrorEnum[] =
	{
		DMENU::ItemEnumPair("<Use Set>", (I64)MMMirrorMode::kInvalid),
		DMENU::ItemEnumPair("Disabled", (I64)MMMirrorMode::kNone),
		DMENU::ItemEnumPair("Allow", (I64)MMMirrorMode::kAllow),
		DMENU::ItemEnumPair("Forced", (I64)MMMirrorMode::kForced),
		DMENU::ItemEnumPair()
	};
	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("Override Mirror Mode", s_mirrorEnum, DMENU::EditInt, &g_motionMatchingOptions.m_overrideMirrorMode));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Sets...", g_pMotionMatchingMgr->CreateMenu()));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Procedural Options...", CreateMotionMatchingProceduralMenu()));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Debug Draw...", CreateMotionMatchingDrawMenu()));

	return pMenu;
}
