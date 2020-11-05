/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-path-node-mgr.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg-mem.h"
#include "corelib/util/msg.h"

#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/text/string-builder.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavPathNodeMgr g_navPathNodeMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
#define ENABLE_PNM_ASSERTS 0
#define ENABLE_PNM_LOG 0

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_PNM_ASSERTS
#define PNM_ASSERT(x)                                                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		if ((!(x)) && g_navPathNodeMgr.m_pDebugLog)                                                                    \
			g_navPathNodeMgr.m_pDebugLog->Dump(GetMsgOutput(kMsgOut));                                                 \
		ALWAYS_ASSERT(x);                                                                                              \
	} while (false)
#define PNM_ASSERTF(x, y)                                                                                              \
	do                                                                                                                 \
	{                                                                                                                  \
		if ((!(x)) && g_navPathNodeMgr.m_pDebugLog)                                                                    \
			g_navPathNodeMgr.m_pDebugLog->Dump(GetMsgOutput(kMsgOut));                                                 \
		ALWAYS_ASSERTF(x, y);                                                                                          \
	} while (false)
#else
#define PNM_ASSERT ASSERT
#define PNM_ASSERTF ASSERTF
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_PNM_LOG
#define PnmLog(str, ...)                                                                                               \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_navPathNodeMgr.m_pDebugLog)                                                                              \
			g_navPathNodeMgr.m_pDebugLog->Printf(str, __VA_ARGS__);                                                    \
	} while (false)
#else
static inline void PnmLogNULL(const char*, ...) {}
#define PnmLog PnmLogNULL
#endif

