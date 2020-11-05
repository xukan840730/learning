/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/entry-action-pack.h"

#include "ndlib/text/stringid-util.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/scriptx/h/ai-entry-ap-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
EntryActionPack::EntryActionPack()
	: ActionPack(kEntryActionPack), m_destAnimId(INVALID_STRING_ID_64), m_destAnimPhase(0.0f)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
EntryActionPack::EntryActionPack(const ScriptPointer<DC::EntryApTable>& dcDef,
								 const BoundFrame& bfLoc,
								 StringId64 destAnimId,
								 float destAnimPhase)
	: ActionPack(kEntryActionPack, kOrigin, bfLoc, (const Level*)nullptr)
	, m_def(dcDef)
	, m_destAnimId(destAnimId)
	, m_destAnimPhase(destAnimPhase)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EntryActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	ParentClass::DebugDraw(tt);

	StringBuilder<512> desc;

	desc.append_format("[%s]\n%s @ %0.3f",
					   DevKitOnly_StringIdToString(m_def.GetId()),
					   DevKitOnly_StringIdToString(m_destAnimId),
					   m_destAnimPhase);

	g_prim.Draw(DebugString(GetBoundFrame().GetTranslationWs(), desc.c_str(), kColorWhite, 0.5f), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
I32F EntryActionPack::SelectEntryDefAnims(const DC::EntryApTable* pDcDef, StringId64 charId)
{
	if (!pDcDef)
		return -1;

	for (I32F i = 0; i < pDcDef->m_itemCount; ++i)
	{
		if (!IsStringIdInList(charId, pDcDef->m_entryItems[i].m_characterType))
		{
			continue;
		}

		return i;
	}

	for (I32F i = 0; i < pDcDef->m_itemCount; ++i)
	{
		if (!IsStringIdInList(SID("any"), pDcDef->m_entryItems[i].m_characterType))
		{
			continue;
		}

		return i;
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EntryActionPack::HasNavMeshClearance(const NavLocation& navLoc,
										  bool debugDraw /* = false */,
										  DebugPrimTime tt /* = kPrimDuration1FramePauseable */) const
{
	const NavMesh* pNavMesh = navLoc.ToNavMesh();
	if (pNavMesh && pNavMesh->NavMeshForcesSwim())
	{
		if (debugDraw)
		{
			g_prim.Draw(DebugCoordAxes(GetLocatorWs(), 0.3f, kPrimEnableHiddenLineAlpha), tt);
			StringBuilder<256> desc;
			desc.format("%s\n%s @ %0.3f\nSwim Nav Mesh Not Supported",
						DevKitOnly_StringIdToString(m_def.GetId()),
						DevKitOnly_StringIdToString(m_destAnimId),
						m_destAnimPhase);
			const Point regPosWs = GetRegistrationPointWs();
			g_prim.Draw(DebugString(regPosWs, desc.c_str(), kColorRed, 0.8f), tt);
		}

		return false;
	}

	return ParentClass::HasNavMeshClearance(navLoc, debugDraw, tt);
}