/************************************************************************/
/* NavPathNode                                                          */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNode::AsActionPack(ActionPack* pAp,
							   Point_arg posPs,
							   const NavLocation& registrationLoc,
							   float pathCost,
							   bool exitNode)
{
	NAV_ASSERT(registrationLoc.IsValid());

	PnmLog("Node '%d' created as ActionPack 0x%p '%s'\n", int(m_id), pAp, pAp->GetName());

	switch (registrationLoc.GetType())
	{
	case NavLocation::Type::kNavPoly:
		m_navMgrId = registrationLoc.GetNavManagerId();
		NAV_ASSERT(m_navMgrId.IsValid());
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		m_hNavLedge = registrationLoc.GetNavLedgeHandle();
		NAV_ASSERT(m_hNavLedge.IsValid());
		break;
#endif

	default:
		m_navMgrId.Invalidate();
#if ENABLE_NAV_LEDGES
		m_hNavLedge.Invalidate();
#endif
		break;
	}

	m_nodeType	   = exitNode ? NavPathNode::kNodeTypeActionPackExit : NavPathNode::kNodeTypeActionPackEnter;
	m_blockageMask = Nav::kStaticBlockageMaskNone;
	m_hActionPack  = pAp;

	SetPositionPs(posPs);
	SetExtraCost(pathCost);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNode::AsActionPack(ActionPack* pAp,
							   Point_arg posPs,
							   const NavManagerId& navMgrId,
							   float pathCost,
							   bool exitNode)
{
	NAV_ASSERT(navMgrId.IsValid());

	PnmLog("Node '%d' created as ActionPack 0x%p '%s'\n", int(m_id), pAp, pAp->GetName());

	m_nodeType	   = exitNode ? NavPathNode::kNodeTypeActionPackExit : NavPathNode::kNodeTypeActionPackEnter;
	m_blockageMask = Nav::kStaticBlockageMaskNone;
	m_hActionPack  = pAp;
	m_navMgrId	   = navMgrId;

	SetPositionPs(posPs);
	SetExtraCost(pathCost);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNode::AsNavPoly(const NavPoly& poly, Point_arg posPs)
{
	const NavPolyHandle hPoly = NavPolyHandle(&poly);
	const NavManagerId mid = hPoly.GetManagerId();

	PnmLog("Node '%d' created as NavPoly '%d+%d+%d'\n", int(m_id), mid.m_navMeshIndex, mid.m_uniqueId, mid.m_iPoly);

	m_navMgrId				= poly.GetNavManagerId();
	m_nodeType				= NavPathNode::kNodeTypePoly;
	m_blockageMask			= poly.GetBlockageMask();
	m_hActionPack			= ActionPackHandle();
#if ENABLE_NAV_LEDGES
	m_hNavLedge.Invalidate();
#endif

	NAV_ASSERT(m_navMgrId.IsValid());

	SetPositionPs(posPs);
	SetExtraCost(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNode::AsNavPolyEx(const NavPolyEx* pPolyEx, Point_arg posPs)
{
	PNM_ASSERT(pPolyEx);
	if (!pPolyEx)
		return;

	const NavPolyExHandle hPolyEx = NavPolyExHandle(pPolyEx);
	const NavManagerId mid = hPolyEx.GetManagerId();

	PnmLog("Node '%d' created as NavPolyEx '%d+%d+%d+%d'\n",
		   int(m_id),
		   mid.m_navMeshIndex,
		   mid.m_uniqueId,
		   mid.m_iPoly,
		   mid.m_iPolyEx);

	m_navMgrId				= pPolyEx->GetNavManagerId();
	m_nodeType				= NavPathNode::kNodeTypePolyEx;
	m_blockageMask			= pPolyEx->GetSourceBlockageMask();
	m_hActionPack			= ActionPackHandle();
#if ENABLE_NAV_LEDGES
	m_hNavLedge.Invalidate();
#endif

	NAV_ASSERT(m_navMgrId.IsValid());

	SetPositionPs(posPs);
	SetExtraCost(0.0f);
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNode::AsNavLedge(const NavLedge* pLedge, Point_arg posPs)
{
	m_nodeType				= NavPathNode::kNodeTypeNavLedge;
	m_hNavLedge				= pLedge;
	m_blockageMask			= Nav::kStaticBlockageMaskNone;
	m_traversalSkillMask	= 0;
	m_factionIdMask			= 0;
	m_navMgrId.Invalidate();

	SetPositionPs(posPs);
	SetExtraCost(0.0f);
}
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPathNode::WorldToParent(Point posWs) const
{
	Point posPs = posWs;

	switch (m_nodeType)
	{
	case NavPathNode::kNodeTypePoly:
	case NavPathNode::kNodeTypePolyEx:
		{
			if (const NavMesh* pMesh = NavMeshHandle(m_navMgrId).ToNavMesh())
			{
				posPs = pMesh->WorldToParent(posWs);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavPathNode::kNodeTypeNavLedge:
		{
			if (const NavLedgeGraph* pGraph = m_hNavLedge.ToLedgeGraph())
			{
				posPs = pGraph->WorldToParent(posWs);
			}
		}
		break;
#endif // ENABLE_NAV_LEDGES
	case NavPathNode::kNodeTypeActionPackEnter:
	case NavPathNode::kNodeTypeActionPackExit:
		{
			if (const NavMesh* pMesh = NavMeshHandle(m_navMgrId).ToNavMesh())
			{
				posPs = pMesh->WorldToParent(posWs);
			}
#if ENABLE_NAV_LEDGES
			else if (const NavLedgeGraph* pGraph = m_hNavLedge.ToLedgeGraph())
			{
				posPs = pGraph->WorldToParent(posWs);
			}
#endif // ENABLE_NAV_LEDGES
		}
		break;
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPathNode::ParentToWorld(Point posPs) const
{
	Point posWs = posPs;

	switch (m_nodeType)
	{
	case NavPathNode::kNodeTypePoly:
	case NavPathNode::kNodeTypePolyEx:
		{
			if (const NavMesh* pMesh = NavMeshHandle(m_navMgrId).ToNavMesh())
			{
				posWs = pMesh->ParentToWorld(posPs);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavPathNode::kNodeTypeNavLedge:
		{
			if (const NavLedgeGraph* pGraph = m_hNavLedge.ToLedgeGraph())
			{
				posWs = pGraph->ParentToWorld(posPs);
			}
		}
		break;
#endif // ENABLE_NAV_LEDGES

	case NavPathNode::kNodeTypeActionPackEnter:
	case NavPathNode::kNodeTypeActionPackExit:
		{
			if (const NavMesh* pMesh = NavMeshHandle(m_navMgrId).ToNavMesh())
			{
				posWs = pMesh->ParentToWorld(posPs);
			}
#if ENABLE_NAV_LEDGES
			else if (const NavLedgeGraph* pGraph = m_hNavLedge.ToLedgeGraph())
			{
				posWs = pGraph->ParentToWorld(posPs);
			}
#endif // ENABLE_NAV_LEDGES
		}
		break;
	}

	return posWs;
}

/************************************************************************/
/* NavPathNodeMgr                                                       */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
NavPathNodeMgr::NavPathNodeMgr()
{
	m_maxNodeCount = 0;
	m_maxLinkCount = 0;
	m_nodeAllocCount = 0;
	m_linkAllocCount = 0;
	m_maxNodeAllocCount = 0;
	m_maxLinkAllocCount = 0;
	m_nodeAllocFailCount = 0;
	m_linkAllocFailCount = 0;
	m_nodes = nullptr;
	m_links = nullptr;
	m_reverseLinks = nullptr;
	m_allocMap.ClearAllBits();  // all nodes initially unallocated

	m_pDebugLog = nullptr;
	m_pDebugLogMem = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::Init()
{
	AllocateJanitor jj(kAllocNpcGlobals, FILE_LINE_FUNC);

	m_maxNodeCount = kMaxNodeCount;
	m_maxLinkCount = kMaxLinkCount;

	m_nodes = NDI_NEW Node[kMaxNodeCount];
	m_links = NDI_NEW Link[kMaxLinkCount];
	m_reverseLinks = NDI_NEW ReverseLink[kMaxLinkCount];

	Reset();

	if (FALSE_IN_FINAL_BUILD(Memory::IsDebugMemoryAvailable()))
	{
		static const size_t kDebugLogSize = 16 * 1024;
		m_pDebugLogMem = NDI_NEW(kAllocDebug) char[kDebugLogSize];
		m_pDebugLog = NDI_NEW(kAllocDebug) DoutMem("Nav Path Node Mgr Log", m_pDebugLogMem, kDebugLogSize);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::Reset()
{
	m_allocMap.ClearAllBits();  // all nodes initially unallocated

// put all links into a linked list starting at link 0
	for (U32F iLink = 0; iLink < m_maxLinkCount; ++iLink)
	{
		m_links[iLink].m_nodeId = kInvalidNodeId;
		m_links[iLink].m_nextLinkId = iLink + 1;

		m_reverseLinks[iLink].m_nodeId = kInvalidNodeId;
		m_reverseLinks[iLink].m_nextLinkId = iLink + 1;
	}

	for (U32F iNode = 0; iNode < m_maxNodeCount; ++iNode)
	{
		Node& node = m_nodes[iNode];
		node.m_id = iNode;
		node.m_linkId = kInvalidLinkId;
		node.m_reverseLinkId = kInvalidLinkId;
	}

	m_links[m_maxLinkCount - 1].m_nextLinkId = kInvalidLinkId;
	m_nodeAllocFailCount = 0;
	m_linkAllocFailCount = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_NAV_PATH_NODE_GRAPH_VALIDATION
void NavPathNodeMgr::Validate() const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!g_navOptions.m_enableNavPathGraphValidation)
	{
		return;
	}

	// build a map of free links
	BitArray<kMaxLinkCount> freeLinkMap;
	freeLinkMap.ClearAllBits();
	freeLinkMap.SetBit(0);
	U32 freeLinkCount = 0;

	// walk the free list
	for (U32F iLink = m_links[0].m_nextLinkId; iLink != kInvalidLinkId; iLink = m_links[iLink].m_nextLinkId)
	{
		freeLinkMap.SetBit(iLink);
		++freeLinkCount;
		const Link& link = m_links[iLink];
		PNM_ASSERT(link.m_nodeId == kInvalidNodeId);
	}

	// build map of allocated links
	BitArray<kMaxLinkCount> allocLinkMap;
	allocLinkMap.ClearAllBits();
	U32 allocLinkCount = 0;
	U32 polyNodeCount = 0;
	U32 polyExNodeCount = 0;
	U32 tapNodeCount = 0;
	U32 ledgeNodeCount = 0;

	// for each node,
	for (U32F iNode = 0; iNode < m_maxNodeCount; ++iNode)
	{
		const Node& node = m_nodes[iNode];

		PNM_ASSERT(!node.IsCorrupted());
		PNM_ASSERT(node.m_id == iNode);

		// if node is allocated,
		if (m_allocMap.IsBitSet(iNode))
		{
			PNM_ASSERT(node.IsValid());

			bool isLinkPoly = false;

			if (node.m_nodeType == NavPathNode::kNodeTypePoly)
			{
				const NavPolyHandle hPoly = node.GetNavPolyHandle();

				PNM_ASSERT(!hPoly.IsNull());

				const NavPoly* pPoly = hPoly.ToNavPoly();

				PNM_ASSERT(pPoly);
				PNM_ASSERT(pPoly->GetPathNodeId() == iNode); // check poly links to node
				isLinkPoly = pPoly->IsLink();

				++polyNodeCount;
			}
			else if (node.m_nodeType == NavPathNode::kNodeTypePolyEx)
			{
				const NavPolyExHandle hPolyEx = node.GetNavPolyExHandle();
				PNM_ASSERT(!hPolyEx.IsNull());
				const NavPolyEx* pPolyEx = hPolyEx.ToNavPolyEx();
				PNM_ASSERT(pPolyEx);
				PNM_ASSERT(pPolyEx->GetPathNodeId() == iNode);

				++polyExNodeCount;
			}
			else if (node.IsActionPackNode())
			{
				const ActionPackHandle hAp = node.GetActionPackHandle();
				PNM_ASSERT(!hAp.HasTurnedInvalid());

				// we may be calling this from inside TAP registration so gracefully allow null
				if (const TraversalActionPack* pTap = hAp.ToActionPack<TraversalActionPack>())
				{
					PNM_ASSERT(pTap->IsKnownPathNodeId(iNode));
				}

				++tapNodeCount;
			}
#if ENABLE_NAV_LEDGES
			else if (node.IsNavLedgeNode())
			{
				const NavLedgeHandle hLedge = node.GetNavLedgeHandle();
				PNM_ASSERT(!hLedge.IsNull());

				const NavLedge* pLedge = hLedge.ToLedge();
				PNM_ASSERT(pLedge);
				PNM_ASSERT(pLedge->GetPathNodeId() == iNode);

				++ledgeNodeCount;
			}
#endif // ENABLE_NAV_LEDGES

			for (U32F iLink = node.m_linkId; iLink != kInvalidLinkId; iLink = m_links[iLink].m_nextLinkId)
			{
				PNM_ASSERT(iLink < kMaxLinkCount); // check link index not out of bounds
				PNM_ASSERT(!freeLinkMap.IsBitSet(iLink));  // link better not be on free list
				PNM_ASSERT(!allocLinkMap.IsBitSet(iLink));  // check we haven't visited this link before..
				allocLinkMap.SetBit(iLink);
				++allocLinkCount;
				const Link& link = m_links[iLink];
				const ActionPack* pAp = node.IsActionPackNode() ? node.GetActionPack() : nullptr;

				PNM_ASSERTF(link.m_nodeId != kInvalidNodeId, ("Invalid link node at (iLink %d)", int(iLink)));

				if (link.m_nodeId == kInvalidNodeId)
					continue;

				PNM_ASSERTF(m_allocMap.IsBitSet(link.m_nodeId),
							("Bad link node Id %d (iNode %d iLink %d) %s",
							 link.m_nodeId,
							 int(iNode),
							 int(iLink),
							 pAp ? pAp->GetName() : "")); // link is to allocated node

				const Node& destNode = m_nodes[link.m_nodeId];

				PNM_ASSERT(destNode.IsValid());

				bool foundCorrespondingLink = false;
				const bool reverseLinkMatches0 = (link.m_reverseLinkId != kInvalidLinkId)
												 && (m_reverseLinks[link.m_reverseLinkId].m_forwardLinkId == iLink);
				bool reverseLinkMatches1 = false;

				for (U32F iDestLink = destNode.m_linkId; !foundCorrespondingLink && iDestLink != kInvalidLinkId;
					 iDestLink		= m_links[iDestLink].m_nextLinkId)
				{
					const Link& destLink = m_links[iDestLink];
					if (destLink.m_nodeId == iNode)
					{
						switch (link.m_type)
						{
						case NavPathNode::kLinkTypeBiDirectional:
							foundCorrespondingLink = (destLink.m_type == NavPathNode::kLinkTypeBiDirectional);
							break;
						case NavPathNode::kLinkTypeIncoming:
							foundCorrespondingLink = (destLink.m_type == NavPathNode::kLinkTypeOutgoing);
							break;
						case NavPathNode::kLinkTypeOutgoing:
							foundCorrespondingLink = (destLink.m_type == NavPathNode::kLinkTypeIncoming);
							break;
						}

						reverseLinkMatches1 = foundCorrespondingLink && (destLink.m_reverseLinkId != kInvalidLinkId)
											  && (m_reverseLinks[destLink.m_reverseLinkId].m_forwardLinkId == iDestLink);
					}
				}

				const bool foundReverseLink = foundCorrespondingLink && reverseLinkMatches0 && reverseLinkMatches1;

				PNM_ASSERTF(isLinkPoly || foundReverseLink,
							("Couldn't find reverse link (iNode %d iLink %d) %s",
							 int(iNode),
							 int(iLink),
							 pAp ? pAp->GetName() : ""));
			}
		}
		else
		{
			// node is unallocated
			PNM_ASSERT(node.m_linkId == kInvalidLinkId);  // check no links
			PNM_ASSERT(node.m_nodeType == NavPathNode::kNodeTypeInvalid); // check node type invalid
		}
	}
	PNM_ASSERT(allocLinkCount + freeLinkCount + 1 == m_maxLinkCount); // check for leaks in the bookkeeping of links
	//MsgCon("NavPathMgrNode::Validate: poly nodes %d, tap nodes %d, links %d\n", polyNodeCount, tapNodeCount, allocLinkCount);
}
#endif // ENABLE_NAV_PATH_NODE_GRAPH_VALIDATION

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::Update()
{
	PROFILE(Navigation, NavPathNodeMgr_Update);

	if (FALSE_IN_FINAL_BUILD(Failed()))
	{
		// allow us to reset if we completely unload everything
		if ((0 == m_nodeAllocCount) && (0 == m_linkAllocCount))
		{
			MsgOut(" **** RESETTING NavPathNodeMgr ****\n");
			Reset();
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_navOptions.m_validateNavPathGraphEveryFrame))
	{
		const bool prevVal = g_navOptions.m_enableNavPathGraphValidation;
		g_navOptions.m_enableNavPathGraphValidation = true;
		Validate();
		g_navOptions.m_enableNavPathGraphValidation = prevVal;
	}

	if (FALSE_IN_FINAL_BUILD(g_navOptions.m_dumpNavPathNodeMgrLogToTTY))
	{
		if (m_pDebugLog)
		{
			m_pDebugLog->Dump(GetMsgOutput(kMsgOut));
		}

		g_navOptions.m_dumpNavPathNodeMgrLogToTTY = false;
	}

	if (FALSE_IN_FINAL_BUILD(g_navOptions.m_printNavPathNodeMgrLogToMsgCon))
	{
		DoutBase* pMsgCon = GetMsgOutput(kMsgCon);
		if (m_pDebugLog && pMsgCon)
		{
			ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
			const size_t displayBufSize = 6 * 1024;
			char* displayBuf = NDI_NEW char[displayBufSize];
			memset(displayBuf, 0, displayBufSize);

			m_pDebugLog->DumpToBuffer(displayBuf, displayBufSize - 1, true);

			pMsgCon->Print(displayBuf);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const RenderCamera& cam = GetRenderCamera(0);

	const bool skipDynamic = g_navOptions.m_drawNavPathNodesStaticOnly;

	for (U64 iNode = m_allocMap.FindFirstSetBit(); iNode != U64(-1); iNode = m_allocMap.FindNextSetBit(iNode))
	{
		const Node& n = GetNode(iNode);
		const bool dynamicNode = (n.GetNodeType() == NavPathNode::kNodeTypePolyEx);
		const Vector vo = (dynamicNode ? Vector(0.0f, 0.5f, 0.0f) : kZero) + Vector(0.0f, float(iNode % 5) * 0.1f, 0.0f);

		if (dynamicNode && skipDynamic)
			continue;

		const Point nodePosPs = Point(n.GetPositionPs()) + vo;
		const Point nodePosWs = n.ParentToWorld(nodePosPs);

		const Scalar distToCamSqr = LengthSqr(cam.m_position - nodePosWs);

		if (distToCamSqr > 500.0f)
			continue;

		g_prim.Draw(DebugCross(nodePosWs, 0.05f));

		if (distToCamSqr < Sqr(20.0f))
		{
			StringBuilder<128> desc;
			desc.append_format("%d", int(iNode));

			switch (n.GetNodeType())
			{
			case NavPathNode::kNodeTypePoly:
				break;
			case NavPathNode::kNodeTypePolyEx:
				desc.append_format("-ex");
				break;
			case NavPathNode::kNodeTypeActionPackEnter:
				{
					desc.append_format("-ap-in");
					if (const ActionPack* pAp = n.GetActionPack())
					{
						desc.append_format("\n%s", pAp->GetName());
					}
				}
				break;
			case NavPathNode::kNodeTypeActionPackExit:
				{
					desc.append_format("-ap-out");
					if (const ActionPack* pAp = n.GetActionPack())
					{
						desc.append_format("\n%s", pAp->GetName());
					}
				}
				break;
#if ENABLE_NAV_LEDGES
			case NavPathNode::kNodeTypeNavLedge:
				desc.append_format("-ledge");
				break;
#endif
			default:
				desc.append_format("-unknown");
				break;
			}

			if (n.IsBlocked())
			{
				desc.append_format(" [blocked]");
			}

			g_prim.Draw(DebugString(nodePosWs, desc.c_str(), kColorWhite, 0.5f));
		}

		for (NavPathNode::LinkId linkId = n.m_linkId; linkId != kInvalidLinkId; linkId = m_links[linkId].m_nextLinkId)
		{
			const Node& linkNode = GetNode(m_links[linkId].m_nodeId);
			const bool dynamicLinkNode = linkNode.GetNodeType() == NavPathNode::kNodeTypePolyEx;

			if (dynamicLinkNode && skipDynamic)
				continue;

			const Vector linkVo = (dynamicLinkNode ? Vector(0.0f, 0.5f, 0.0f) : kZero) + Vector(0.0f, float(m_links[linkId].m_nodeId % 5) * 0.1f, 0.0f);
			const Point linkNodePosPs = Point(linkNode.GetPositionPs()) + linkVo;

			const Point linkNodePosWs = linkNode.ParentToWorld(linkNodePosPs);

			const bool dynamic = m_links[linkId].IsDynamic();

			switch (m_links[linkId].m_type)
			{
			case NavPathNode::kLinkTypeBiDirectional:
				g_prim.Draw(DebugLine(nodePosWs, linkNodePosWs, dynamic ? kColorRed : kColorGray, kColorWhite));
				break;

			case NavPathNode::kLinkTypeOutgoing:
				g_prim.Draw(DebugArrow(nodePosWs, linkNodePosWs, dynamic ? kColorRed : kColorGray));
				break;

			case NavPathNode::kLinkTypeIncoming:
				g_prim.Draw(DebugArrow(linkNodePosWs, nodePosWs, dynamic ? kColorRed : kColorGray));
				break;

			default:
				g_prim.Draw(DebugLine(nodePosWs, linkNodePosWs, dynamic ? kColorRed : kColorGray, kColorWhite));
				break;
			}


			if (!m_allocMap.IsBitSet(linkNode.GetId()))
			{
				g_prim.Draw(DebugString(linkNodePosWs + Vector(0.0f, 0.5f, 0.0f),
										StringBuilder<128>("%d unallocated", int(linkNode.GetId())).c_str(),
										kColorRed));
			}

			if (distToCamSqr < Sqr(20.0f))
			{
				g_prim.Draw(DebugString(Lerp(nodePosWs, linkNodePosWs, 0.33f),
										StringBuilder<64>("%d", int(linkId)).c_str(),
										dynamic ? kColorRedTrans : kColorGrayTrans,
										0.6f));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::DebugPrintStats(MsgOutput output /* = kMsgCon */) const
{
	STRIP_IN_FINAL_BUILD;

	if (DoutBase* pDout = GetMsgOutput(output))
	{
		pDout->Printf("Nav Path Node Mgr:\n");
		pDout->Printf("  Nodes:  %d / %d (%0.1f%%) [high: %d]",
					  m_nodeAllocCount,
					  m_maxNodeCount,
					  100.0f * (float(m_nodeAllocCount) / float(m_maxNodeCount)),
					  m_maxNodeAllocCount);
		if (m_nodeAllocFailCount > 0)
		{
			pDout->Printf("  [%d Failed allocations!]", m_nodeAllocFailCount);
		}
		pDout->Printf("\n");

		pDout->Printf("  Links:  %d / %d (%0.1f%%) [high: %d]",
					  m_linkAllocCount,
					  m_maxLinkCount,
					  100.0f * (float(m_linkAllocCount) / float(m_maxLinkCount)),
					  m_maxLinkAllocCount);
		if (m_linkAllocFailCount > 0)
		{
			pDout->Printf("  [%d Failed allocations!]", m_linkAllocFailCount);
		}
		pDout->Printf("\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::AddExNodesFromNavPoly(const NavPoly* pPoly)
{
	if (!pPoly)
		return;

	const NavMesh* pMesh = pPoly->GetNavMesh();

	for (NavPolyEx* pNewPolyEx = pPoly->GetNavPolyExList(); pNewPolyEx; pNewPolyEx = pNewPolyEx->m_pNext)
	{
		pNewPolyEx->m_pathNodeId = AllocateNode();

		if (pNewPolyEx->m_pathNodeId < m_maxNodeCount)
		{
			const Point posLs = pNewPolyEx->GetCentroid();
			const Point posPs = pMesh->LocalToParent(posLs);
			m_nodes[pNewPolyEx->m_pathNodeId].AsNavPolyEx(pNewPolyEx, posPs);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::AddExLinksFromNavPoly(const NavPoly* pPoly)
{
	PROFILE_ACCUM(AddExLinksFromNavPoly);

	NavPolyEx* pStartingExPoly = pPoly ? pPoly->GetNavPolyExList() : nullptr;

	if (!pStartingExPoly)
		return;

	const NavMesh* pMesh = pPoly->GetNavMesh();
	const NavPathNode::NodeId polyNodeId = pPoly->GetPathNodeId();
	PNM_ASSERT(polyNodeId != kInvalidNodeId);
	NavPathNode& polyNode = GetNode(polyNodeId);


	for (NavPolyEx* pNewPolyEx = pStartingExPoly; pNewPolyEx; pNewPolyEx = pNewPolyEx->m_pNext)
	{
		PNM_ASSERT(pNewPolyEx->m_ownerPathNodeId == polyNodeId);

		const U32F iFromNode = pNewPolyEx->m_pathNodeId;
		PNM_ASSERT(iFromNode != kInvalidNodeId);

		for (U32F iN = 0; iN < pNewPolyEx->m_numVerts; ++iN)
		{
			const Point edge0Ps = pMesh->LocalToParent(pNewPolyEx->GetVertex(iN));
			const Point edge1Ps = pMesh->LocalToParent(pNewPolyEx->GetNextVertex(iN));

			if (const NavPolyEx* pNeighborEx = NavPolyExHandle(pNewPolyEx->m_adjPolys[iN]).ToNavPolyEx())
			{
				PNM_ASSERT(pNeighborEx->m_ownerPathNodeId != kInvalidNodeId);
				AddLink(iFromNode,
						pNeighborEx->m_pathNodeId,
						edge0Ps,
						edge1Ps,
						NavPathNode::kLinkTypeBiDirectional,
						pNeighborEx->m_ownerPathNodeId);
			}
			else if (const NavPoly* pNeighbor = NavPolyHandle(pNewPolyEx->m_adjPolys[iN]).ToNavPoly())
			{
				const U32F iNeighborNode = pNeighbor->GetPathNodeId();
				PNM_ASSERT(iNeighborNode != kInvalidNodeId);

				AddLink(iFromNode, iNeighborNode, edge0Ps, edge1Ps, NavPathNode::kLinkTypeBiDirectional, iNeighborNode);
				AddLink(iNeighborNode, iFromNode, edge1Ps, edge0Ps, NavPathNode::kLinkTypeBiDirectional, polyNodeId);
			}
		}
	}

	// add AP links to the ex polys that contain them
	for (U32F iLink = polyNode.GetFirstLinkId(); iLink != kInvalidLinkId; iLink = m_links[iLink].m_nextLinkId)
	{
		const Link& link = m_links[iLink];
		if (link.IsDynamic())
			continue;

		const NavPathNode::NodeId apNodeId = link.m_nodeId;
		PNM_ASSERT(apNodeId != kInvalidNodeId);
		if (apNodeId == kInvalidNodeId)
			continue;

		const Node& apNode = m_nodes[apNodeId];
		if (!apNode.IsActionPackNode())
			continue;

		NavPolyEx* pBestPolyEx = nullptr;
		float bestRating = kLargeFloat;

		const Point nodePosLs = pMesh->ParentToLocal(apNode.GetPositionPs());

		for (NavPolyEx* pNewPolyEx = pStartingExPoly; pNewPolyEx; pNewPolyEx = pNewPolyEx->m_pNext)
		{
			const float sd = pNewPolyEx->SignedDistPointPolyXzSqr(nodePosLs);
			if (sd < bestRating)
			{
				bestRating = sd;
				pBestPolyEx = pNewPolyEx;

				if (sd < 0.0f)
					break;
			}
		}

		if (!pBestPolyEx)
		{
			// ???
			pBestPolyEx = pStartingExPoly;
		}

#if 0
		if (bestRating > -kSmallestFloat)
		{
			const float d = pBestPolyEx->SignedDistPointPolyXzSqr(nodePosLs);

			g_prim.Draw(DebugCross(apNode.GetPositionPs(), 0.1f, kColorCyan));

			g_prim.Draw(DebugString(pMesh->LocalToWorld(pBestPolyEx->GetVertex(0)), "0"));
			g_prim.Draw(DebugString(pMesh->LocalToWorld(pBestPolyEx->GetVertex(1)), "1"));
			g_prim.Draw(DebugString(pMesh->LocalToWorld(pBestPolyEx->GetVertex(2)), "2"));

			pBestPolyEx->DebugDrawEdges(kColorOrange, kColorYellow, kColorRed, 0.0f);
		}
#endif

		PNM_ASSERT(pBestPolyEx);

		const U32F iFromNode = pBestPolyEx->m_pathNodeId;

		AddLink(iFromNode,
				apNodeId,
				link.m_edgeVertsPs[0],
				link.m_edgeVertsPs[1],
				NavPathNode::LinkType(link.m_type),
				apNodeId);

		for (U32F iApLink = apNode.GetFirstLinkId(); iApLink != kInvalidLinkId; iApLink = m_links[iApLink].m_nextLinkId)
		{
			const Link& apLink = m_links[iApLink];
			if (apLink.IsDynamic())
				continue;
			if (apLink.m_nodeId != polyNodeId)
				continue;

			AddLink(apNodeId,
					iFromNode,
					link.m_edgeVertsPs[0],
					link.m_edgeVertsPs[1],
					NavPathNode::LinkType(apLink.m_type),
					polyNodeId);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::RemoveNavPolyEx(NavPolyEx* pPolyEx)
{
	if (!pPolyEx)
		return;

	if (pPolyEx->m_pathNodeId == kInvalidNodeId)
		return;

	RemoveNode(pPolyEx->m_pathNodeId);

	pPolyEx->m_pathNodeId = kInvalidNodeId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavPathNodeMgr::AllocateNode()
{
	U32F iNode = m_allocMap.FindFirstClearBit();
	//PNM_ASSERT(iNode < kMaxNodeCount);
	if (iNode < kMaxNodeCount)
	{
		PnmLog("Allocating node '%d'\n", iNode);
		++m_nodeAllocCount;
		m_allocMap.SetBit(iNode);
		m_maxNodeAllocCount = Max(m_maxNodeAllocCount, m_nodeAllocCount);
	}
	else
	{
		++m_nodeAllocFailCount;
		iNode = kInvalidNodeId;
	}
	return iNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::RemoveAllLinks(U32F iNode)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (iNode < kMaxNodeCount)
	{
		Node& node = m_nodes[iNode];

		// first search for any incoming links
		{
			U32F iRLink = node.m_reverseLinkId;
			while (iRLink != kInvalidLinkId)
			{
				ReverseLink& rLink = m_reverseLinks[iRLink];
				iRLink = m_reverseLinks[iRLink].m_nextLinkId;

				RemoveLink(rLink.m_nodeId, iNode);
			}

			node.m_reverseLinkId = kInvalidLinkId;
		}

		{
			U32F iLink = node.m_linkId;
			while (iLink != kInvalidLinkId)
			{
				PNM_ASSERT(iLink < kMaxLinkCount);
				Link& link = m_links[iLink];
				// remember next link in the chain
				iLink = link.m_nextLinkId;

				RemoveLink(iNode, link.m_nodeId);
			}

			node.m_linkId = kInvalidLinkId;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::RemoveNode(U32F iNode)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (iNode == NavPathNodeMgr::kInvalidNodeId)
		return;

	PNM_ASSERT(iNode < kMaxNodeCount);
	if (iNode >= kMaxNodeCount)
		return;

	PNM_ASSERT(m_allocMap.IsBitSet(iNode));

	PnmLog("Removing node '%d'\n", int(iNode));

	Node& node = m_nodes[iNode];

	RemoveAllLinks(iNode);

	node.m_nodeType = NavPathNode::kNodeTypeInvalid;
	FreeNode(iNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::FreeNode(U32F iNode)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	PNM_ASSERT(iNode < kMaxNodeCount);
	if (iNode < kMaxNodeCount)
	{
		PNM_ASSERT(m_allocMap.IsBitSet(iNode));
		PnmLog("Freeing node '%d'\n", int(iNode));
		m_allocMap.ClearBit(iNode);
		--m_nodeAllocCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPathNodeMgr::TryFreeNode(U32F iNode)
{
	if ((iNode == kInvalidNodeId) || (iNode >= kMaxNodeCount))
		return false;

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	PNM_ASSERT(m_allocMap.IsBitSet(iNode));
	PnmLog("Freeing node '%d'\n", int(iNode));
	m_allocMap.ClearBit(iNode);
	--m_nodeAllocCount;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavPathNodeMgr::AllocateLink()
{
	U32F iNextLink = m_links[0].m_nextLinkId; // link 0 is head of free list
	PNM_ASSERT(iNextLink != kInvalidLinkId);
	if (iNextLink != kInvalidLinkId)
	{
		++m_linkAllocCount;
		m_maxLinkAllocCount = Max(m_maxLinkAllocCount, m_linkAllocCount);
		Link& link = m_links[iNextLink];
		m_links[0].m_nextLinkId = link.m_nextLinkId;
		link.m_nodeId = kInvalidNodeId;
		link.m_nextLinkId = kInvalidLinkId;
	}
	else
	{
		++m_linkAllocFailCount;
	}
	return iNextLink;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::FreeLink(Link* pLink)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const U32F iLink = pLink - &m_links[0];
	PnmLog("Freeing link '%d'\n", int(iLink));
	--m_linkAllocCount;
	pLink->m_nodeId = kInvalidNodeId;
	pLink->m_nextLinkId = m_links[0].m_nextLinkId;
	m_links[0].m_nextLinkId = pLink - &m_links[0];
	PNM_ASSERT(m_links[0].m_nextLinkId < kMaxLinkCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavPathNodeMgr::AllocateReverseLink()
{
	U32F iNextRLink = m_reverseLinks[0].m_nextLinkId; // link 0 is head of free list
	PNM_ASSERT(iNextRLink != kInvalidLinkId);
	if (iNextRLink != kInvalidLinkId)
	{
		ReverseLink& rLink = m_reverseLinks[iNextRLink];
		m_reverseLinks[0].m_nextLinkId = rLink.m_nextLinkId;
		rLink.m_nodeId = kInvalidNodeId;
		rLink.m_nextLinkId = kInvalidLinkId;
	}

	return iNextRLink;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::FreeReverseLink(ReverseLink* pLink)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const U32F iRLink = pLink - &m_reverseLinks[0];
	PnmLog("Freeing reverse link '%d'\n", int(iRLink));
	pLink->m_nodeId = kInvalidNodeId;
	pLink->m_nextLinkId = m_reverseLinks[0].m_nextLinkId;
	m_reverseLinks[0].m_nextLinkId = pLink - &m_reverseLinks[0];
	PNM_ASSERT(m_reverseLinks[0].m_nextLinkId < kMaxLinkCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPathNodeMgr::LinkId NavPathNodeMgr::AddLink(U32F iFromNode,
											   U32F iToNode,
											   Point_arg edge0Ps,
											   Point_arg edge1Ps,
											   NavPathNode::LinkType linkType /* = NavPathNode::kLinkTypeBiDirectional */,
											   U32F iStaticToNode /* = kInvalidNodeId */)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	//Validate();

	// todo add error messages
	PNM_ASSERT(iFromNode < kMaxNodeCount);
	PNM_ASSERT(iToNode < kMaxNodeCount);

	LinkId ret = kInvalidLinkId;

	if ((iFromNode < kMaxNodeCount) && (iToNode < kMaxNodeCount))
	{
		PNM_ASSERT(m_allocMap.IsBitSet(iFromNode));
		PNM_ASSERT(m_allocMap.IsBitSet(iToNode));

		const U32F iLink = AllocateLink();
		PNM_ASSERT(iLink != kInvalidLinkId);
		PNM_ASSERT(iLink < kMaxLinkCount);

		if (iLink >= kMaxLinkCount || iLink == kInvalidLinkId)
		{
			return kInvalidLinkId;
		}

		const U32F iReverseLink = AllocateReverseLink();
		PNM_ASSERT(iReverseLink != kInvalidLinkId);
		PNM_ASSERT(iReverseLink < kMaxLinkCount);

		if (iLink != kInvalidLinkId)
		{
			PnmLog("Adding link '%d' ['%d' -> '%d']\n", int(iLink), int(iFromNode), int(iToNode));

			Link& link = m_links[iLink];
			LinkId* piLink = &m_nodes[iFromNode].m_linkId;  // ptr to patch
			link.m_nodeId = iToNode;
			link.m_nextLinkId = *piLink;
			link.m_edgeVertsPs[0] = edge0Ps;
			link.m_edgeVertsPs[1] = edge1Ps;
			link.m_type = linkType;
			link.m_staticNodeId = iStaticToNode;
			link.m_reverseLinkId = iReverseLink;
			*piLink = iLink;
			ret = iLink;
		}

		if (iReverseLink != kInvalidLinkId)
		{
			PnmLog("Adding reverse link '%d' ['%d' -> '%d']\n", int(iReverseLink), int(iToNode), int(iFromNode));

			ReverseLink& rLink = m_reverseLinks[iReverseLink];
			LinkId* piRLink = &m_nodes[iToNode].m_reverseLinkId;  // ptr to patch
			rLink.m_nodeId = iFromNode;
			rLink.m_forwardLinkId = iLink;
			rLink.m_nextLinkId = *piRLink;
			*piRLink = iReverseLink;
		}
	}

	//Validate();

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::AddLinkSafe(U32F iFromNode,
								 U32F iToNode,
								 Point_arg edge0Ps,
								 Point_arg edge1Ps,
								 NavPathNode::LinkType linkType /* = NavPathNode::kLinkTypeBiDirectional */)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	// only add a link between the nodes if there isn't one already
	PNM_ASSERT(iFromNode < kMaxNodeCount);
	PNM_ASSERT(iToNode < kMaxNodeCount);
	if (iFromNode < kMaxNodeCount && iToNode < kMaxNodeCount)
	{
		PNM_ASSERT(m_allocMap.IsBitSet(iFromNode));
		PNM_ASSERT(m_allocMap.IsBitSet(iToNode));
		Node& fromNode = m_nodes[iFromNode];
		bool linkFound = false;
		// for each link,
		for (U32F iLink = fromNode.m_linkId; iLink != kInvalidLinkId; iLink = m_links[iLink].m_nextLinkId)
		{
			PNM_ASSERT(iLink < kMaxLinkCount);
			Link& link = m_links[iLink];
			// check if requested link already exists
			if (link.m_nodeId == iToNode &&
				link.m_type == linkType)
			{
				linkFound = true;
				break;
			}
		}
		// only add if it won't be a duplicate
		if (!linkFound)
		{
			AddLink(iFromNode, iToNode, edge0Ps, edge1Ps, linkType);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPathNodeMgr::RemoveLink(U32F iFromNode, U32F iToNode)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());
	PNM_ASSERT(iFromNode < kMaxNodeCount);
	PNM_ASSERT(iToNode < kMaxNodeCount);

	bool found = false;

	U32F iFoundRevLink = kInvalidLinkId;

	if (iFromNode < kMaxNodeCount)
	{
		if (m_allocMap.IsBitSet(iFromNode))
		{
			// ptr to remember which index to patch
			//   NOTE: piLink starts out pointing to an element of a Node struct, but
			//     later may be changed to point to an element of a Link struct
			LinkId* piLink = &m_nodes[iFromNode].m_linkId;
			while (true)
			{
				LinkId iLink = *piLink;
				if (iLink == kInvalidLinkId)
				{
					// end of list reached, link not found
					break;
				}
				PNM_ASSERT(iLink < kMaxLinkCount);
				Link& link = m_links[iLink];
				// is this the link we're after?
				if (link.m_nodeId == iToNode)
				{
					iFoundRevLink = link.m_reverseLinkId;

					// found link to remove
					// set previous link to skip over this link
					*piLink = link.m_nextLinkId;
					FreeLink(&link);

					found = true;
					break;
				}
				piLink = &link.m_nextLinkId;
				iLink = *piLink;
			}
		}
		else
		{
			PNM_ASSERT(false);
		}
	}

	if (iToNode < kMaxNodeCount)
	{
		if (m_allocMap.IsBitSet(iToNode))
		{
			LinkId* piRLink = &m_nodes[iToNode].m_reverseLinkId;
			while (true)
			{
				LinkId iRLink = *piRLink;
				if (iRLink == kInvalidLinkId)
				{
					break;
				}

				ReverseLink& rLink = m_reverseLinks[iRLink];

				if (rLink.m_nodeId == iFromNode)
				{
					PNM_ASSERT(iRLink == iFoundRevLink);

					*piRLink = rLink.m_nextLinkId;
					FreeReverseLink(&rLink);
					break;
				}

				piRLink = &rLink.m_nextLinkId;
				iRLink = *piRLink;
			}
		}
		else
		{
			PNM_ASSERT(false);
		}
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPathNodeMgr::AddNavMesh(NavMesh* pMesh)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	Validate();

	PNM_ASSERT(pMesh);

	U32F polyCount = pMesh->GetPolyCount();

	PnmLog("Adding NavMesh '%s' with '%d' polys\n", pMesh->GetName(), int(polyCount));

	for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		NavPoly& poly = pMesh->GetPoly(iPoly);

		poly.m_pathNodeId = kInvalidNodeId;
	}

	// allocate all nodes without links
	for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		NavPoly& poly = pMesh->GetPoly(iPoly);

		if (poly.m_flags.m_error)
			continue;

		const U32F iNode = AllocateNode();

		if (iNode >= m_maxNodeCount)
		{
			for (I32F iUnPoly = iPoly; iUnPoly >= 0; --iUnPoly)
			{
				NavPoly& unPoly = pMesh->GetPoly(iUnPoly);
				if (unPoly.m_pathNodeId != kInvalidNodeId)
				{
					FreeNode(unPoly.m_pathNodeId);
					unPoly.m_pathNodeId = kInvalidNodeId;
				}
			}

			return false;
		}

		poly.m_pathNodeId = iNode;

		Point posLs = poly.GetCentroid();
		Node& node = m_nodes[iNode];
		node.AsNavPoly(poly, pMesh->LocalToParent(posLs));
	}

	// add links between nodes
	for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		const NavPoly& poly = pMesh->GetPoly(iPoly);
		if (poly.m_flags.m_error)
			continue;

		U32F iFromNode = poly.GetPathNodeId();
		PNM_ASSERT(iFromNode < m_maxNodeCount);
		if (iFromNode >= m_maxNodeCount)
			continue;

		U32F edgeCount = poly.GetVertexCount();
		for (U32F iEdge = 0; iEdge < edgeCount; ++iEdge)
		{
			U32F iAdjPoly = poly.GetAdjacentId(iEdge);
			if (iAdjPoly == NavPoly::kNoAdjacent)
				continue;

			const NavPoly& destPoly = pMesh->GetPoly(iAdjPoly);
			if (destPoly.m_flags.m_error)
				continue;

			const Point edge0Ps = pMesh->LocalToParent(poly.GetVertex(iEdge));
			const Point edge1Ps = pMesh->LocalToParent(poly.GetNextVertex(iEdge));

			NAV_ASSERTF(iEdge < 4, ("Trying to add a link with an edge index of %d", iEdge));

			if (kInvalidLinkId == AddLink(iFromNode, destPoly.GetPathNodeId(), edge0Ps, edge1Ps))
			{
				for (I32F iUnPoly = iPoly; iUnPoly >= 0; --iUnPoly)
				{
					NavPoly& unPoly = pMesh->GetPoly(iUnPoly);
					if (unPoly.m_pathNodeId != kInvalidNodeId)
					{
						RemoveNode(unPoly.m_pathNodeId);
						unPoly.m_pathNodeId = kInvalidNodeId;
					}
				}

				return false;
			}
		}
	}

	Validate();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::RemoveNavMesh(NavMesh* pMesh)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	Validate();

	PNM_ASSERT(pMesh);
	U32F polyCount = pMesh->GetPolyCount();

	PnmLog("Removing NavMesh '%s' with '%d' polys\n", pMesh->GetName(), int(polyCount));

	// for each poly in the navmesh,
	for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		NavPoly& poly = pMesh->GetPoly(iPoly);

		if (poly.m_pathNodeId == kInvalidNodeId)
			continue;

		// get the corresponding node
		const U32F iNode = poly.GetPathNodeId();
		PNM_ASSERT(iNode < kMaxNodeCount);
		if (iNode < kMaxNodeCount)
		{
			// remove it
			RemoveNode(iNode);
			poly.m_pathNodeId = kInvalidNodeId;
		}
	}

	Validate();
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPathNodeMgr::AddNavLedgeGraph(NavLedgeGraph* pLedgeGraph)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	Validate();

	PNM_ASSERT(pLedgeGraph);

	const U32F ledgeCount = pLedgeGraph->GetLedgeCount();

	PnmLog("Adding NavLedgeGraph '%s' with '%d' ledges\n", pLedgeGraph->GetName(), int(ledgeCount));

	for (U32F iLedge = 0; iLedge < ledgeCount; ++iLedge)
	{
		NavLedge& ledge = pLedgeGraph->GetLedge(iLedge);

		ledge.m_pathNodeId = kInvalidNodeId;

		if (ledge.m_flags.m_error)
			continue;

		const U32F iNode = AllocateNode();
		PNM_ASSERT(iNode < m_maxNodeCount);

		if (iNode >= m_maxNodeCount)
		{
			for (I32F iUnLedge = iLedge; iUnLedge >= 0; --iUnLedge)
			{
				NavLedge& unLedge = pLedgeGraph->GetLedge(iUnLedge);
				if (unLedge.m_pathNodeId != kInvalidNodeId)
				{
					FreeNode(unLedge.m_pathNodeId);
					unLedge.m_pathNodeId = kInvalidNodeId;
				}
			}

			return false;
		}

		ledge.m_pathNodeId = iNode;

		Point posLs = ledge.GetCenterLs();
		Node& node = m_nodes[iNode];
		node.AsNavLedge(&ledge, pLedgeGraph->LocalToParent(posLs));
	}

	// add links between ledges
	for (U32F iLedge = 0; iLedge < ledgeCount; ++iLedge)
	{
		const NavLedge& ledge = pLedgeGraph->GetLedge(iLedge);
		if (ledge.m_flags.m_error)
			continue;

		const U32F iFromNode = ledge.GetPathNodeId();
		PNM_ASSERT(iFromNode < m_maxNodeCount);
		if (iFromNode >= m_maxNodeCount)
			continue;

		const U32F neighborCount = ledge.GetNumNeighbors();
		for (U32F iNeighbor = 0; iNeighbor < neighborCount; ++iNeighbor)
		{
			const U32F iAdjLedge = ledge.GetLink(iNeighbor).m_destLedgeId;

			const NavLedge& destLedge = pLedgeGraph->GetLedge(iAdjLedge);
			if (destLedge.m_flags.m_error)
				continue;

			const U32F iDestNode = destLedge.GetPathNodeId();

			const Point centerPs = pLedgeGraph->LocalToParent(ledge.GetCenterLs());

			if (kInvalidLinkId == AddLink(iFromNode, iDestNode, centerPs, centerPs))
			{
				for (I32F iUnLedge = iLedge; iUnLedge >= 0; --iUnLedge)
				{
					NavLedge& unLedge = pLedgeGraph->GetLedge(iUnLedge);
					if (unLedge.m_pathNodeId != kInvalidNodeId)
					{
						FreeNode(unLedge.m_pathNodeId);
						unLedge.m_pathNodeId = kInvalidNodeId;
					}
				}

				return false;
			}
		}
	}

	Validate();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPathNodeMgr::RemoveNavLedgeGraph(NavLedgeGraph* pLedgeGraph)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	Validate();

	PNM_ASSERT(pLedgeGraph);
	const U32F ledgeCount = pLedgeGraph->GetLedgeCount();

	PnmLog("Removing NavLedgeGraph '%s' with '%d' ledges\n", pLedgeGraph->GetName(), int(ledgeCount));

	for (U32F iLedge = 0; iLedge < ledgeCount; ++iLedge)
	{
		NavLedge& ledge = pLedgeGraph->GetLedge(iLedge);

		const U32F iNode = ledge.m_pathNodeId;

		if (iNode == kInvalidNodeId)
			continue;

		// get the corresponding node
		PNM_ASSERT(iNode < kMaxNodeCount);
		if (iNode < kMaxNodeCount)
		{
			// remove it
			RemoveNode(iNode);
			ledge.m_pathNodeId = kInvalidNodeId;
		}
	}

	Validate();
}
#endif // ENABLE_NAV_LEDGES
