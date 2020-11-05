/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "gamelib/gameplay/nav/nav-path-build.h"

#include "corelib/containers/hashtable.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/exposure-map.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-patch.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"

#include <algorithm>

/// --------------------------------------------------------------------------------------------------------------- ///
static const size_t kMaxNavItems = 256;
typedef BitArray<kMaxNavItems> NavItemBitArray;
typedef RobinHoodHashTable<NavManagerId, NavItemBitArray> NavItemRegistrationTable;
static CONST_EXPR float kLinkDist = 0.0001f;

#define DEBUG_NAV_ITEM_TABLE 0

static inline bool FastPointEqual(const Point _a, const Point _b)
{
	const __m128 a = _a.QuadwordValue();
	const __m128 b = _b.QuadwordValue();
	const __m128 msk = _mm_permute_ps(_mm_cmp_ps(a, b, _CMP_NEQ_OQ), 164);
	return _mm_testz_ps(msk, msk);
}
namespace Nav
{
	bool g_hackPrintParams = false;

	/// --------------------------------------------------------------------------------------------------------------- ///
	static DMENU::ItemEnumPair s_smoothPathFindFilters[] =
	{
		DMENU::ItemEnumPair("No Smoothing",						kNoSmoothing),
		DMENU::ItemEnumPair("Full Smoothing",					kFullSmoothing),

		DMENU::ItemEnumPair()
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct CornerWalkData
	{
		const NavItemRegistrationTable* m_pCornerTable;
		NavItemBitArray m_cornerBits;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavPortal
	{
		enum class Type
		{
			kRegular,
			kSingular,
			kWideTap,
		};

		void Init(Point_arg p0, Point_arg p1, Type type, const NavNodeData* pNodeData)
		{
			m_pos[0] = p0;
			m_pos[1] = p1;
			m_normal = RotateY90(AsUnitVectorXz(p1 - p0, kZero));
			m_type = type;

			m_pNodeData = pNodeData;
		}

		void Init(Point_arg p0,
			Point_arg p1,
			Vector_arg normal,
			Type type,
			const NavNodeData* pNodeData)
		{
			m_pos[0] = p0;
			m_pos[1] = p1;
			m_normal = normal;
			m_type = type;

			m_pNodeData = pNodeData;
		}

		void LocalToWorld(const NavMesh* pNavMesh)
		{
			m_pos[0] = pNavMesh->LocalToWorld(m_pos[0]);
			m_pos[1] = pNavMesh->LocalToWorld(m_pos[1]);
			m_normal = pNavMesh->LocalToWorld(m_normal);
		}

		static Color TypeToDebugColor(const Type t)
		{
			switch (t)
			{
			case Type::kRegular:	return kColorOrange;
			case Type::kSingular:	return kColorCyan;
			case Type::kWideTap:	return kColorBlue;
			}

			return kColorRed;
		}

		Point	m_pos[2];
		Vector	m_normal;
		Type	m_type;

		const NavNodeData*	m_pNodeData;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavFrustum
	{
		NavFrustum(Point_arg focusPos, const NavPortal& portal, U32F iPortal)
		{
			m_vec[0] = portal.m_pos[0] - focusPos;
			m_vec[1] = portal.m_pos[1] - focusPos;
			m_iPortal[0] = m_iPortal[1] = iPortal;
		}

		void UpdateEdge(U32F iEdge, Vector_arg newEdge, U32F iNewPortal)
		{
			m_vec[iEdge] = newEdge;
			m_iPortal[iEdge] = iNewPortal;
		}

		Vector m_vec[2];
		U32F m_iPortal[2];
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavEdge
	{
		struct Link
		{
			Vector m_vertNormalLs = kZero;
			Point m_vertsLs[2] = { kInvalidPoint, kInvalidPoint };
			Point m_vertsPs[2] = { kInvalidPoint, kInvalidPoint };
			const NavEdge* m_pEdge = nullptr;
			I64 m_iEdge = -1;
			float m_linkAngleDeg = 0.0f;
			bool m_interior = false;
			I8 m_linkIntersection = -1;
		};

		// leaving this empty constructor here (instead of default constructor) so that PGO builds don't fail
		NavEdge() {}

		void DebugDraw(float pathRadius, DebugPrimTime tt = kPrimDuration1FrameAuto) const
		{
			const NavMesh* pNavMesh = m_pNavMesh;
			//const Color clr = m_shadowed ? kColorGray : AI::IndexToColor(pNavMesh->GetManagerId().m_navMeshIndex);
			const Color clr = m_shadowed ? kColorGray : AI::IndexToColor(m_iEdge);
			Color clrA = clr;
			clrA.SetA(0.3f);

			PrimServerWrapper ps = PrimServerWrapper(pNavMesh->GetOriginWs());
			ps.SetDuration(tt);

			const Point m = AveragePos(m_v0Ls, m_v1Ls);
			const Vector vo = Vector(0.0f, float(m_iEdge + 1) * 0.025f, 0.0f);

			StringBuilder<128> txt;
			txt.format("e%d", m_iEdge);

			if (m_link0.m_pEdge || m_link1.m_pEdge)
			{
				txt.append(" [");
				if (m_link0.m_pEdge)
					txt.append_format("%d", m_link0.m_iEdge);
				else
					txt.append("-");
				txt.append(":");
				if (m_link1.m_pEdge)
					txt.append_format("%d", m_link1.m_iEdge);
				else
					txt.append("-");
				txt.append("]");
			}

			if (m_shadowed)
			{
				txt.append(" (shadowed)");
			}
			if (m_fromLink)
			{
				txt.append(" (link)");
			}
			ps.EnableHiddenLineAlpha();
			ps.DrawString(m + vo, txt.c_str(), clr, 0.75f);
			ps.DrawLine(m, m + vo, clr);
			ps.SetLineWidth(4.0f);
			ps.DrawArrow(m_v0Ls, m_v1Ls, 0.3f, clr);
			ps.DrawArrow(AveragePos(m_v0Ls, m_v1Ls), m_normalLs * 0.2f, 0.2f, clrA);

			const Point proj0Ls = m_pNavMesh->ParentToLocal(m_projected0Ps);
			const Point proj1Ls = m_pNavMesh->ParentToLocal(m_projected1Ps);

/*
			if (false)
			{
				const Point orgProj0Ls = m_pNavMesh->ParentToLocal(m_orgProjected0Ps);
				const Point orgProj1Ls = m_pNavMesh->ParentToLocal(m_orgProjected1Ps);

				ps.SetLineWidth(2.0f);
				ps.DrawLine(orgProj0Ls, orgProj1Ls, clrA);
			}
*/

			ps.DrawArrow(proj0Ls, proj1Ls, 0.2f, clr);

			if (!m_shadowed)
			{
				if (m_link0.m_pEdge && !m_link0.m_pEdge->m_shadowed)
				{
					const NavEdge& le0 = *m_link0.m_pEdge;

					const Vector vertNormalLs = m_link0.m_vertNormalLs;

					if (m_link0.m_interior)
					{
						ps.DrawCross(m_link0.m_vertsLs[0], 0.05f, clr);
					}
					else
					{
						ps.EnableWireFrame();
						//ps.DrawArc(m_v0Ls, m_normalLs * pathRadius, le0.m_normalLs * pathRadius, clr, 1.0f, true);
						ps.DrawArrow(m_link0.m_vertsLs[0], m_link0.m_vertsLs[1], 0.2f, clr);
					}

					if (m_link0.m_linkIntersection != -1)
					{
						ps.DrawCross(m_link0.m_vertsLs[0], 0.01f, kColorYellow);
					}
				}

				if (m_link1.m_pEdge && !m_link1.m_pEdge->m_shadowed)
				{
					const NavEdge& le1 = *m_link1.m_pEdge;

					const Vector vertNormalLs = m_link1.m_vertNormalLs;

					ps.EnableWireFrame();

					if (m_link1.m_interior)
					{
						ps.DrawSphere(m_link1.m_vertsLs[0], 0.05f, clrA);
					}
					else
					{
						//ps.DrawArc(m_v1Ls, m_normalLs * pathRadius, le1.m_normalLs * pathRadius, clr, 1.0f, true);
						ps.DrawArrow(m_link1.m_vertsLs[0], m_link1.m_vertsLs[1], 0.2f, clr);
					}

					if (m_link1.m_linkIntersection != -1)
					{
						ps.DrawCross(m_link1.m_vertsLs[1], 0.01f, kColorYellow);
					}
				}
			}
		}

		Point m_v0Ls = kOrigin;
		Point m_v1Ls = kOrigin;
		Vector m_normalLs = kZero;
		Vector m_edgeDirXzLs = kZero;

		Point m_v0Ps = kOrigin;
		Point m_v1Ps = kOrigin;
		Vector m_normalPs = kZero;
		Vector m_edgeDirXzPs = kZero;

		//Point m_orgProjected0Ps = kOrigin;
		//Point m_orgProjected1Ps = kOrigin;
		Point m_projected0Ps = kOrigin;
		Point m_projected1Ps = kOrigin;

		NavManagerId m_sourceNavId;
		NavManagerId m_regTableId;
		U64 m_iEdge = -1;
		const NavMesh* m_pNavMesh = nullptr;
		const NavPoly* m_pNavPoly = nullptr;

		Link m_link0;
		Link m_link1;

		bool m_dontMerge = false;
		bool m_fromLink = false;
		bool m_shadowed = false;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavCorner
	{
		struct Link
		{
			const NavCorner* m_pCorner = nullptr;
			I64 m_iCorner = -1;
		};

		// leaving this empty constructor here (instead of default constructor) so that PGO builds don't fail
		NavCorner() {}

		void DebugDraw(U32F iCorner, float pathRadius, DebugPrimTime tt = kPrimDuration1FrameAuto) const;
		bool ContainsPointLs(Point_arg posLs, float pathRadius, float* pCornerTTOut = nullptr) const;
		float GetClosestTTLs(Point_arg posLs) const;

		NavEdge m_e0;
		NavEdge m_e1;
		NavEdge m_oppEdge;

		Vector m_vertNormal = kZero;
		Point m_portalVerts[8];

		Link m_link0;
		Link m_link1;
		Link m_overlap;

		float m_oppEdgeDist = kLargeFloat;
		float m_e0midTT = 0.5f;
		float m_e1midTT = 0.5f;

		U32F m_numPortalVerts = 0;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavEdgeContext
	{
		NavEdgeContext(const PathFindContext& pathContext,
					   const PathWaypointsEx& inputPathPs,
					   const NavItemRegistrationTable& edgeTable)
			: m_pPathContext(&pathContext), m_pInputPathPs(&inputPathPs), m_pEdgeTable(&edgeTable)
		{
		}

		const PathFindContext* m_pPathContext;
		const PathWaypointsEx* m_pInputPathPs;
		const NavItemRegistrationTable* m_pEdgeTable;

		const NavEdge* m_pEdges = nullptr;
		U32F m_numEdges = 0;

		PathWaypointsEx* m_pPathPsOut = nullptr;
		const NavMesh* m_pCurNavMesh = nullptr;
		const NavMesh* m_pIgnoreReachedNavMesh = nullptr;

		Point m_enterProbeStartPs = kInvalidPoint;
		Point m_exitProbeStartPs = kInvalidPoint;

		Point m_enterPosPs = kInvalidPoint;

		I32F m_iPrevEnterReader = 0;
		I32F m_iPrevEdge = -1;
		I32F m_iEnterEdge = -1;
		I32F m_iEnterReader = -1;
		U32F m_iReader = 0;
		I32F m_enteredLink = -1;

		// NB: positive travel direction means to be moving in the same direction as the edge, i.e. from v0 -> v1
		bool m_travelDir = false;
		bool m_travelDirLooped = false;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	void NavCorner::DebugDraw(U32F iCorner, float pathRadius, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
	{
		STRIP_IN_FINAL_BUILD;

		const NavMesh* pNavMesh = m_e0.m_pNavMesh;

		PrimServerWrapper ps = PrimServerWrapper(pNavMesh->GetOriginWs());
		ps.SetDuration(tt);

		const Vector vo = Vector(0.0f, 0.1f + (float(pNavMesh->GetManagerId().m_navMeshIndex % 3) * 0.1f), 0.0f);
		//const Vector vo = Vector(0.0f, 0.1f + (iCorner % 5) * 0.1f, 0.0f);
		const Color clr = AI::IndexToColor(/*iCorner*/ pNavMesh->GetManagerId().m_navMeshIndex);
		const Color clrA = AI::IndexToColor(/*iCorner*/ pNavMesh->GetManagerId().m_navMeshIndex, 0.33f);

		const Point v = m_e0.m_v1Ls;
		const Point link0Pos = Lerp(m_e0.m_v0Ls, m_e0.m_v1Ls, m_e0midTT);
		const Point link1Pos = Lerp(m_e1.m_v0Ls, m_e1.m_v1Ls, m_e1midTT);

		ps.SetLineWidth(2.0f);
		ps.DrawLine(link0Pos + vo, m_e0.m_v1Ls + vo, clr);
		ps.DrawLine(m_e1.m_v0Ls + vo, link1Pos + vo, clr);

		StringBuilder<256> desc;
		desc.clear();
		desc.append_format("c%d", iCorner);

		ps.DrawString(v + vo, desc.c_str(), clr, 0.75f);

		ps.SetLineWidth(1.0f);
		ps.DrawLine(v, v + vo, clrA);

		for (U32F i = 0; i < m_numPortalVerts; ++i)
		{
			ps.DrawString(m_portalVerts[i] + vo, StringBuilder<32>("%d", i).c_str(), clr, 0.5f);

			if (i > 0)
			{
				ps.SetLineWidth(4.0f);
				ps.DrawLine(m_portalVerts[i - 1], m_portalVerts[i], clr);
				ps.SetLineWidth(1.0f);
				ps.DrawLine(m_portalVerts[i - 1] + vo, m_portalVerts[i] + vo, clrA);
			}
		}

		ps.DrawLine(link0Pos + vo, link0Pos + vo + m_e0.m_normalLs * pathRadius, clr);

		ps.DrawLine(link1Pos + vo, link1Pos + vo + m_e1.m_normalLs * pathRadius, clr);

		const bool interiorCorner = DotXz(-m_e0.m_edgeDirXzLs, m_vertNormal) > 0.0f;

		if (!interiorCorner)
		{
			ps.EnableWireFrame();
			ps.DrawArc(v, m_e0.m_normalLs * pathRadius, m_e1.m_normalLs * pathRadius, clr, 1.5f, true);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool NavCorner::ContainsPointLs(Point_arg posLs, float pathRadius, float* pCornerTTOut /* = nullptr */) const
	{
		bool inside = false;

		Point poly[12];
		U32F polyVertCount = 0;

		for (U32F iV = 0; iV < m_numPortalVerts; ++iV)
		{
			poly[polyVertCount++] = m_portalVerts[iV];
		}

		const Point e0mid = Lerp(m_e0.m_v0Ls, m_e0.m_v1Ls, m_e0midTT);
		const Point e1mid = Lerp(m_e1.m_v0Ls, m_e1.m_v1Ls, m_e1midTT);

		poly[polyVertCount++] = e1mid;
		poly[polyVertCount++] = m_e0.m_v1Ls;
		poly[polyVertCount++] = e0mid;
		poly[polyVertCount++] = m_portalVerts[0]; // loop it up

		const float x = posLs.X();
		const float z = posLs.Z();

		for (U32F iP = 0; iP < (polyVertCount - 1); ++iP)
		{
			const Point p0 = poly[iP];
			const Point p1 = poly[iP + 1];

			const float p0x = p0.X();
			const float p0z = p0.Z();
			const float p1x = p1.X();
			const float p1z = p1.Z();

			if ((((p1z <= z) && (z < p0z)) || ((p0z <= z) && (z < p1z)))
				&& (x < (p0x - p1x) * (z - p1z) / (p0z - p1z) + p1x))
			{
				inside = !inside;
			}
		}

		if (pCornerTTOut)
		{
			const float bestTT = inside ? GetClosestTTLs(posLs) : -1.0f;
			*pCornerTTOut = bestTT;
		}

		return inside;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	float NavCorner::GetClosestTTLs(Point_arg posLs) const
	{
		float bestTT = -1.0f;
		float bestD = kLargeFloat;

		for (U32F iV = 0; iV < (m_numPortalVerts - 1); ++iV)
		{
			const Point v0 = m_portalVerts[iV];
			const Point v1 = m_portalVerts[iV + 1];

			Scalar thisTT;
			const float d = DistPointSegmentXz(posLs, v0, v1, nullptr, &thisTT);

			if (d < bestD)
			{
				bestD = d;
				bestTT = float(iV) + thisTT;
			}
		}

		return bestTT;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavCornerTestResult
	{
		I32 m_iCorner;
		const NavCorner* m_pCorner;
		float m_cornerTT;
		float m_probeTT;
		Point m_hitPointLs;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void SmoothPathApproximate(U32F pathLength,
									  PathWaypointsEx& workingPathPs,
									  const NavNodeData** apNodeDataList,
									  Point truncatedGoalPosPs);

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool FindPortalFromPolygon(NavPortal* pOut,
									  const NavPoly& poly,
									  U32F adjId,
									  Scalar_arg portalShrink,
									  const NavNodeData* pNodeData)
	{
		Point pt0 = kOrigin;
		Point pt1 = kOrigin;

		pOut->Init(kOrigin, kOrigin, NavPortal::Type::kSingular, nullptr);

		bool found = false;

		for (U32F iVert = 0; iVert < poly.GetVertexCount(); ++iVert)
		{
			if (poly.GetAdjacentId(iVert) == adjId)
			{
				pt0	  = poly.GetVertex(iVert);
				pt1	  = poly.GetNextVertex(iVert);
				found = true;
				break;
			}
		}

		if (found)
		{
			if (portalShrink > 0.0f)
			{
				const Vector portalVec = pt1 - pt0;
				const Scalar portalLen = LengthXz(portalVec);
				if (portalLen > 2.0f * portalShrink)
				{
					const Vector portalDir = portalVec * Recip(portalLen);
					const Vector portalAdj = portalDir * portalShrink;
					pt0 += portalAdj;
					pt1 -= portalAdj;
				}
				else
				{
					const Point center = AveragePos(pt0, pt1);
					pt0 = center;
					pt1 = center;
				}
			}
		}
		else if (FALSE_IN_FINAL_BUILD(true))
		{
			MailNavMeshReportTo("ryan_huai@naughtydog.com;john_bellomy@naughtydog.com", "NavPoly adjacency failure");

			NAV_ASSERTF(found,
						("Failed to find adjacent edge to go from poly %d to %d [nav mesh: %s]",
						 poly.GetId(),
						 adjId,
						 poly.GetNavMesh() ? poly.GetNavMesh()->GetName() : "<null>"));
		}
		else
		{
			pt0 = pt1 = poly.GetCentroid();
		}

		pOut->Init(pt0, pt1, NavPortal::Type::kRegular, pNodeData);

		return found;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool FindPortalFromPolygonEx(NavPortal* const pOut,
										const NavPolyEx* const pPolyEx,
										const NavManagerId& adjId,
										Scalar_arg portalShrink,
										const NavNodeData* const pNodeData)
	{
		if (!pPolyEx)
		{
			pOut->Init(kOrigin, kOrigin, NavPortal::Type::kSingular, nullptr);
			return false;
		}

		bool found = false;

		Point pt0 = kOrigin;
		Point pt1 = kOrigin;

		for (U32F iVert = 0; iVert < pPolyEx->GetVertexCount(); ++iVert)
		{
			if (pPolyEx->m_adjPolys[iVert] == adjId)
			{
				pt0 = pPolyEx->GetVertex(iVert);
				pt1 = pPolyEx->GetNextVertex(iVert);
				found = true;
				break;
			}
		}

		if (found)
		{
			const Vector portalVec = pt1 - pt0;
			const Scalar portalLen = LengthXz(portalVec);
			if (portalLen > 2.0f * portalShrink)
			{
				const Vector portalDir = portalVec * Recip(portalLen);
				const Vector portalAdj = portalDir * portalShrink;
				pt0 += portalAdj;
				pt1 -= portalAdj;
			}
			else
			{
				const Point center = AveragePos(pt0, pt1);
				pt0 = center;
				pt1 = center;
			}

		}

		pOut->Init(pt0, pt1, NavPortal::Type::kRegular, pNodeData);

		return found;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool BuildPortalFromWideTap(const NavPathNodeProxy* pPrevNode,
									   const NavNodeData* pPrevData,
									   const NavPathNodeProxy* pCurNode,
									   const NavNodeData* pCurData,
									   I64 instanceSeed,
									   float depenRadius,
									   bool reverseSearch,
									   NavPortal* pPortalOut)
	{
		if (!pCurNode || !pCurData || !pCurNode->IsActionPackNode())
			return false;

		const ActionPack* pAp = pCurNode->GetActionPack();
		if (!pAp || pAp->GetType() != ActionPack::kTraversalActionPack)
			return false;

		const TraversalActionPack* pTap = (const TraversalActionPack*)pAp;

		if (pTap->GetApAnimAdjustType() == TraversalActionPack::AnimAdjustType::kNone)
			return false;

		const TraversalActionPack::AnimAdjustRange& adjustRange = pTap->GetApAnimAdjustRange();

		if (Max(adjustRange.m_min, adjustRange.m_max) < NDI_FLT_EPSILON)
		{
			return false;
		}

		const Locator parentSpace = pCurNode->GetParentSpace();
		const Point curPosWs = parentSpace.TransformPoint(pCurData->m_pathNodePosPs);
		const Point prevPosWs = pPrevData ? pPrevData->m_pathNode.ParentToWorld(pPrevData->m_pathNodePosPs) : curPosWs;

		const NavPathNode::NodeType nodeType = pCurNode->GetNodeType();

		Point v0Ws, v1Ws;
		if (!pTap->GetAnimAdjustNavPortalWs(prevPosWs, curPosWs, nodeType, depenRadius, v0Ws, v1Ws, instanceSeed))
		{
			return false;
		}

		if (reverseSearch)
		{
			Swap(v0Ws, v1Ws);
		}

		if (pPortalOut)
		{
			const Point v0Ps = parentSpace.UntransformPoint(v0Ws);
			const Point v1Ps = parentSpace.UntransformPoint(v1Ws);
			const Vector baseNormVecPs = (nodeType == NavPathNode::kNodeTypeActionPackEnter) ? (v1Ps - v0Ps)
																							 : (v0Ps - v1Ps);

			const Vector normalPs = AsUnitVectorXz(RotateY90(baseNormVecPs), kZero);
			const Vector normalWs = parentSpace.TransformVector(normalPs);

			pPortalOut->Init(v0Ws, v1Ws, normalWs, NavPortal::Type::kWideTap, pCurData);
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool PortalsIntersect(const NavPortal& a, const NavPortal& b, Scalar& t0, Scalar& t1)
	{
		const Segment segA = Segment(a.m_pos[0], a.m_pos[1]);
		const Segment segB = Segment(b.m_pos[0], b.m_pos[1]);

		return IntersectSegmentSegmentXz(segA, segB, t0, t1);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ClipWideTapPortal(NavPortal& portalPs, NavPortal& tapPortalPs, float tt)
	{
		const float epsilon = 0.00001f;
		const float dotP = DotXz(portalPs.m_pos[0] - tapPortalPs.m_pos[0], tapPortalPs.m_normal);
		const I32F iSelectVert = (dotP > epsilon) ? 0 : 1;
		const I32F iOtherVert = iSelectVert ^ 1;

		const Point newPosPs = Lerp(portalPs.m_pos[0], portalPs.m_pos[1], tt);
		portalPs.m_pos[iOtherVert] = newPosPs;
		tapPortalPs.m_pos[iOtherVert] = newPosPs;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F TrimPortalsForWideTaps(NavPortal* aPortalListPs, U32F pathLength)
	{
		if (!aPortalListPs || (pathLength < 2))
			return pathLength;

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		ExternalBitArray* pValidPortals = NDI_NEW(kAlign16) ExternalBitArray[pathLength];
		const size_t alignedBitSize = AlignSize(ExternalBitArray::DetermineCapacity(pathLength), kAlign64);
		U64* pBits = NDI_NEW(kAlign64) U64[alignedBitSize / sizeof(U64)];
		pValidPortals->Init(alignedBitSize, pBits, true);

		for (U32F iPortal = 1; iPortal < pathLength - 1; ++iPortal)
		{
			NavPortal& tapPortalPs = aPortalListPs[iPortal];

			if (tapPortalPs.m_type != NavPortal::Type::kWideTap)
				continue;

			for (U32F iOtherPortal = 1; iOtherPortal < pathLength - 1; ++iOtherPortal)
			{
				if (iOtherPortal == iPortal)
					continue;

				NavPortal& otherPortalPs = aPortalListPs[iOtherPortal];

				Scalar t0, t1;
				if (PortalsIntersect(otherPortalPs, tapPortalPs, t0, t1))
				{
					if (otherPortalPs.m_type == NavPortal::Type::kWideTap)
					{
						ClipWideTapPortal(otherPortalPs, tapPortalPs, t0);
					}
					else
					{
						pValidPortals->ClearBit(iOtherPortal);
					}
				}
			}
		}

		U32F newPathLength = 0;

		ExternalBitArray::Iterator itr(*pValidPortals);
		for (U64 iPortal = itr.First(); iPortal < pathLength; iPortal = itr.Advance())
		{
			aPortalListPs[newPathLength] = aPortalListPs[iPortal];
			++newPathLength;
		}

		return newPathLength;
	}

#ifndef FINAL_BUILD
	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugPrintNavManagerId(const char* pEntryName, const NavManagerId& navMgrId)
	{
		if (navMgrId.IsValid())
		{
			MsgOut("  %s: [NavMgrId: %d %d %d %d]\n",
				   pEntryName,
				   navMgrId.m_navMeshIndex,
				   navMgrId.m_uniqueId,
				   navMgrId.m_iPoly,
				   navMgrId.m_iPolyEx);
		}
		else
		{
			MsgOut("  %s: [NavMgrId: invalid]\n", pEntryName);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugPrintPolyExFindPortal(const NavPolyEx* const pPolyEx,
									const NavManagerId& adjId,
									bool isBaseContained,
									bool isContained,
									bool isFound)
	{
		MsgOut("[Level: %s] [NavMesh: %s] [BasePoly: %d] [NavPolyEx: %d] [%d %d %d]\n",
			   DevKitOnly_StringIdToString(pPolyEx->GetNavMesh()->GetLevelId()),
			   pPolyEx->GetNavMesh()->GetName(),
			   pPolyEx->GetBasePoly()->GetId(),
			   pPolyEx->GetId(),
			   (int)isBaseContained,
			   (int)isContained,
			   (int)isFound);

		DebugPrintNavManagerId("self", pPolyEx->GetNavManagerId());

		for (U32F iVert = 0; iVert < pPolyEx->GetVertexCount(); iVert++)
		{
			DebugPrintNavManagerId("adjacent", pPolyEx->m_adjPolys[iVert]);
		}

		DebugPrintNavManagerId("target", adjId);
	}
#endif

	/// --------------------------------------------------------------------------------------------------------------- ///
	const NavPolyEx* FindBestIncomingPolyEx(const NavPoly& prevPoly, NavManagerId destNavId, Point_arg posLs)
	{
		const NavPolyEx* pBestEx = nullptr;
		float bestRating = kLargeFloat;

		for (const NavPolyEx* pPolyEx = prevPoly.GetNavPolyExList(); pPolyEx; pPolyEx = pPolyEx->m_pNext)
		{
			bool isAdj = false;
			for (int i = 0; i < pPolyEx->GetVertexCount(); ++i)
			{
				if ((pPolyEx->GetNavManagerId() == destNavId) || (pPolyEx->m_adjPolys[i] == destNavId))
				{
					isAdj = true;
					break;
				}
			}

			if (!isAdj)
				continue;

			const float sd = pPolyEx->SignedDistPointPolyXzSqr(posLs);

			if ((sd < bestRating) || !pBestEx)
			{
				pBestEx = pPolyEx;
				bestRating = sd;

				if (sd < 0.0f)
					break;
			}
		}

		return pBestEx;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	//
	// walk the list of nodes and generate a list of portals for the path to be threaded through
	//
	// each portal represents a transition from one node to the next
	// (except for the last bogus portal we add for the goal point)
	//
	static U32F BuildPortalList(const PathFindContext& pathContext,
								const BuildPathParams& buildParams,
								Point_arg startPosPs,
								Point_arg goalPosPs,
								const NavNodeKey* aKeyList,
								const NavNodeData** apDataList,
								const size_t pathLength,
								float tapDepenRadius,
								NavPortal* pPortalsOut)
	{
		if (pathLength < 2)
			return 0;

		const float portalShrink = buildParams.m_portalShrink;

		const NavMeshHandle hStartMesh = apDataList[0]->m_pathNode.GetNavMeshHandle();
		const NavMesh* pStartMesh = hStartMesh.ToNavMesh();

		NavMeshHandle hPrevMesh = hStartMesh;
		const NavMesh* pPrevMesh = pStartMesh;

		pPortalsOut[0].Init(startPosPs, startPosPs, NavPortal::Type::kSingular, apDataList[0]);

		// generate all of the portals (into parent space)
		U32F portalCount = 1;

		for (U32F iiNode = 1; iiNode < pathLength - 1; ++iiNode)
		{
			NavPortal& portal = pPortalsOut[portalCount];
			const NavNodeKey curKey = aKeyList[iiNode];
			const U32F iCurNode = curKey.GetPathNodeId();

			const NavNodeData* pPrevData = apDataList[iiNode - 1];
			const NavNodeData* pCurData = apDataList[iiNode];

			const NavPathNodeProxy* pPrevNode = &pPrevData->m_pathNode;
			const NavPathNodeProxy* pCurNode = &pCurData->m_pathNode;

			const NavMeshHandle hCurMesh = pCurNode->GetNavMeshHandle();
			const NavMesh* pCurMesh = hCurMesh.ToNavMesh();

			NavPathNode::NodeType prevNodeType = pPrevNode->GetNodeType();
			if (prevNodeType == NavPathNode::kNodeTypeActionPackExit)
			{
				if ((pPrevNode->GetNavManagerId().m_iPolyEx != 0) || (pCurNode->GetNodeType() == NavPathNode::kNodeTypePolyEx))
				{
					prevNodeType = NavPathNode::kNodeTypePolyEx;
				}
				else if (pPrevNode->GetNavManagerId().IsValid())
				{
					prevNodeType = NavPathNode::kNodeTypePoly;
				}
#if ENABLE_NAV_LEDGES
				else if (!pPrevNode->GetNavLedgeHandle().IsNull())
				{
					prevNodeType = NavPathNode::kNodeTypeNavLedge;
				}
#endif // ENABLE_NAV_LEDGES
			}

			if (BuildPortalFromWideTap(pPrevNode,
									   pPrevData,
									   pCurNode,
									   pCurData,
									   buildParams.m_wideTapInstanceSeed,
									   tapDepenRadius,
									   pathContext.m_reverseSearch,
									   &portal))
			{
			}
			else if (pCurNode->IsNavMeshNode() && NavPathNode::IsActionPackNode(prevNodeType))
			{
				// don't bother generating portals between nav mesh nodes and TAPs
				continue;
			}
			else if (!pCurNode->IsNavMeshNode() || !NavPathNode::IsNavMeshNode(prevNodeType))
			{
				const Point posPs = pCurData->m_pathNodePosPs;
				const Point posWs = pCurData->m_pathNode.ParentToWorld(posPs);

				portal.Init(posWs, posWs, pCurNode->IsNavMeshNode() ? NavPortal::Type::kRegular : NavPortal::Type::kSingular, pCurData);
			}
			else if (prevNodeType == NavPathNode::kNodeTypePoly)
			{
				// current and next nodes are both nav-poly nodes
				NAV_ASSERT(pPrevMesh);
				NAV_ASSERT(pCurNode->IsNavMeshNode());

				const NavPoly& prevPoly = pPrevMesh->GetPoly(pPrevNode->GetNavManagerId().m_iPoly);

				U32F iCurPolyId = pCurNode->GetNavManagerId().m_iPoly;

				// if next node is in a different nav mesh from the current node,
				if (hPrevMesh != hCurMesh)
				{
					// we need to get the id of the link poly in the current mesh
					//   in order to properly construct the portal to the next nav mesh
					bool foundNextId = false;
					for (U32F iEdge = 0; iEdge < prevPoly.GetVertexCount(); ++iEdge)
					{
						U32F iAdjId = prevPoly.GetAdjacentId(iEdge);
						if (iAdjId != NavPoly::kNoAdjacent)
						{
							const NavPoly& adjPoly = pPrevMesh->GetPoly(iAdjId);
							if (adjPoly.IsLink())
							{
								const NavMeshLink& link = pPrevMesh->GetLink(adjPoly.GetLinkId());
								if ((link.GetDestNavMesh() == hCurMesh) &&
									(link.GetDestPreLinkPolyId() == iCurPolyId))
								{
									iCurPolyId = iAdjId;
									foundNextId = true;
									break;
								}
							}
						}
					}
					NAV_ASSERT(foundNextId);
				}

				const bool found = FindPortalFromPolygon(&portal, prevPoly, iCurPolyId, portalShrink, pCurData);
				NAV_ASSERT(found);

				portal.LocalToWorld(pPrevMesh);
			}
			else if (prevNodeType == NavPathNode::kNodeTypePolyEx)
			{
				const NavPoly& prevPoly = pPrevMesh->GetPoly(pPrevNode->GetNavManagerId().m_iPoly);

				const bool prevNodePolyExValid = (pPrevNode->GetNavManagerId().m_iPolyEx != 0);
				const NavPolyEx* pPrevPolyEx = nullptr;

				const NavManagerId adjId = pCurNode->GetNavManagerId();

				if (prevNodePolyExValid)
				{
					pPrevPolyEx = pPrevNode->GetNavPolyExHandle().ToNavPolyEx();
				}
				else
				{
					const Point prevPosLs = pPrevMesh->ParentToLocal(pPrevData->m_pathNodePosPs);
					pPrevPolyEx = FindBestIncomingPolyEx(prevPoly, adjId, prevPosLs);
				}

				if (!pPrevPolyEx)
				{
					const Point posPs = pCurData->m_pathNodePosPs;
					const Point posWs = pCurData->m_pathNode.ParentToWorld(posPs);
					portal.Init(posWs, posWs, NavPortal::Type::kSingular, pCurData);
				}
				else if (adjId.m_iPolyEx == pPrevPolyEx->GetId())
				{
					// Can happen if tap exits to the same nav poly ex
					continue;
				}
				else
				{
					const bool found = FindPortalFromPolygonEx(&portal, pPrevPolyEx, adjId, portalShrink, pCurData);
#ifndef FINAL_BUILD
					if (!found)
					{
						MsgOut("\n\n=============================[ FindPortalFromPolygonEx(...) failure ]=============================\n");

						const NavMesh* pNavMesh;
						Point posLs;
						Vec4 vDots;
						bool isBaseContained;
						bool isContained;
						bool isFound;

						{
							pNavMesh = pPrevPolyEx->GetNavMesh();
							posLs	 = pNavMesh->ParentToLocal(pPrevData->m_pathNodePosPs);

							isBaseContained = pPrevPolyEx->GetBasePoly()->PolyContainsPointLs(posLs);
							isContained		= pPrevPolyEx->PolyContainsPointLs(posLs, &vDots);
							isFound			= false;

							DebugPrintPolyExFindPortal(pPrevPolyEx, adjId, isBaseContained, isContained, isFound);
						}

						if (!prevNodePolyExValid)
						{
							MsgOut("\nprevNodePolyExValid: false, testing entire prevPoly\n\n");

							pNavMesh = prevPoly.GetNavMesh();
							posLs	 = pNavMesh->ParentToLocal(pPrevData->m_pathNodePosPs);

							for (const NavPolyEx* pPolyEx = prevPoly.GetNavPolyExList(); pPolyEx; pPolyEx = pPolyEx->m_pNext)
							{
								isBaseContained = pPolyEx->GetBasePoly()->PolyContainsPointLs(posLs);
								isContained		= pPolyEx->PolyContainsPointLs(posLs, &vDots);
								isFound = FindPortalFromPolygonEx(&portal, pPolyEx, adjId, portalShrink, pCurData);

								DebugPrintPolyExFindPortal(pPolyEx, adjId, isBaseContained, isContained, isFound);
							}
						}

						MsgOut("\n=================================================================================================\n");

						MailNavMeshReportTo("ryan_huai@naughtydog.com;john_bellomy@naughtydog.com", "[MAIL_ASSERT] FindPortalFromPolygonEx(...) failure");
						NAV_ASSERTF(false, ("[ FindPortalFromPolygonEx(...) failure ]: %s, %d, %s", FILE_LINE_FUNC));
					}
#endif

					portal.LocalToWorld(pPrevMesh);
				}
			}
			else
			{
				NAV_ASSERTF(false, ("Nav Path Node '%d' has unexpected type '%d'", iCurNode, pCurNode->GetNodeType()));
			}

			hPrevMesh = hCurMesh;
			pPrevMesh = pCurMesh;
			++portalCount;
		}

		// put everything in our path find parent space (except our start and goal dummies which we already added in parent space)
		for (U32F iPortal = 1; iPortal < portalCount; ++iPortal)
		{
			NavPortal& portal = pPortalsOut[iPortal];
			portal.m_pos[0] = pathContext.m_parentSpace.UntransformPoint(portal.m_pos[0]);
			portal.m_pos[1] = pathContext.m_parentSpace.UntransformPoint(portal.m_pos[1]);
		}

		// add a bogus portal for the goal
		pPortalsOut[portalCount].Init(goalPosPs, goalPosPs, NavPortal::Type::kRegular, apDataList[pathLength - 1]);
		portalCount++;

		portalCount = TrimPortalsForWideTaps(pPortalsOut, portalCount);

		if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawPortals))
		{
			PrimServerWrapper ps = PrimServerWrapper(pathContext.m_parentSpace);
			ps.SetLineWidth(3.0f);
			ps.EnableHiddenLineAlpha();
			ps.SetDuration(buildParams.m_debugDrawTime);

			for (U32F iPortal = 0; iPortal < portalCount; ++iPortal)
			{
				const NavPortal& portal = pPortalsOut[iPortal];
				const Vector offset(0, 0.02f * float(iPortal), 0);
				const Color clr = NavPortal::TypeToDebugColor(portal.m_type);
				const Color clrA = Color(clr, 0.3f);
				ps.DrawArrow(portal.m_pos[0] + offset, portal.m_pos[1] + offset, 0.3f, clrA);
				ps.DrawCross(portal.m_pos[0] + offset, 0.15f, clrA);
				ps.DrawCross(portal.m_pos[1] + offset, 0.15f, clrA);
				ps.DrawString(AveragePos(portal.m_pos[0], portal.m_pos[1]) + offset, StringBuilder<128>("%d", (int)iPortal).c_str(), clr, 0.65f);
			}
		}

		return portalCount;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static NavManagerId GetAdjacentNavId(const NavMesh* pMesh, const NavPoly* pPoly, const NavPolyEx* pPolyEx, U32F iEdge)
	{
		PROFILE_ACCUM(GetAdjacentNavId);

		NavManagerId ret;

		ret.Invalidate();

		if (pPolyEx)
		{
			ret = pPolyEx->GetAdjacentPolyId(iEdge);
		}
		else if (pPoly)
		{
			const U32F adjPolyId = pPoly->GetAdjacentId(iEdge);

			if (adjPolyId != NavPoly::kNoAdjacent)
			{
				const NavPoly& adjPoly = pMesh->GetPoly(adjPolyId);

				ret = pMesh->GetManagerId();
				ret.m_iPoly = adjPolyId;

				NavManagerId sourceNavId = ret;
				sourceNavId.m_iPoly = pPoly->GetId();

				for (const NavPolyEx* pAdjEx = adjPoly.GetNavPolyExList(); pAdjEx; pAdjEx = pAdjEx->GetNextPolyInList())
				{
					const U32F vertCount = pAdjEx->GetVertexCount();
					for (U32F iV = 0; iV < vertCount; ++iV)
					{
						if (pAdjEx->GetAdjacentPolyId(iV) == sourceNavId)
						{
							ret.m_iPolyEx = pAdjEx->GetId();
							break;
						}
					}

					if (ret.m_iPolyEx)
					{
						break;
					}
				}
			}
		}

		return ret;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F EliminateNarrowWedges(NavEdge* pEdgesOut, U32F numInputEdges, float pathRadius)
	{
		PROFILE_AUTO(Navigation);

		PROFILE_ACCUM(EliminateNarrowWedges);

		U32F numEdges = numInputEdges;
		const float pathDiameter = (2.0f * pathRadius);

		while (true)
		{
			bool eliminatedEdge = false;

			for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
			{
				const NavEdge& e0 = pEdgesOut[iEdge];

				if (e0.m_dontMerge)
					continue;

				for (U32F iOtherEdge = 0; iOtherEdge < numEdges; ++iOtherEdge)
				{
					if (iEdge == iOtherEdge)
						continue;

					const NavEdge& e1 = pEdgesOut[iOtherEdge];

					if (e1.m_dontMerge)
						continue;

					if (e0.m_sourceNavId.m_navMeshIndex != e1.m_sourceNavId.m_navMeshIndex)
						continue;

					if (Dist(e0.m_v1Ls, e1.m_v0Ls) > 0.0001f)
						continue;

					const Vector vertNormal = AsUnitVectorXz(e0.m_normalLs + e1.m_normalLs, kZero);
					const bool interiorCorner = DotXz(SafeNormalize(e0.m_v0Ls - e0.m_v1Ls, kZero), vertNormal) > 0.2f;

					if (!interiorCorner)
						continue;

					Point closestPt;
					Scalar tt;

					bool shouldMerge = false;

					if (!shouldMerge)
					{
						const float wedgeDist = DistXz(e0.m_v0Ls, e1.m_v1Ls);
						if (wedgeDist < pathDiameter)
						{
							shouldMerge = true;
						}
					}

					if (!shouldMerge)
					{
						const float edgeLen0 = DistXz(e0.m_v0Ls, e0.m_v1Ls);
						const float edgeLen1 = DistXz(e1.m_v0Ls, e1.m_v1Ls);
						const float shortEdgeLen = Min(edgeLen0, edgeLen1);

						if (shortEdgeLen < (pathDiameter * 0.25f))
						{
							shouldMerge = true;
						}
					}

					if (!shouldMerge)
					{
						const float normDot = DotXz(e0.m_normalLs, e1.m_normalLs);
						if (normDot < 0.1f)
						{
							const float opVertDist = DistPointSegmentXz(e0.m_v1Ls, e0.m_v0Ls, e1.m_v1Ls);
							if (opVertDist < pathDiameter)
							{
								shouldMerge = true;
							}
						}
					}

					if (shouldMerge)
					{
						const Point e10Ws = e0.m_pNavMesh->LocalToWorld(e1.m_v0Ls);

						if (e0.m_fromLink || e1.m_fromLink)
						{
							for (U32F iLinkEdge = 0; iLinkEdge < numEdges; ++iLinkEdge)
							{
								NavEdge& linkEdge = pEdgesOut[iLinkEdge];

								if (iLinkEdge == iEdge || iLinkEdge == iOtherEdge)
									continue;

								if (linkEdge.m_dontMerge)
									continue;

								if (!linkEdge.m_pNavPoly->IsLink() && !linkEdge.m_pNavPoly->IsPreLink())
									continue;

								if (linkEdge.m_sourceNavId.m_navMeshIndex == e0.m_sourceNavId.m_navMeshIndex)
									continue;

								const Point link0Ws = linkEdge.m_pNavMesh->LocalToWorld(linkEdge.m_v0Ls);
								const Point link1Ws = linkEdge.m_pNavMesh->LocalToWorld(linkEdge.m_v1Ls);

								//const float d00 = Dist(e00Ws, link0Ws);
								//const float d01 = Dist(e00Ws, link1Ws);
								const float d10 = Dist(e10Ws, link0Ws);
								//const float d11 = Dist(e10Ws, link1Ws);

								if (d10 > 0.001f)
									continue;

								const Point e00Ws = e0.m_pNavMesh->LocalToWorld(e0.m_v0Ls);
								const Point e11Ws = e1.m_pNavMesh->LocalToWorld(e1.m_v1Ls);

								linkEdge.m_v0Ls = linkEdge.m_pNavMesh->WorldToLocal(e00Ws);
								linkEdge.m_v1Ls = linkEdge.m_pNavMesh->WorldToLocal(e11Ws);
								linkEdge.m_edgeDirXzLs = AsUnitVectorXz(linkEdge.m_v1Ls - linkEdge.m_v0Ls, kZero);
								linkEdge.m_normalLs = RotateY90(linkEdge.m_edgeDirXzLs);
								linkEdge.m_fromLink = true;

								break;
							}
						}

						pEdgesOut[iEdge].m_v0Ls = e0.m_v0Ls;
						pEdgesOut[iEdge].m_v1Ls = e1.m_v1Ls;
						pEdgesOut[iEdge].m_edgeDirXzLs = AsUnitVectorXz(e1.m_v1Ls - e0.m_v0Ls, kZero);
						pEdgesOut[iEdge].m_normalLs = RotateY90(pEdgesOut[iEdge].m_edgeDirXzLs);
						pEdgesOut[iEdge].m_fromLink = pEdgesOut[iEdge].m_fromLink || pEdgesOut[iOtherEdge].m_fromLink;

						pEdgesOut[iOtherEdge] = pEdgesOut[numEdges - 1];
						pEdgesOut[numEdges - 1] = NavEdge();

						--numEdges;

						eliminatedEdge = true;

						break;
					}
				}

				if (eliminatedEdge)
					break;
			}

			if (!eliminatedEdge)
				break;
		}

		return numEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F RecordBlockingEdges(const NavMesh* pMesh,
									const NavPoly* pPoly,
									const NavPolyEx* pPolyEx,
									const Nav::StaticBlockageMask obeyedStatickBlockageMask,
									const NavBlockerBits& obeyedBlockers,
									float pathRadius,
									NavEdge* pEdgesOut,
									U32F maxEdgesOut,
									U32F& edgeId)
	{
		if (!pMesh || !pPoly || !pEdgesOut || (0 == maxEdgesOut))
			return 0;

		const bool isLink = pPoly->IsLink();
		const bool isPreLink = pPoly->IsPreLink();

		U32F iSkip = -1;

		const U32F vertexCount = pPolyEx ? pPolyEx->GetVertexCount() : pPoly->GetVertexCount();

		if (isLink)
		{
			for (U32F iV = 0; iV < vertexCount; ++iV)
			{
				if (pPoly->GetAdjacentId(iV) != NavPoly::kNoAdjacent)
				{
					iSkip = iV + 2;
					break;
				}
			}
		}

		NavMesh::BaseProbeParams probeParams;
		probeParams.m_obeyedStaticBlockers = obeyedStatickBlockageMask;
		probeParams.m_obeyedBlockers = obeyedBlockers;
		probeParams.m_dynamicProbe = true;

		U32F numEdges = 0;

		for (U32F iV = 0; iV < vertexCount; ++iV)
		{
			if (UNLIKELY(iV == iSkip))
				continue;

			const NavMesh::BoundaryFlags boundaryFlags = NavMesh::IsBlockingEdge(pMesh, pPoly, pPolyEx, iV, probeParams);

			if (boundaryFlags == NavMesh::kBoundaryNone)
			{
				continue;
			}

			if (numEdges < maxEdgesOut)
			{
				const Point v0Ls = pPolyEx ? pPolyEx->GetVertex(iV) : pPoly->GetVertex(iV);
				const Point v1Ls = pPolyEx ? pPolyEx->GetNextVertex(iV) : pPoly->GetNextVertex(iV);
				const Vector edgeDirLsXz = AsUnitVectorXz(v1Ls - v0Ls, kZero);
				const Vector normalLs = RotateY90(edgeDirLsXz);

				NavEdge& newEdge = pEdgesOut[numEdges];

				newEdge.m_v0Ls		  = v0Ls;
				newEdge.m_v1Ls		  = v1Ls;
				newEdge.m_normalLs	  = normalLs;
				newEdge.m_edgeDirXzLs = edgeDirLsXz;

				newEdge.m_sourceNavId = pPolyEx ? pPolyEx->GetNavManagerId() : pPoly->GetNavManagerId();
				newEdge.m_regTableId  = pEdgesOut[numEdges].m_sourceNavId;
				newEdge.m_regTableId.m_iPolyEx = 0;
				newEdge.m_pNavMesh	= pMesh;
				newEdge.m_pNavPoly	= pPoly;
				newEdge.m_dontMerge = false;
				newEdge.m_fromLink	= isLink || isPreLink;

				newEdge.m_v0Ps		  = pMesh->LocalToParent(v0Ls);
				newEdge.m_v1Ps		  = pMesh->LocalToParent(v1Ls);
				newEdge.m_normalPs	  = pMesh->LocalToParent(normalLs);
				newEdge.m_edgeDirXzPs = pMesh->LocalToParent(edgeDirLsXz);

				newEdge.m_projected0Ps	  = newEdge.m_v0Ps + (newEdge.m_normalPs * pathRadius);
				newEdge.m_projected1Ps	  = newEdge.m_v1Ps + (newEdge.m_normalPs * pathRadius);
				//newEdge.m_orgProjected0Ps = newEdge.m_projected0Ps;
				//newEdge.m_orgProjected1Ps = newEdge.m_projected1Ps;

				newEdge.m_iEdge = edgeId++;

				++numEdges;
			}
			else
			{
				break;
			}
		}

		return numEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void RecordBlockingEdgesFromPoly(const PathFindContext& pathContext,
											const NavMesh* pMesh,
											const NavPoly* pPoly,
											NavEdge* pEdgesOut,
											U32F maxEdgesOut,
											U32F& numEdges,
											U32F& edgeId)
	{
		PROFILE_AUTO(Navigation);

		if (pPoly->IsBlocked(pathContext.m_obeyedStaticBlockers))
			return;

		const bool debugDraw = false;

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			Color c = AI::IndexToColor(pMesh->GetManagerId().m_navMeshIndex);
			c.SetA(0.33f);
			pPoly->DebugDraw(c);
		}

		const U32F initialCount = numEdges;

		if (const NavPolyEx* pExPolys = pathContext.m_dynamicSearch ? pPoly->GetNavPolyExList() : nullptr)
		{
			for (const NavPolyEx* pPolyEx = pExPolys; pPolyEx; pPolyEx = pPolyEx->GetNextPolyInList())
			{
				if (pPolyEx->IsBlockedBy(pathContext.m_obeyedBlockers))
					continue;

				numEdges += RecordBlockingEdges(pMesh,
												pPoly,
												pPolyEx,
												pathContext.m_obeyedStaticBlockers,
												pathContext.m_obeyedBlockers,
												pathContext.m_pathRadius,
												&pEdgesOut[numEdges],
												maxEdgesOut - numEdges,
												edgeId);
			}
		}
		else
		{
			numEdges += RecordBlockingEdges(pMesh,
											pPoly,
											nullptr,
											pathContext.m_obeyedStaticBlockers,
											pathContext.m_obeyedBlockers,
											pathContext.m_pathRadius,
											&pEdgesOut[numEdges],
											maxEdgesOut - numEdges,
											edgeId);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct VisitedPolyEntry
	{
		NavMeshHandle m_hNavMesh;
		ExternalBitArray m_visitedPolys;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class VisitedPolyTracker
	{
	public:
		VisitedPolyTracker()
		{
			m_aEntries = NDI_NEW VisitedPolyEntry[NavMeshMgr::kMaxNavMeshCount];
			m_numEntries = 0;
		}

		VisitedPolyEntry& GetOrCreateEntry(const NavMesh* pMesh)
		{
			const NavMeshHandle hMesh = pMesh;

			I32F iExisting = -1;

			for (U32F iEntry = 0; iEntry < m_numEntries; ++iEntry)
			{
				if (m_aEntries[iEntry].m_hNavMesh == hMesh)
				{
					return m_aEntries[iEntry];
				}
			}

			NAV_ASSERT(m_numEntries < NavMeshMgr::kMaxNavMeshCount);

			VisitedPolyEntry& newEntry = m_aEntries[m_numEntries];
			++m_numEntries;

			const U32F polyCount = pMesh->GetPolyCount();
			newEntry.m_hNavMesh = hMesh;
			U64* aBlocks = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(polyCount)];
			newEntry.m_visitedPolys.Init(polyCount, aBlocks, false);

			return newEntry;
		}

		VisitedPolyEntry* m_aEntries;
		U32F m_numEntries;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F RecordBlockingEdgesFromPathPoly(const PathFindContext& pathContext,
												const NavMesh* pMesh,
												const NavPoly* pPoly,
												VisitedPolyTracker& polyTracker,
												const NavPoly** apPolys,
												NavEdge* pEdgesOut,
												U32F maxEdgesOut,
												U32F numEdges,
												U32F& edgeId,
												U32F& numPolysRecorded)
	{
		PROFILE_AUTO(Navigation);

		if (!pPoly)
			return numEdges;

		const NavPoly* pPolyToUse = pPoly;
		const NavMesh* pMeshToUse = (pPoly->GetNavMeshHandle() == NavMeshHandle(pMesh)) ? pMesh : pPoly->GetNavMesh();

		if (!pMeshToUse)
			return numEdges;

		VisitedPolyEntry& entry = polyTracker.GetOrCreateEntry(pMeshToUse);
		VisitedPolyEntry* pEntryToUse = &entry;

		// JDB: we actually need blocking edges from link polys because if they are kinked with the pre-link (...)
		// then we need to create radial corners from the joint so we don't get degenerate path builds
/*
		const NavMesh* pLinkedMesh = nullptr;
		const NavPoly* pLinkedPoly = pPoly->IsLink() ? pPoly->GetLinkedPoly(&pLinkedMesh) : nullptr;

		if (pLinkedPoly)
		{
			const U32F linkedPolyId = pLinkedPoly->GetId();

			VisitedPolyEntry& linkedEntry = polyTracker.GetOrCreateEntry(pLinkedMesh);
			NAV_ASSERT(linkedEntry.m_hNavMesh == pLinkedMesh);

			pEntryToUse = &linkedEntry;
			pPolyToUse = pLinkedPoly;
			pMeshToUse = pLinkedMesh;
		}
*/

		NAV_ASSERT(pEntryToUse->m_hNavMesh == pMeshToUse);

		const U32F polyId = pPolyToUse->GetId();

		if (!pEntryToUse->m_visitedPolys.IsBitSet(polyId))
		{
			RecordBlockingEdgesFromPoly(pathContext, pMeshToUse, pPolyToUse, pEdgesOut, maxEdgesOut, numEdges, edgeId);
			++numPolysRecorded;
			pEntryToUse->m_visitedPolys.SetBit(polyId);
		}

		if (const NavPolyDistEntry* pDistTable = pPolyToUse->GetPolyDistList())
		{
			const U32F polyCount = pMeshToUse->GetPolyCount();

			for (U32F i = 0; pDistTable[i].GetDist() < (pathContext.m_pathRadius * 1.15f); ++i)
			{
				const U32F iPoly = pDistTable[i].GetPolyIndex();
				if (iPoly >= polyCount)
					break;

				const NavPoly& otherPoly = pMeshToUse->GetPoly(iPoly);

				if (otherPoly.IsLink())
					continue;

				if (pEntryToUse->m_visitedPolys.IsBitSet(iPoly))
					continue;

				RecordBlockingEdgesFromPoly(pathContext, pMeshToUse, &otherPoly, pEdgesOut, maxEdgesOut, numEdges, edgeId);
				++numPolysRecorded;
				pEntryToUse->m_visitedPolys.SetBit(iPoly);
			}
		}

		return numEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct RecordEdgesVisitData
	{
		VisitedPolyTracker* m_pPolyTracker = nullptr;
		const PathFindContext* m_pPathContext = nullptr;
		const NavPoly** m_apPolys = nullptr;

		const NavMesh* m_pNavMesh;
		U32F m_numEdges;
		U32F m_edgeId;
		U32F m_numPolysRecorded;
		NavEdge* m_pEdgesOut;

		U32F m_maxEdgesOut;
		bool m_debugDraw;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void cbVisitRecordPolyEdges(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData)
	{
		if (!pPoly)
			return;

		RecordEdgesVisitData& data = *(RecordEdgesVisitData*)userData;

		data.m_numEdges = RecordBlockingEdgesFromPathPoly(*data.m_pPathContext,
														  data.m_pNavMesh,
														  pPoly,
														  *data.m_pPolyTracker,
														  data.m_apPolys,
														  data.m_pEdgesOut,
														  data.m_maxEdgesOut,
														  data.m_numEdges,
														  data.m_edgeId,
														  data.m_numPolysRecorded);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F RecordBlockingEdgesFromMesh(const PathFindContext& pathContext,
											const NavMesh* pMesh,
											const NavPoly* pStartPoly,
											const Segment& probeSegLs,
											VisitedPolyTracker& polyTracker,
											const NavPoly** apPolys,
											NavEdge* pEdgesOut,
											U32F maxEdgesOut,
											U32F numEdges,
											U32F& edgeId,
											U32F& numPolysRecorded)
	{
		PROFILE_AUTO(Navigation);

		if (!pMesh)
		{
			return numEdges;
		}

		NavMesh::ProbeParams params;
		params.m_start		  = probeSegLs.a;
		params.m_move		  = probeSegLs.b - probeSegLs.a;
		params.m_pStartPoly	  = pStartPoly;
		params.m_dynamicProbe = false;
		params.m_probeRadius  = 0.0f;
		params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

		RecordEdgesVisitData data;
		data.m_pPolyTracker = &polyTracker;
		data.m_pPathContext = &pathContext;
		data.m_pNavMesh		= pMesh;
		data.m_numEdges		= numEdges;
		data.m_edgeId		= edgeId;
		data.m_numPolysRecorded = numPolysRecorded;
		data.m_pEdgesOut		= pEdgesOut;
		data.m_maxEdgesOut		= maxEdgesOut;
		data.m_debugDraw		= false;
		data.m_apPolys = apPolys;

		pMesh->WalkPolysInLineLs(params, cbVisitRecordPolyEdges, (uintptr_t)&data);

		edgeId = data.m_edgeId;
		numPolysRecorded = data.m_numPolysRecorded;

		return data.m_numEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F BuildPotentialEdgeList(const PathFindContext& pathContext,
									   const BuildPathParams& buildParams,
									   const NavNodeData** apDataList,
									   U32F numNodes,
									   NavEdge* pEdgesOut,
									   U32F maxEdgesOut)
	{
		PROFILE_AUTO(Navigation);

		PROFILE_ACCUM(BuildPotentialEdgeList);

		if (!apDataList || (0 == numNodes))
			return 0;

		if (!pEdgesOut || (0 == maxEdgesOut))
			return 0;

		U32F numEdges = 0;
		U32F edgeId = 0;
		U32F numPolysRecorded = 0;

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
		VisitedPolyTracker polyTracker;

		const bool newRadialPathBuild = g_navOptions.m_useNewRadialPathBuild;

		{
			const float padAmount = pathContext.m_pathRadius * 1.25f;

			struct NavMeshEntry
			{
				NavMeshHandle m_hMesh;
				U32 m_iStart;
				U32 m_iEnd;
			};

			const NavPoly** apPolys = NDI_NEW const NavPoly*[512];
			static CONST_EXPR size_t kMaxNavMeshEntries = 128;
			NavMeshEntry* pEntries = NDI_NEW NavMeshEntry[kMaxNavMeshEntries];
			U32F numEntries = 0;

			U32F iPrev = 0;
			NavMeshHandle hCurNavMesh = NavMeshHandle();
			bool first = true;

			for (U32F iNode = 0; iNode < numNodes; ++iNode)
			{
				NAV_ASSERT(apDataList[iNode]);
				if (!apDataList[iNode])
					continue;

				if (apDataList[iNode]->m_pathNode.IsNavMeshNode())
				{
					const NavMeshHandle hMesh = apDataList[iNode]->m_pathNode.GetNavMeshHandle();

					if (const NavPoly* pPoly = apDataList[iNode]->m_pathNode.GetNavPolyHandle().ToNavPoly())
					{
						const NavMesh* pMesh = hMesh.ToNavMesh();
						numEdges = RecordBlockingEdgesFromPathPoly(pathContext,
																   pMesh,
																   pPoly,
																   polyTracker,
																   apPolys,
																   pEdgesOut,
																   maxEdgesOut,
																   numEdges,
																   edgeId,
																   numPolysRecorded);


						if (pPoly->IsPreLink() && !newRadialPathBuild)
						{
							const U32F linkId = pPoly->GetLinkId();
							const U32F linkPolyId = pMesh->GetLink(linkId).GetSrcLinkPolyId();

							const NavPoly* pLinkPoly = pMesh->UnsafeGetPolyFast(linkPolyId);

							numEdges = RecordBlockingEdgesFromPathPoly(pathContext,
																	   pMesh,
																	   pLinkPoly,
																	   polyTracker,
																	   apPolys,
																	   pEdgesOut,
																	   maxEdgesOut,
																	   numEdges,
																	   edgeId,
																	   numPolysRecorded);
						}
					}

					if (first)
					{
						hCurNavMesh = hMesh;
						first = false;
					}
					else if (hMesh != hCurNavMesh)
					{
						NAV_ASSERT(numEntries < kMaxNavMeshEntries);

						pEntries[numEntries].m_hMesh = hCurNavMesh;
						pEntries[numEntries].m_iStart = iPrev;
						pEntries[numEntries].m_iEnd = iNode - 1;
						++numEntries;

						hCurNavMesh = hMesh;
						iPrev = iNode;
					}
				}
			}

			if ((iPrev < (numNodes - 1)) && !hCurNavMesh.IsNull())
			{
				pEntries[numEntries].m_hMesh = hCurNavMesh;
				pEntries[numEntries].m_iStart = iPrev;
				pEntries[numEntries].m_iEnd = numNodes - 1;
				++numEntries;
			}

			for (U32F iEntry = 0; iEntry < numEntries; ++iEntry)
			{
				const NavMeshEntry& entry = pEntries[iEntry];
				const NavMesh* pMesh = entry.m_hMesh.ToNavMesh();
				if (!pMesh)
					continue;

				const NavPoly* pStartPoly = apDataList[entry.m_iStart]->m_pathNode.GetNavPolyHandle().ToNavPoly();

				const Point entryStartPs = apDataList[entry.m_iStart]->m_pathNodePosPs;
				const Point entryStartWs = pathContext.m_parentSpace.TransformPoint(entryStartPs);
				const Point entryStartLs = pMesh->WorldToLocal(entryStartWs);

				const Point entryEndPs = apDataList[entry.m_iEnd]->m_pathNodePosPs;
				const Point entryEndWs = pathContext.m_parentSpace.TransformPoint(entryEndPs);
				const Point entryEndLs = pMesh->WorldToLocal(entryEndWs);

				const Segment gatherSegLs = Segment(entryStartLs, entryEndLs);

				numEdges = RecordBlockingEdgesFromMesh(pathContext,
													   pMesh,
													   pStartPoly,
													   gatherSegLs,
													   polyTracker,
													   apPolys,
													   pEdgesOut,
													   maxEdgesOut,
													   numEdges,
													   edgeId,
													   numPolysRecorded);
			}
		}

		return numEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void LinkEdgeLeft(NavEdge* pEdges,
							 NavEdge& e0,
							 NavEdge& e1,
							 bool interiorCorner,
							 float linkAngleDeg,
							 float pathRadius,
							 const Point* pManualPosPs = nullptr)
	{
		const Vector vertNormalPs = AsUnitVectorXz(e0.m_normalPs + e1.m_normalPs, kZero);

		if (e0.m_link0.m_pEdge)
		{
			pEdges[e0.m_link0.m_iEdge].m_link1 = NavEdge::Link();
		}

		if (e1.m_link1.m_pEdge)
		{
			pEdges[e1.m_link1.m_iEdge].m_link0 = NavEdge::Link();
		}

		e0.m_link0.m_pEdge = &e1;
		e0.m_link0.m_iEdge = e1.m_iEdge;
		e0.m_link0.m_vertNormalLs = e0.m_pNavMesh->ParentToLocal(vertNormalPs);
		e0.m_link0.m_interior = interiorCorner;
		e0.m_link0.m_linkAngleDeg = linkAngleDeg;

		e1.m_link1.m_pEdge = &e0;
		e1.m_link1.m_iEdge = e0.m_iEdge;
		e1.m_link1.m_vertNormalLs = e1.m_pNavMesh->ParentToLocal(vertNormalPs);
		e1.m_link1.m_interior = interiorCorner;
		e1.m_link1.m_linkAngleDeg = linkAngleDeg;

		if (interiorCorner)
		{
			const float normDot = DotXz(e0.m_normalPs, e1.m_normalPs);
			const float normAngleRad = SafeAcos(normDot);
			const float pushOutDist = pathRadius / Cos(normAngleRad * 0.5f);

			Point intPosPs = kInvalidPoint;

			if (pManualPosPs)
			{
				intPosPs = pManualPosPs[0];
			}
			else
			{
				intPosPs = e0.m_v0Ps + (vertNormalPs * pushOutDist);
			}

			// intentionally not shrinking for interior edges so that when we do ProbeAndShadowEdges() we're testing
			// the fullest possible range (also why we DO expand for exterior edges)
			//e0.m_projected0Ps = intPosPs;
			//e1.m_projected1Ps = intPosPs;

			NAV_ASSERT(IsReasonable(intPosPs));

			e0.m_link0.m_vertsPs[0] = intPosPs;
			e0.m_link0.m_vertsPs[1] = intPosPs;

			e1.m_link1.m_vertsPs[0] = intPosPs;
			e1.m_link1.m_vertsPs[1] = intPosPs;

			e0.m_link0.m_vertsLs[0] = e0.m_pNavMesh->ParentToLocal(intPosPs);
			e0.m_link0.m_vertsLs[1] = e0.m_link0.m_vertsLs[0];

			e1.m_link1.m_vertsLs[0] = e1.m_pNavMesh->ParentToLocal(intPosPs);
			e1.m_link1.m_vertsLs[1] = e1.m_link1.m_vertsLs[0];
		}
		else
		{
			Point p0Ps = kInvalidPoint;
			Point p1Ps = kInvalidPoint;

			if (pManualPosPs)
			{
				p0Ps = pManualPosPs[0];
				p1Ps = pManualPosPs[1];
			}
			else
			{
				const float intDot0 = DotXz(e0.m_normalPs, vertNormalPs);
				const float intDot1 = DotXz(e1.m_normalPs, vertNormalPs);
				const float intAngleRad = SafeAcos(intDot0);
				const float intAngleRad2 = SafeAcos(intDot1);

				const float l = Tan(intAngleRad * 0.5f) * pathRadius;

				const Point basePosPs = e0.m_v0Ps + (vertNormalPs * pathRadius);
				const Vector offsetPs = RotateY90(vertNormalPs) * l;
				p0Ps = basePosPs + offsetPs;
				p1Ps = basePosPs - offsetPs;
			}

			NAV_ASSERT(IsReasonable(p0Ps));
			NAV_ASSERT(IsReasonable(p1Ps));

			e0.m_projected0Ps = p1Ps;
			e1.m_projected1Ps = p0Ps;

			e0.m_link0.m_vertsPs[0] = p0Ps;
			e0.m_link0.m_vertsPs[1] = p1Ps;
			e1.m_link1.m_vertsPs[0] = p0Ps;
			e1.m_link1.m_vertsPs[1] = p1Ps;

			e0.m_link0.m_vertsLs[0] = e0.m_pNavMesh->ParentToLocal(p0Ps);
			e0.m_link0.m_vertsLs[1] = e0.m_pNavMesh->ParentToLocal(p1Ps);
			e1.m_link1.m_vertsLs[0] = e1.m_pNavMesh->ParentToLocal(p0Ps);
			e1.m_link1.m_vertsLs[1] = e1.m_pNavMesh->ParentToLocal(p1Ps);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void LinkEdgeRight(NavEdge* pEdges,
							  NavEdge& e0,
							  NavEdge& e1,
							  bool interiorCorner,
							  float linkAngleDeg,
							  float pathRadius,
							  const Point* pManualPosPs = nullptr)
	{
		const Vector vertNormalPs = AsUnitVectorXz(e0.m_normalPs + e1.m_normalPs, kZero);

		if (e0.m_link1.m_pEdge)
		{
			pEdges[e0.m_link1.m_iEdge].m_link0 = NavEdge::Link();
		}

		if (e1.m_link0.m_pEdge)
		{
			pEdges[e1.m_link0.m_iEdge].m_link1 = NavEdge::Link();
		}

		e0.m_link1.m_pEdge = &e1;
		e0.m_link1.m_iEdge = e1.m_iEdge;
		e0.m_link1.m_vertNormalLs = e0.m_pNavMesh->ParentToLocal(vertNormalPs);
		e0.m_link1.m_interior = interiorCorner;
		e0.m_link1.m_linkAngleDeg = linkAngleDeg;

		e1.m_link0.m_pEdge = &e0;
		e1.m_link0.m_iEdge = e0.m_iEdge;
		e1.m_link0.m_vertNormalLs = e1.m_pNavMesh->ParentToLocal(vertNormalPs);
		e1.m_link0.m_interior = interiorCorner;
		e1.m_link0.m_linkAngleDeg = linkAngleDeg;

		if (interiorCorner)
		{
			Point intPosPs = kInvalidPoint;

			if (pManualPosPs)
			{
				intPosPs = pManualPosPs[0];
			}
			else
			{
				const float normDot = DotXz(e0.m_normalPs, e1.m_normalPs);
				const float normAngleRad = SafeAcos(normDot);
				const float pushOutDist = pathRadius / Cos(normAngleRad * 0.5f);

				intPosPs = e0.m_v1Ps + (vertNormalPs * pushOutDist);
			}

			// intentionally not shrinking for interior edges so that when we do ProbeAndShadowEdges() we're testing
			// the fullest possible range (also why we DO expand for exterior edges)
			//e0.m_projected1Ps = intPosPs;
			//e1.m_projected0Ps = intPosPs;

			NAV_ASSERT(IsReasonable(intPosPs));

			e0.m_link1.m_vertsPs[0] = intPosPs;
			e0.m_link1.m_vertsPs[1] = intPosPs;
			e1.m_link0.m_vertsPs[0] = intPosPs;
			e1.m_link0.m_vertsPs[1] = intPosPs;

			e0.m_link1.m_vertsLs[0] = e0.m_pNavMesh->ParentToLocal(intPosPs);
			e0.m_link1.m_vertsLs[1] = e0.m_link1.m_vertsLs[0];
			e1.m_link0.m_vertsLs[0] = e1.m_pNavMesh->ParentToLocal(intPosPs);
			e1.m_link0.m_vertsLs[1] = e1.m_link0.m_vertsLs[0];
		}
		else
		{
			Point p0Ps = kInvalidPoint;
			Point p1Ps = kInvalidPoint;

			if (pManualPosPs)
			{
				p0Ps = pManualPosPs[0];
				p1Ps = pManualPosPs[1];
			}
			else
			{
				const float intDot0 = DotXz(e0.m_normalPs, vertNormalPs);
				const float intDot1 = DotXz(e1.m_normalPs, vertNormalPs);
				const float intAngleRad = SafeAcos(intDot0);
				const float intAngleRad2 = SafeAcos(intDot1);

				const float l = Tan(intAngleRad * 0.5f) * pathRadius;

				const Point basePosPs = e0.m_v1Ps + (vertNormalPs * pathRadius);
				const Vector offsetPs = RotateY90(vertNormalPs) * l;
				p0Ps = basePosPs + offsetPs;
				p1Ps = basePosPs - offsetPs;
			}

			NAV_ASSERT(IsReasonable(p0Ps));
			NAV_ASSERT(IsReasonable(p1Ps));

			e0.m_projected1Ps = p0Ps;
			e1.m_projected0Ps = p1Ps;

			e0.m_link1.m_vertsPs[0] = p0Ps;
			e0.m_link1.m_vertsPs[1] = p1Ps;
			e1.m_link0.m_vertsPs[0] = p0Ps;
			e1.m_link0.m_vertsPs[1] = p1Ps;

			e0.m_link1.m_vertsLs[0] = e0.m_pNavMesh->ParentToLocal(p0Ps);
			e0.m_link1.m_vertsLs[1] = e0.m_pNavMesh->ParentToLocal(p1Ps);
			e1.m_link0.m_vertsLs[0] = e1.m_pNavMesh->ParentToLocal(p0Ps);
			e1.m_link0.m_vertsLs[1] = e1.m_pNavMesh->ParentToLocal(p1Ps);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void PreserveDisconnectedLinkLeft(NavEdge* pEdges,
											 NavEdge& e0,
											 NavEdge& e1,
											 NavEdge::Link* preservedLink0,
											 NavEdge::Link* preservedLink1)
	{
		preservedLink0[e0.m_iEdge] = NavEdge::Link();
		preservedLink1[e1.m_iEdge] = NavEdge::Link();

		if (e0.m_link0.m_pEdge)
		{
			if (e0.m_link0.m_interior)
			{
				preservedLink1[e0.m_link0.m_iEdge] = NavEdge::Link();
			}
			else
			{
				preservedLink1[e0.m_link0.m_iEdge] = e0.m_link0.m_pEdge->m_link1;
			}
		}

		if (e1.m_link1.m_pEdge)
		{
			if (e1.m_link1.m_interior)
			{
				preservedLink0[e1.m_link1.m_iEdge] = NavEdge::Link();
			}
			else
			{
				preservedLink0[e1.m_link1.m_iEdge] = e1.m_link1.m_pEdge->m_link0;
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void PreserveDisconnectedLinkRight(NavEdge* pEdges,
											  NavEdge& e0,
											  NavEdge& e1,
											  NavEdge::Link* preservedLink0,
											  NavEdge::Link* preservedLink1)
	{
		preservedLink0[e1.m_iEdge] = NavEdge::Link();
		preservedLink1[e0.m_iEdge] = NavEdge::Link();

		if (e0.m_link1.m_pEdge)
		{
			if (e0.m_link1.m_interior)
			{
				preservedLink0[e0.m_link1.m_iEdge] = NavEdge::Link();
			}
			else
			{
				preservedLink0[e0.m_link1.m_iEdge] = e0.m_link1.m_pEdge->m_link0;
			}
		}

		if (e1.m_link0.m_pEdge)
		{
			if (e1.m_link0.m_interior)
			{
				preservedLink1[e1.m_link0.m_iEdge] = NavEdge::Link();
			}
			else
			{
				preservedLink1[e1.m_link0.m_iEdge] = e1.m_link0.m_pEdge->m_link1;
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ValidateEdgeNetwork(const NavEdge* pEdges, U32F numEdges)
	{
		STRIP_IN_FINAL_BUILD;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const NavEdge& startingEdge = pEdges[iEdge];

			NavItemBitArray visitedLeft(false);
			NavItemBitArray visitedRight(false);

			for (const NavEdge* pLeftEdge = startingEdge.m_link0.m_pEdge;
				 pLeftEdge && pLeftEdge != &startingEdge;
				 pLeftEdge = pLeftEdge->m_link0.m_pEdge)
			{
				if (visitedLeft.IsBitSet(pLeftEdge->m_iEdge))
				{
#if 1 // #ifdef JBELLOMY
					DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
					g_hackPrintParams = true;
					g_ndConfig.m_pDMenuMgr->SetProgPause(true);
					g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
					g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
					g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
					MsgConErr("RADIAL PATH BUILD FAIL (Left Link Validation Loop)\n");

					return;
#else
					break;
#endif
				}
				else
				{
					visitedLeft.SetBit(pLeftEdge->m_iEdge);
				}
			}

			for (const NavEdge* pRightEdge = startingEdge.m_link1.m_pEdge;
				 pRightEdge && pRightEdge != &startingEdge;
				 pRightEdge = pRightEdge->m_link1.m_pEdge)
			{
				if (visitedRight.IsBitSet(pRightEdge->m_iEdge))
				{
#if 1 // #ifdef JBELLOMY
					DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
					g_hackPrintParams = true;
					g_ndConfig.m_pDMenuMgr->SetProgPause(true);
					g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
					g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
					g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
					MsgConErr("RADIAL PATH BUILD FAIL (Right Link Validation Loop)\n");


					return;
#else
					break;
#endif
				}
				else
				{
					visitedRight.SetBit(pRightEdge->m_iEdge);
				}
			}

			if (startingEdge.m_link0.m_pEdge && (startingEdge.m_link0.m_pEdge->m_link1.m_iEdge != iEdge))
			{
#if 1 // #ifdef JBELLOMY
				DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
				g_hackPrintParams = true;
				g_ndConfig.m_pDMenuMgr->SetProgPause(true);
				g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
				MsgConErr("RADIAL PATH BUILD FAIL (Left Link Rev)\n");

				return;
#else
				break;
#endif
			}

			if (startingEdge.m_link1.m_pEdge && (startingEdge.m_link1.m_pEdge->m_link0.m_iEdge != iEdge))
			{
#if 1 // #ifdef JBELLOMY
				DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
				g_hackPrintParams = true;
				g_ndConfig.m_pDMenuMgr->SetProgPause(true);
				g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
				MsgConErr("RADIAL PATH BUILD FAIL (Right Link Rev)\n");

				return;
#else
				break;
#endif
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsPointClearOfEdgesPs(Point_arg posPs,
									  float pathRadius,
									  U64 iSourceEdge,
									  const NavEdge* pEdges,
									  U32F numEdges,
									  const NavItemRegistrationTable& edgeTable,
									  I32F ignoreEdge0 = -1,
									  I32F ignoreEdge1 = -1)
	{
		const NavEdge& startingEdge = pEdges[iSourceEdge];
		const NavItemBitArray& nearbyEdges = edgeTable.At(startingEdge.m_regTableId);

		for (U64 iNearby : nearbyEdges)
		{
			if (iNearby == iSourceEdge ||
				iNearby == ignoreEdge0 || iNearby == ignoreEdge1 ||
				iNearby == startingEdge.m_link0.m_iEdge || iNearby == startingEdge.m_link1.m_iEdge)
			{
				continue;
			}

			const NavEdge& otherEdge = pEdges[iNearby];

			const float d = DistPointSegmentXz(posPs, otherEdge.m_v0Ps, otherEdge.m_v1Ps);
			if (d <= pathRadius)
			{
				return false;
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsSegmentClearOfEdgesPs(const Segment segPs,
										float pathRadius,
										U64 iSourceEdge,
										const NavEdge* pEdges,
										U32F numEdges,
										const NavItemRegistrationTable& edgeTable,
										I32F ignoreEdge0 = -1,
										I32F ignoreEdge1 = -1)
	{
		const NavEdge& startingEdge = pEdges[iSourceEdge];
		const NavItemBitArray& nearbyEdges = edgeTable.At(startingEdge.m_regTableId);

		for (U64 iNearby : nearbyEdges)
		{
			if (iNearby == iSourceEdge ||
				iNearby == ignoreEdge0 || iNearby == ignoreEdge1 ||
				iNearby == startingEdge.m_link0.m_iEdge || iNearby == startingEdge.m_link1.m_iEdge)
			{
				continue;
			}

			const NavEdge& otherEdge = pEdges[iNearby];

			Scalar t0, t1;
			const Scalar d = DistSegmentSegmentXz(segPs, Segment(otherEdge.m_v0Ps, otherEdge.m_v1Ps), t0, t1);
			if (d <= pathRadius)
			{
				return false;
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool ProbeLeftLink(const Segment& startingSegPs,
							  const Vector& edgeDir0XzPs,
							  const NavEdge::Link& link,
							  bool& isRight,
							  Scalar& t0,
							  Scalar& t1)
	{
		if (!link.m_pEdge || link.m_interior)
		{
			return false;
		}

		const Point link0Ps = link.m_vertsPs[0];
		const Point link1Ps = link.m_vertsPs[1];

		const Segment linkSegPs = Segment(link0Ps, link1Ps);

		const Vector linkEdgeNormPs = RotateY90(link1Ps - link0Ps);
		isRight = DotXz(linkEdgeNormPs, edgeDir0XzPs) < 0.0f;

		// test extra link segment as well
		return isRight && IntersectSegmentSegmentXz(startingSegPs, linkSegPs, t0, t1);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool ProbeRightLink(const Segment& startingSegPs,
							   const Vector& edgeDir0XzPs,
							   const NavEdge::Link& link,
							   bool& isRight,
							   Scalar& t0,
							   Scalar& t1)
	{
		if (!link.m_pEdge || link.m_interior)
		{
			return false;
		}

		const Point link0Ps = link.m_vertsPs[0];
		const Point link1Ps = link.m_vertsPs[1];

		const Segment linkSegPs = Segment(link0Ps, link1Ps);

		const Vector linkEdgeNormPs = RotateY90(link1Ps - link0Ps);
		isRight = DotXz(linkEdgeNormPs, edgeDir0XzPs) < 0.0f;

		// test extra link segment as well
		return !isRight && IntersectSegmentSegmentXz(startingSegPs, linkSegPs, t0, t1);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	class SafeAllocator
	{
	public:
		SafeAllocator()
		{
			m_pTopAllocator = Memory::TopAllocator();
			m_pFallbackAllocator = Memory::GetAllocator(kAllocSingleFrame);
		}

		template<class T>
		T* Allocate(U32 count, const char *sourceFile, U32F sourceLine, const char *srcFunc)
		{
			Alignment align(alignof(NavEdge::Link));
			U32 size = sizeof(T) * count;
			Memory::Allocator* pAlloc = m_pTopAllocator->CanAllocate(size, align) ? m_pTopAllocator : m_pFallbackAllocator;
			void* pPtr = pAlloc->Allocate(size, align, sourceFile, sourceLine, srcFunc);
			return new(pPtr) T[count];
		}

		template<class T>
		T* Allocate(const char *sourceFile, U32F sourceLine, const char *srcFunc)
		{
			Alignment align(alignof(NavEdge::Link));
			U32 size = sizeof(T);
			Memory::Allocator* pAlloc = m_pTopAllocator->CanAllocate(size, align) ? m_pTopAllocator : m_pFallbackAllocator;
			void* pPtr = pAlloc->Allocate(size, align, sourceFile, sourceLine, srcFunc);
			return new(pPtr) T;
		}

	private:
		Memory::Allocator* m_pTopAllocator;
		Memory::Allocator* m_pFallbackAllocator;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct ProbeAndShadowEdgesContext
	{
		ProbeAndShadowEdgesContext(const PathFindContext& pathContext, U32F numEdges, SafeAllocator& allocator)
			: m_pathRadius(pathContext.m_pathRadius)
			, m_pathDiameter(2.0f * pathContext.m_pathRadius)
			, m_edgesNotLinkedLeft(false)
			, m_edgesNotLinkedRight(false)
			, m_allocator(allocator)
		{
			m_edgesNotLinkedLeft.SetBitRange(0, numEdges - 1);
			m_edgesNotLinkedRight.SetBitRange(0, numEdges - 1);

			m_preservedLink0 = allocator.Allocate<NavEdge::Link>(2 * numEdges, FILE_LINE_FUNC);
			m_preservedLink1 = m_preservedLink0 + numEdges;
		}

		float m_pathRadius;
		float m_pathDiameter;

		NavItemBitArray m_edgesNotLinkedLeft;
		NavItemBitArray m_edgesNotLinkedRight;

		NavEdge::Link* m_preservedLink0;
		NavEdge::Link* m_preservedLink1;

		SafeAllocator& m_allocator;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ProbeVisitEdge(ProbeAndShadowEdgesContext& ctx,
							   NavEdge* pEdges,
							   U32F numEdges,
							   U32F iEdge,
							   const NavItemRegistrationTable& edgeTable)
	{
		NavEdge& startingEdge = pEdges[iEdge];
		const Segment startingSegPs = Segment(startingEdge.m_projected0Ps, startingEdge.m_projected1Ps);
		const Vector edgeDir0XzPs = startingEdge.m_edgeDirXzPs;

		const NavItemBitArray& nearbyEdges = edgeTable.At(startingEdge.m_regTableId);

		Scalar t0, t1;

		Scalar tLeft = 0.0f;
		I32F iNewLeftEdge = -1;
		NavEdge::Link* pLeftHitLinkEdge = nullptr;

		Scalar tRight = 1.0f;
		I32F iNewRightEdge = -1;
		NavEdge::Link* pRightHitLinkEdge = nullptr;

		for (U64 iNearby : nearbyEdges)
		{
			if (iNearby == iEdge)
				continue;

			bool isRight = false;
			bool hit = false;

			NavEdge::Link* pHitLink = nullptr;

			NavEdge& nearbyEdge = pEdges[iNearby];
			const Segment nearbySegPs = Segment(nearbyEdge.m_projected0Ps, nearbyEdge.m_projected1Ps);

			const float distToEdge = DistSegmentSegmentXz(Segment(startingEdge.m_v0Ps, startingEdge.m_v1Ps),
														  Segment(nearbyEdge.m_v0Ps, nearbyEdge.m_v1Ps),
														  t0,
														  t1);
			if (distToEdge >= ctx.m_pathDiameter)
			{
				continue;
			}

			if (IntersectSegmentSegmentXz(startingSegPs, nearbySegPs, t0, t1))
			{
				const float dotP = DotXz(nearbyEdge.m_normalPs, edgeDir0XzPs);
				isRight = dotP < 0.0f;
				hit = true;

				// ignore touching intersections for edges that are joined together
				if (Dist(startingEdge.m_v1Ps, nearbyEdge.m_v0Ps) < kLinkDist)
				{
					if (t0 + NDI_FLT_EPSILON >= 1.0f)
					{
						hit = false;
					}
				}
				else if (Dist(startingEdge.m_v0Ps, nearbyEdge.m_v1Ps) < kLinkDist)
				{
					if (t0 - NDI_FLT_EPSILON <= 0.0f)
					{
						hit = false;
					}
				}
			}
			else
			{
				if (ProbeLeftLink(startingSegPs, edgeDir0XzPs, nearbyEdge.m_link0, isRight, t0, t1))
				{
					hit = true;
					pHitLink = &nearbyEdge.m_link0;
				}

				if (!hit && ProbeRightLink(startingSegPs, edgeDir0XzPs, nearbyEdge.m_link1, isRight, t0, t1))
				{
					hit = true;
					pHitLink = &nearbyEdge.m_link1;
				}

				if (!hit && ProbeLeftLink(startingSegPs, edgeDir0XzPs, ctx.m_preservedLink0[iNearby], isRight, t0, t1))
				{
					hit = true;
					pHitLink = &ctx.m_preservedLink0[iNearby];
				}

				if (!hit && ProbeRightLink(startingSegPs, edgeDir0XzPs, ctx.m_preservedLink1[iNearby], isRight, t0, t1))
				{
					hit = true;
					pHitLink = &ctx.m_preservedLink1[iNearby];
				}
			}

			if (hit)
			{
				// (t0 >= tLeft) and (t0 <= tRight) is an ugly ass hack to work around the real issue
				// which is we might need to split one edge into multiple valid edges
				// a la circle depen :|

				if (isRight)
				{
					if ((t0 < tRight || iNewRightEdge < 0) && (t0 >= tLeft))
					{
						tRight = t0;
						iNewRightEdge = iNearby;
						pRightHitLinkEdge = pHitLink;
					}
				}
				else
				{
					if ((t0 > tLeft || (iNewLeftEdge < 0)) && (t0 <= tRight))
					{
						tLeft = t0;
						iNewLeftEdge = iNearby;
						pLeftHitLinkEdge = pHitLink;
					}
				}
			}
		}

		// Save of the right edge vert here since the right hit link might be modified when connecting the new left edge.
		Point linkedRightEdgeVert = pRightHitLinkEdge ? pRightHitLinkEdge->m_vertsPs[1] : kInvalidPoint;

		if (iNewLeftEdge >= 0 && startingEdge.m_link0.m_iEdge != iNewLeftEdge)
		{
			NavEdge& newLeftEdge = pEdges[iNewLeftEdge];

			const Vector edgeDir1XzPs = newLeftEdge.m_pNavMesh->LocalToParent(newLeftEdge.m_edgeDirXzLs);
			const float cy = CrossY(edgeDir1XzPs, edgeDir0XzPs);
			const float linkAngleDeg = RADIANS_TO_DEGREES(SafeAsin(cy));

			const Point intPosPs = Lerp(startingSegPs.a, startingSegPs.b, tLeft);

			Point newVertsPs[2] = { intPosPs, intPosPs };

			if (pLeftHitLinkEdge)
			{
				newVertsPs[0] = pLeftHitLinkEdge->m_vertsPs[0];
			}

			PreserveDisconnectedLinkLeft(pEdges, startingEdge, newLeftEdge, ctx.m_preservedLink0, ctx.m_preservedLink1);
			LinkEdgeLeft(pEdges, startingEdge, newLeftEdge, !pLeftHitLinkEdge, linkAngleDeg, ctx.m_pathRadius, newVertsPs);

			newLeftEdge.m_projected1Ps = newVertsPs[0];
			startingEdge.m_projected0Ps = newVertsPs[1];

			ctx.m_edgesNotLinkedLeft.ClearBit(iEdge);
			ctx.m_edgesNotLinkedRight.ClearBit(iNewLeftEdge);
		}

		if (iNewRightEdge >= 0 && startingEdge.m_link1.m_iEdge != iNewRightEdge)
		{
			NavEdge& newRightEdge = pEdges[iNewRightEdge];

			const Vector edgeDir1XzPs = newRightEdge.m_pNavMesh->LocalToParent(newRightEdge.m_edgeDirXzLs);
			const float cy = CrossY(edgeDir0XzPs, edgeDir1XzPs);
			const bool interiorCorner = cy > 0.0f;
			const float linkAngleDeg = RADIANS_TO_DEGREES(SafeAsin(cy));

			const Point intPosPs = Lerp(startingSegPs.a, startingSegPs.b, tRight);

			Point newVertsPs[2] = { intPosPs, intPosPs };

			if (pRightHitLinkEdge)
			{
				newVertsPs[1] = linkedRightEdgeVert;
			}

			PreserveDisconnectedLinkRight(pEdges, startingEdge, newRightEdge, ctx.m_preservedLink0, ctx.m_preservedLink1);
			LinkEdgeRight(pEdges, startingEdge, newRightEdge, !pRightHitLinkEdge, linkAngleDeg, ctx.m_pathRadius, newVertsPs);

			startingEdge.m_projected1Ps = newVertsPs[0];
			newRightEdge.m_projected0Ps = newVertsPs[1];

			ctx.m_edgesNotLinkedRight.ClearBit(iEdge);
			ctx.m_edgesNotLinkedLeft.ClearBit(iNewRightEdge);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ProbeVisitEdges(ProbeAndShadowEdgesContext& ctx,
								NavEdge* pEdges,
								U32F numEdges,
								const I64* pOriginalRightLink,
								const NavItemBitArray& toVisit,
								NavItemBitArray& visited,
								const NavItemRegistrationTable& edgeTable)
	{
		for (U64 iVisit : toVisit)
		{
			for (I64 iEdge = iVisit; iEdge >= 0; iEdge = pOriginalRightLink[iEdge])
			{
				if (visited.IsBitSet(iEdge))
				{
					break;
				}

				visited.SetBit(iEdge);

				ProbeVisitEdge(ctx,
							   pEdges,
							   numEdges,
							   iEdge,
							   edgeTable);
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsEdgeShadowed(ProbeAndShadowEdgesContext& ctx,
							   U64 iEdge,
							   NavEdge* pEdges,
							   U32F numEdges,
							   const NavItemRegistrationTable& edgeTable)
	{
		const NavEdge& edge = pEdges[iEdge];

		bool isPointClear = IsSegmentClearOfEdgesPs(Segment(edge.m_projected0Ps, edge.m_projected1Ps),
													ctx.m_pathRadius,
													iEdge,
													pEdges,
													numEdges,
													edgeTable);
		bool edgeFlipped = false;

		if (isPointClear && edge.m_link0.m_pEdge && edge.m_link1.m_pEdge && edge.m_link0.m_interior && edge.m_link1.m_interior)
		{
			const Segment edgeSeg(edge.m_v0Ps, edge.m_v1Ps);
			const Segment projSeg(edge.m_link0.m_pEdge->m_projected1Ps, edge.m_link1.m_pEdge->m_projected0Ps);

			edgeFlipped = DotXz(edgeSeg.GetVec(), projSeg.GetVec()) < -NDI_FLT_EPSILON;
		}

		return !isPointClear || edgeFlipped;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ShadowEdgeChains(ProbeAndShadowEdgesContext& ctx,
								 bool left,
								 const NavItemBitArray& disconnected,
								 NavItemBitArray& visited,
								 NavItemBitArray& shadowed,
								 NavEdge* pEdges,
								 U32F numEdges,
								 const NavItemRegistrationTable& edgeTable)
	{
		NavItemBitArray visitedSub;

		for (U64 iDisconnected : disconnected)
		{
			if (visited.IsBitSet(iDisconnected))
			{
				continue;
			}

			bool allShadowed = true;
			visitedSub.ClearAllBits();

			for (I64 iEdge = iDisconnected; iEdge >= 0; iEdge = left ? pEdges[iEdge].m_link1.m_iEdge : pEdges[iEdge].m_link0.m_iEdge)
			{
				if (visitedSub.IsBitSet(iEdge))
					break;

				visited.SetBit(iEdge);
				visitedSub.SetBit(iEdge);

				if (!IsEdgeShadowed(ctx, iEdge, pEdges, numEdges, edgeTable))
				{
					allShadowed = false;
					break;
				}
			}

			if (allShadowed)
			{
				visitedSub.ClearAllBits();

				for (I64 iEdge = iDisconnected; iEdge >= 0; iEdge = left ? pEdges[iEdge].m_link1.m_iEdge : pEdges[iEdge].m_link0.m_iEdge)
				{
					if (visitedSub.IsBitSet(iEdge))
						break;

					NavEdge& disEdge = pEdges[iEdge];
					disEdge.m_shadowed = true;
					shadowed.SetBit(iEdge);
					visitedSub.SetBit(iEdge);
				}
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ProbeEdges(ProbeAndShadowEdgesContext& ctx,
						   NavEdge* pEdges,
						   U32F numEdges,
						   const NavItemRegistrationTable& edgeTable)
	{
		// Connect edges
		{
			NavItemBitArray visited(false);
			I64* pOriginalRightLink = ctx.m_allocator.Allocate<I64>(numEdges, FILE_LINE_FUNC);

			// First visit all chains
			{
				NavItemBitArray toVisit(false);

				for (U32F iEdge = 0; iEdge < numEdges; iEdge++)
				{
					pOriginalRightLink[iEdge] = pEdges[iEdge].m_link1.m_iEdge;

					if (!pEdges[iEdge].m_link0.m_pEdge)
					{
						toVisit.SetBit(iEdge);
					}
				}

				ProbeVisitEdges(ctx,
								pEdges,
								numEdges,
								pOriginalRightLink,
								toVisit,
								visited,
								edgeTable);
			}

			// Visit all loops
			{
				NavItemBitArray toVisit(false);
				toVisit.SetBitRange(0, numEdges - 1);

				NavItemBitArray::BitwiseAndComp(&toVisit, toVisit, visited);

				ProbeVisitEdges(ctx,
								pEdges,
								numEdges,
								pOriginalRightLink,
								toVisit,
								visited,
								edgeTable);
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ShadowEdges(ProbeAndShadowEdgesContext& ctx,
							NavEdge* pEdges,
							U32F numEdges,
							const NavItemRegistrationTable& edgeTable)
	{
		NavItemBitArray shadowedEdges(false);
		NavItemBitArray disconnectedLeft(false);
		NavItemBitArray disconnectedRight(false);

		for (U32 iEdge = 0; iEdge < numEdges; iEdge++)
		{
			const NavEdge& edge = pEdges[iEdge];

			if (!edge.m_link0.m_pEdge)
			{
				disconnectedLeft.SetBit(iEdge);
			}

			if (!edge.m_link1.m_pEdge)
			{
				disconnectedRight.SetBit(iEdge);
			}

			if (edge.m_shadowed)
			{
				shadowedEdges.SetBit(iEdge);
			}
		}

		// Shadow edge chains
		{
			NavItemBitArray visited(false);

			ShadowEdgeChains(ctx,
							 true,
							 disconnectedLeft,
							 visited,
							 shadowedEdges,
							 pEdges,
							 numEdges,
							 edgeTable);

			ShadowEdgeChains(ctx,
							 false,
							 disconnectedRight,
							 visited,
							 shadowedEdges,
							 pEdges,
							 numEdges,
							 edgeTable);
		}

		// any edges we didn't connect by way of intersections, use the analytically generated
		// link positions to set the correct projected position range
		NavItemBitArray::BitwiseAndComp(&ctx.m_edgesNotLinkedLeft, ctx.m_edgesNotLinkedLeft, shadowedEdges);
		NavItemBitArray::BitwiseAndComp(&ctx.m_edgesNotLinkedRight, ctx.m_edgesNotLinkedRight, shadowedEdges);

		for (U64 iLinkLeft : ctx.m_edgesNotLinkedLeft)
		{
			NavEdge& edge = pEdges[iLinkLeft];

			if (edge.m_link0.m_pEdge)
			{
				edge.m_projected0Ps = edge.m_link0.m_vertsPs[1];
			}
		}

		for (U64 iLinkRight : ctx.m_edgesNotLinkedRight)
		{
			NavEdge& edge = pEdges[iLinkRight];

			if (edge.m_link1.m_pEdge)
			{
				edge.m_projected1Ps = edge.m_link1.m_vertsPs[0];
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	// A bit is set in the returned value if the point is clear.
	static U8 WhichPointIsClearOfEdgesPs(const ProbeAndShadowEdgesContext& ctx,
										 const Point& p1, const Point& p2,
										 U32 iSourceEdge,
										 const NavEdge* pEdges,
										 U32F numEdges,
										 const NavItemRegistrationTable& edgeTable)
	{
		U8 result = 0;

		if (IsPointClearOfEdgesPs(p1, ctx.m_pathRadius, iSourceEdge, pEdges, numEdges, edgeTable))
		{
			SetBit(result, 0);
		}

		if (IsPointClearOfEdgesPs(p2, ctx.m_pathRadius, iSourceEdge, pEdges, numEdges, edgeTable))
		{
			SetBit(result, 1);
		}

		return result;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U8 FindClosestToNearbyEdge(const Point& p1, const Point& p2,
									  const NavEdge& edge,
									  const NavEdge* pEdges, U32F numEdges,
									  const NavItemRegistrationTable& edgeTable)
	{
		F32 closestT1 = kLargeFloat;
		F32 closestT2 = kLargeFloat;

		NavItemBitArray nearby = edgeTable.At(edge.m_regTableId);
		nearby.ClearBit(edge.m_iEdge);

		if (edge.m_link0.m_pEdge)
		{
			nearby.ClearBit(edge.m_link0.m_iEdge);
		}

		if (edge.m_link1.m_pEdge)
		{
			nearby.ClearBit(edge.m_link1.m_iEdge);
		}

		for (U64 iNearby : nearby)
		{
			const NavEdge& e1 = pEdges[iNearby];
			const Segment s1(e1.m_projected0Ps, e1.m_projected1Ps);

			F32 dist1 = DistPointSegmentXz(p1, s1);
			F32 dist2 = DistPointSegmentXz(p2, s1);

			closestT1 = Min(dist1, closestT1);
			closestT2 = Min(dist2, closestT2);
		}

		return closestT2 < closestT1 ? 2 : 1;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsBetweenArcXz(Vector_arg arcBegin, Vector_arg arcEnd, Vector_arg test)
	{
		F32 sgn = CrossY(arcBegin, arcEnd);

		F32 sb = CrossY(arcBegin, test);
		F32 se = CrossY(arcEnd, test);

		return sgn * sb > 0.0f && -sgn * se > 0.0f;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IntersectCircleCircleXz(Point_arg c0, Point_arg c1, F32 r, Point& i0, Point& i1)
	{
		const F32 kTolerance = 0.00001f;

		Vector d = c1 - c0;
		F32 dist = LengthXz(d);
		if (dist >= 2.0f * r || dist < kTolerance)
		{
			// The circles are either too far away (and there is no more then 1 intersection) or they are on top of each other
			// (in which case there are infinitely many intersections). In both cases we can ignore the intersections.
			return false;
		}

		F32 a = (dist * dist) / (2.0f * dist);
		F32 h = Sqrt(r * r - a * a);
		Point p = c0 + (a / dist) * d;

		i0 = Point(p.X() + (h * d.Z()) / dist, c0.Y(), p.Z() - (h * d.X()) / dist);
		i1 = Point(p.X() - (h * d.Z()) / dist, c0.Y(), p.Z() + (h * d.X()) / dist);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IntersectLinkArcs(NavEdge& e0, NavEdge& e1, float r)
	{
		const Point c0 = e0.m_v0Ls;
		const Point c1 = e1.m_v0Ls;

		Point i0, i1;
		if (!IntersectCircleCircleXz(c0, c1, r, i0, i1))
		{
			return false;
		}

		const Vector a0l = e0.m_link0.m_vertsLs[0] - c0;
		const Vector a0r = e0.m_link0.m_vertsLs[1] - c0;

		const Vector a1l = e1.m_link0.m_vertsLs[0] - c1;
		const Vector a1r = e1.m_link0.m_vertsLs[1] - c1;

		return IsBetweenArcXz(a0l, a0r, i0 - c0) || IsBetweenArcXz(a0l, a0r, i1 - c0) ||
			   IsBetweenArcXz(a1l, a1r, i0 - c1) || IsBetweenArcXz(a1l, a1r, i1 - c1);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IntersectLinks(ProbeAndShadowEdgesContext& ctx,
							   NavEdge& e0, NavEdge& e1,
							   NavEdge* pEdges, U32F numEdges,
							   const NavItemRegistrationTable& edgeTable)
	{
		Scalar t0, t1;

		Segment s0(e0.m_link0.m_vertsLs[0], e0.m_link0.m_vertsLs[1]);
		Segment s1(e1.m_link0.m_vertsLs[0], e1.m_link0.m_vertsLs[1]);

		if (!IntersectSegmentSegmentXz(s0, s1, t0, t1))
		{
			return false;
		}

		if (!IntersectLinkArcs(e0, e1, ctx.m_pathRadius))
		{
			return false;
		}

		U8 clear0 = WhichPointIsClearOfEdgesPs(ctx, e0.m_link0.m_vertsPs[0], e0.m_link0.m_vertsPs[1],
												e0.m_iEdge, pEdges, numEdges, edgeTable);
		U8 clear1 = WhichPointIsClearOfEdgesPs(ctx, e1.m_link0.m_vertsPs[0], e1.m_link0.m_vertsPs[1],
												e1.m_iEdge, pEdges, numEdges, edgeTable);

		if (clear0 == 3)
		{
			U8 closest = FindClosestToNearbyEdge(e0.m_link0.m_vertsPs[0], e0.m_link0.m_vertsPs[1],
													e0, pEdges, numEdges, edgeTable);

			clear0 = closest == 1 ? 2 : 1;
		}

		if (clear1 == 3)
		{
			U8 closest = FindClosestToNearbyEdge(e1.m_link0.m_vertsPs[0], e1.m_link0.m_vertsPs[1],
													e1, pEdges, numEdges, edgeTable);

			clear1 = closest == 1 ? 2 : 1;
		}

		if (clear0 == 0 || clear1 == 0 || clear0 == 3 || clear1 == 3)
		{
			return false;
		}

		// Determine which edge and link should be used
		U8 iLink0 = 0;
		U8 iLink1 = 0;
		NavEdge* edge0 = &e0;
		NavEdge* edge1 = &e1;

		if (clear0 == 1)
		{
			edge0 = &pEdges[e0.m_link0.m_iEdge];
			iLink0 = 1;

			e0.m_link0 = NavEdge::Link();
		}
		else
		{
			pEdges[e0.m_link0.m_iEdge].m_link1 = NavEdge::Link();
		}

		if (clear1 == 1)
		{
			edge1 = &pEdges[e1.m_link0.m_iEdge];
			iLink1 = 1;

			e1.m_link0 = NavEdge::Link();
		}
		else
		{
			pEdges[e1.m_link0.m_iEdge].m_link1 = NavEdge::Link();
		}

		// Modify the links
		Point intersectLs = Lerp(s0, t0);
		Point intersectPs = e0.m_pNavMesh->LocalToParent(intersectLs);

		if (iLink0 == 0)
		{
			if (edge0->m_link0.m_pEdge)
			{
				edge0->m_link0.m_iEdge = edge1->m_iEdge;
				edge0->m_link0.m_pEdge = edge1;
				edge0->m_link0.m_interior = false;
				edge0->m_link0.m_linkIntersection = iLink1;
				edge0->m_link0.m_vertsLs[0] = intersectLs;
				edge0->m_link0.m_vertsPs[0] = intersectPs;

				ctx.m_edgesNotLinkedLeft.ClearBit(edge0->m_iEdge);
			}
		}
		else
		{
			if (edge0->m_link1.m_pEdge)
			{
				edge0->m_link1.m_iEdge = edge1->m_iEdge;
				edge0->m_link1.m_pEdge = edge1;
				edge0->m_link1.m_interior = false;
				edge0->m_link1.m_linkIntersection = iLink1;
				edge0->m_link1.m_vertsLs[1] = intersectLs;
				edge0->m_link1.m_vertsPs[1] = intersectPs;

				ctx.m_edgesNotLinkedRight.ClearBit(edge0->m_iEdge);
			}
		}

		if (iLink1 == 0)
		{
			if (edge1->m_link0.m_pEdge)
			{
				edge1->m_link0.m_iEdge = edge0->m_iEdge;
				edge1->m_link0.m_pEdge = edge0;
				edge1->m_link0.m_interior = false;
				edge1->m_link0.m_linkIntersection = iLink0;
				edge1->m_link0.m_vertsLs[0] = intersectLs;
				edge1->m_link0.m_vertsPs[0] = intersectPs;

				ctx.m_edgesNotLinkedLeft.ClearBit(edge1->m_iEdge);
			}
		}
		else
		{
			if (edge1->m_link1.m_pEdge)
			{
				edge1->m_link1.m_iEdge = edge0->m_iEdge;
				edge1->m_link1.m_pEdge = edge0;
				edge1->m_link1.m_interior = false;
				edge1->m_link1.m_linkIntersection = iLink0;
				edge1->m_link1.m_vertsLs[1] = intersectLs;
				edge1->m_link1.m_vertsPs[1] = intersectPs;

				ctx.m_edgesNotLinkedRight.ClearBit(edge1->m_iEdge);
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IntersectRemovedLinks(const ProbeAndShadowEdgesContext& ctx,
									  NavEdge& e0, NavEdge& e1,
									  const NavEdge::Link& link0,
									  const NavEdge::Link& link1,
									  I8 iLink0, I8 iLink1)
	{
		Segment s0(link0.m_vertsLs[0], link0.m_vertsLs[1]);
		Segment s1(link1.m_vertsLs[0], link1.m_vertsLs[1]);

		Scalar t0, t1;
		if (!IntersectSegmentSegmentXz(s0, s1, t0, t1))
		{
			return false;
		}

		Point posLs = Lerp(s0, t0);
		Point posPs = e0.m_pNavMesh->LocalToParent(posLs);

		{
			U32F index = iLink0 ? 1 : 0;
			NavEdge::Link& e0Link = iLink0 ? e0.m_link1 : e0.m_link0;

			e0Link = link0;
			e0Link.m_pEdge = &e1;
			e0Link.m_iEdge = e1.m_iEdge;
			e0Link.m_linkIntersection = iLink1;
			e0Link.m_interior = false;
			e0Link.m_vertsLs[index] = posLs;
			e0Link.m_vertsPs[index] = posPs;
		}

		{
			U32F index = iLink1 ? 1 : 0;
			NavEdge::Link& e1Link = iLink1 ? e1.m_link1 : e1.m_link0;

			e1Link = link1;
			e1Link.m_pEdge = &e0;
			e1Link.m_iEdge = e0.m_iEdge;
			e1Link.m_linkIntersection = iLink0;
			e1Link.m_interior = false;
			e1Link.m_vertsLs[index] = posLs;
			e1Link.m_vertsPs[index] = posPs;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void IntersectNearbyRemovedLinks(ProbeAndShadowEdgesContext& ctx,
											const NavItemBitArray& nearby,
											NavItemBitArray& hasRemovedLink0,
											NavItemBitArray& hasRemovedLink1,
											U32F iEdge, I8 iLink,
											NavEdge* pEdges, U32F numEdges,
											const NavItemRegistrationTable& edgeTable)
	{
		NavEdge& e0 = pEdges[iEdge];

		const NavEdge::Link& link = iLink ? ctx.m_preservedLink1[iEdge] : ctx.m_preservedLink0[iEdge];
		NavItemBitArray& hasRemovedLink = iLink ? hasRemovedLink1 : hasRemovedLink0;

		for (U64 iNearby : nearby)
		{
			NavEdge& e1 = pEdges[iNearby];

			if (e0.m_pNavMesh != e1.m_pNavMesh)
			{
				continue;
			}

			if (hasRemovedLink0.IsBitSet(iNearby))
			{
				if (IntersectRemovedLinks(ctx, e0, e1, link, ctx.m_preservedLink0[iNearby], iLink, 0))
				{
					hasRemovedLink0.ClearBit(iNearby);
					hasRemovedLink.ClearBit(iEdge);
					break;
				}
			}
			else if (!e1.m_shadowed && !e1.m_link0.m_interior && e1.m_link0.m_pEdge && !e1.m_link1.m_pEdge &&
					 e1.m_link0.m_linkIntersection == -1 && e1.m_link0.m_iEdge != iEdge)
			{
				e0.m_link0 = ctx.m_preservedLink0[iEdge];

				if (IntersectLinks(ctx, e0, e1, pEdges, numEdges, edgeTable))
				{
					hasRemovedLink0.ClearBit(iNearby);
					hasRemovedLink.ClearBit(iEdge);
					break;
				}

				e0.m_link0 = NavEdge::Link();
			}

			if (hasRemovedLink1.IsBitSet(iNearby))
			{
				if (IntersectRemovedLinks(ctx, e0, e1, link, ctx.m_preservedLink1[iNearby], iLink, 1))
				{
					hasRemovedLink1.ClearBit(iNearby);
					hasRemovedLink.ClearBit(iEdge);
					break;
				}
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ResolveLinkIntersections(ProbeAndShadowEdgesContext& ctx,
										 NavEdge* pEdges,
										 U32F numEdges,
										 const NavItemRegistrationTable& edgeTable)
	{
		NavItemBitArray hasRemovedLink0(false);
		NavItemBitArray hasRemovedLink1(false);

		for (U32F iEdge = 0; iEdge < numEdges; iEdge++)
		{
			NavEdge& e0 = pEdges[iEdge];

			if (!e0.m_link0.m_pEdge && ctx.m_preservedLink0[iEdge].m_pEdge)
			{
				hasRemovedLink0.SetBit(iEdge);
			}

			if (!e0.m_link1.m_pEdge && ctx.m_preservedLink1[iEdge].m_pEdge)
			{
				hasRemovedLink1.SetBit(iEdge);
			}

			if (e0.m_shadowed || e0.m_link0.m_interior || e0.m_link0.m_linkIntersection != -1 || !e0.m_link0.m_pEdge)
			{
				continue;
			}

			NavItemBitArray nearby = edgeTable.At(e0.m_regTableId);
			nearby.ClearBitRange(0, iEdge);

			for (U64 iNearby : nearby)
			{
				NavEdge& e1 = pEdges[iNearby];

				if (e1.m_shadowed || e1.m_link0.m_interior || e1.m_link0.m_linkIntersection != -1 ||
					!e1.m_link0.m_pEdge || e1.m_link0.m_iEdge == iEdge || e1.m_pNavMesh != e0.m_pNavMesh)
				{
					continue;
				}

				if (IntersectLinks(ctx, e0, e1, pEdges, numEdges, edgeTable))
				{
					break;
				}
			}
		}

		NavItemBitArray toVisit;
		NavItemBitArray::BitwiseOr(&toVisit, hasRemovedLink0, hasRemovedLink1);

		for (U64 iEdge : toVisit)
		{
			NavEdge& e0 = pEdges[iEdge];

			NavItemBitArray nearby = edgeTable.At(e0.m_regTableId);
			nearby.ClearBit(iEdge);

			if (hasRemovedLink0.IsBitSet(iEdge))
			{
				IntersectNearbyRemovedLinks(ctx, nearby, hasRemovedLink0, hasRemovedLink1,
											iEdge, 0, pEdges, numEdges, edgeTable);
			}

			if (hasRemovedLink1.IsBitSet(iEdge))
			{
				IntersectNearbyRemovedLinks(ctx, nearby, hasRemovedLink0, hasRemovedLink1,
											iEdge, 1, pEdges, numEdges, edgeTable);
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct NavIntersection
	{
		enum Element : I8
		{
			kEdge,
			kLink0,

			kNumElements,
		};

		U16 m_iEdge;
		Element m_element;

		float m_t;
	};

	struct NavEdgeIntersection
	{
		NavIntersection m_edges[2];
	};

	typedef FixedArray<NavIntersection, kMaxNavItems> NavIntersectionArray;
	typedef FixedArray<NavEdgeIntersection, kMaxNavItems> NavEdgeIntersectionArray;

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void IntersectSegments(const Segment& s0,
								  const Segment& s1,
								  U32F ie0,
								  U32F ie1,
								  NavIntersection::Element elem0,
								  NavIntersection::Element elem1,
								  NavEdgeIntersectionArray& intersections)
	{
		Scalar t0, t1;
		if (IntersectSegmentSegmentXz(s0, s1, t0, t1))
		{
			NavEdgeIntersection intersection;

			intersection.m_edges[0] = { (U16)ie0, elem0, t0 };
			intersection.m_edges[1] = { (U16)ie1, elem1, t1 };

			intersections.PushBack(intersection);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void IntersectSegmentWithNavEdge(const Segment& s0,
											const NavEdge& e1,
											U32F ie0,
											U32F ie1,
											NavIntersection::Element elem0,
											NavEdgeIntersectionArray& intersections)
	{
		IntersectSegments(s0,
						  Segment(e1.m_projected0Ps, e1.m_projected1Ps),
						  ie0,
						  ie1,
						  elem0,
						  NavIntersection::kEdge,
						  intersections);

		if (!e1.m_link0.m_interior && e1.m_link0.m_pEdge)
		{
			IntersectSegments(s0,
							  Segment(e1.m_link0.m_vertsPs[0], e1.m_link0.m_vertsPs[1]),
							  ie0,
							  ie1,
							  elem0,
							  NavIntersection::kLink0,
							  intersections);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void IntersectNavEdges(U32F ie0, U32F ie1, const NavEdge* pEdges, NavEdgeIntersectionArray& intersections)
	{
		const NavEdge& e0 = pEdges[ie0];
		const NavEdge& e1 = pEdges[ie1];

		if (e0.m_link0.m_iEdge == ie1 || e0.m_link1.m_iEdge == ie1)
		{
			return;
		}

		// Intersect the edge segment
		{
			IntersectSegmentWithNavEdge(Segment(e0.m_projected0Ps, e0.m_projected1Ps),
										e1,
										ie0,
										ie1,
										NavIntersection::kEdge,
										intersections);
		}

		// Intersect the link0 segment
		if (!e0.m_link0.m_interior && e0.m_link0.m_iEdge != -1)
		{
			IntersectSegmentWithNavEdge(Segment(e0.m_link0.m_vertsPs[0], e0.m_link0.m_vertsPs[1]),
										e1,
										ie0,
										ie1,
										NavIntersection::kLink0,
										intersections);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static Segment GetNavEdgeProjectedSegment(const NavEdge& edge, NavIntersection::Element element)
	{
		switch (element)
		{
		case NavIntersection::kEdge:  return Segment(edge.m_projected0Ps, edge.m_projected1Ps);
		case NavIntersection::kLink0: return Segment(edge.m_link0.m_vertsPs[0], edge.m_link0.m_vertsPs[1]);
		}

		return Segment(kInvalidPoint, kInvalidPoint);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddNewEdgeToRegistrationTable(const NavEdge& edge,
											  NavEdge* pEdges,
											  U32F numEdges,
											  NavItemRegistrationTable& edgeTable)
	{
		const NavItemBitArray& nearby = edgeTable.At(edge.m_regTableId);
		for (U64 iEdge : nearby)
		{
			if (iEdge >= numEdges)
			{
				break;
			}

			const NavEdge& other = pEdges[iEdge];
			edgeTable.At(other.m_regTableId).SetBit(edge.m_iEdge);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void GetIntersectionHitTimes(const NavEdgeIntersectionArray& intersections,
										U32F iEdge,
										NavIntersectionArray& hitTimes)
	{
		hitTimes.Clear();

		for (const NavEdgeIntersection& intersection : intersections)
		{
			for (U32F k = 0; k < 2; k++)
			{
				if (intersection.m_edges[k].m_iEdge == iEdge)
				{
					hitTimes.PushBack(intersection.m_edges[k]);
				}
			}
		}

		std::sort(hitTimes.Data(), hitTimes.Data() + hitTimes.Size(), [](const NavIntersection& a, const NavIntersection& b)
		{
			return a.m_t < b.m_t;
		});
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static I32F SplitEdgeElement(const Segment& segProjPs,
								 const Segment& segLs,
								 NavIntersection hit0,
								 NavIntersection hit1,
								 NavEdge& edge,
								 NavEdge* pEdges,
								 U32F numEdges,
								 U32F& numNewEdges,
								 U32F maxNumEdges,
								 NavItemRegistrationTable& edgeTable)
	{
		if (numNewEdges + 1 >= maxNumEdges)
		{
			return -1;
		}

		I32F iNewEdge = numNewEdges;
		numNewEdges++;

		float t0 = hit0.m_element == NavIntersection::kEdge ? hit0.m_t : 0.0f;
		float t1 = hit1.m_element == NavIntersection::kEdge ? hit1.m_t : 1.0f;
		float t = Lerp(t0, t1, 0.5f);

		Point splitPointProjPs = Lerp(segProjPs, t);
		Point splitPointProjLs = edge.m_pNavMesh->LocalToParent(splitPointProjPs);
		Point splitPointLs = Lerp(segLs, t);
		Point splitPointPs = edge.m_pNavMesh->LocalToParent(splitPointLs);

		NavEdge& newEdge = pEdges[iNewEdge];

		newEdge = edge;
		newEdge.m_iEdge = iNewEdge;
		newEdge.m_v0Ls = splitPointLs;
		newEdge.m_v0Ps = splitPointPs;
		newEdge.m_projected0Ps = splitPointProjPs;

		newEdge.m_link0.m_interior = true;
		newEdge.m_link0.m_linkAngleDeg = 0.0f;
		newEdge.m_link0.m_iEdge = edge.m_iEdge;
		newEdge.m_link0.m_pEdge = &pEdges[newEdge.m_link0.m_iEdge];
		newEdge.m_link0.m_vertNormalLs = newEdge.m_normalLs;
		newEdge.m_link0.m_vertsLs[0] = splitPointProjLs;
		newEdge.m_link0.m_vertsLs[1] = splitPointProjLs;
		newEdge.m_link0.m_vertsPs[0] = splitPointProjPs;
		newEdge.m_link0.m_vertsPs[1] = splitPointProjPs;

		if (edge.m_link1.m_iEdge != -1)
		{
			NavEdge& e1 = pEdges[edge.m_link1.m_iEdge];
			e1.m_link0.m_iEdge = iNewEdge;
			e1.m_link0.m_pEdge = &pEdges[e1.m_link0.m_iEdge];
		}

		edge.m_v1Ls = splitPointLs;
		edge.m_v1Ps = splitPointPs;
		edge.m_projected1Ps = splitPointProjPs;

		edge.m_link1.m_interior = true;
		edge.m_link1.m_linkAngleDeg = 0.0f;
		edge.m_link1.m_iEdge = iNewEdge;
		edge.m_link1.m_pEdge = &pEdges[edge.m_link1.m_iEdge];
		edge.m_link1.m_vertNormalLs = newEdge.m_normalLs;
		edge.m_link1.m_vertsLs[0] = splitPointProjLs;
		edge.m_link1.m_vertsLs[1] = splitPointProjLs;
		edge.m_link1.m_vertsPs[0] = splitPointProjPs;
		edge.m_link1.m_vertsPs[1] = splitPointProjPs;

		AddNewEdgeToRegistrationTable(newEdge, pEdges, numEdges, edgeTable);

		return iNewEdge;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F SplitEdgeBetweenIntersectionPairs(const PathFindContext& pathContext,
												  U32 iEdge,
												  const NavEdgeIntersectionArray& intersections,
												  NavIntersectionArray& hits,
												  NavEdge* pEdges,
												  U32F numEdges,
												  U32F newNumEdges,
												  U32F maxNumEdges,
												  NavItemRegistrationTable& edgeTable)
	{
		GetIntersectionHitTimes(intersections, iEdge, hits);

		if (hits.Size() <= 1)
		{
			return newNumEdges;
		}

		NavEdge* pEdge = pEdges + iEdge;

		U32 iHit = 0;
		{
			Segment seg = GetNavEdgeProjectedSegment(*pEdge, hits[0].m_element);
			seg.b = Lerp(seg, hits[0].m_t);

			if (!IsPointClearOfEdgesPs(Lerp(seg, 0.5f), pathContext.m_pathRadius, iEdge,
									   pEdges, newNumEdges, edgeTable))
			{
				iHit++;
			}
		}

		// Remove hit times that are close together or are from the same link edge
		{
			Segment prevSeg = GetNavEdgeProjectedSegment(*pEdge, hits[0].m_element);
			Point prev = Lerp(prevSeg, hits[0].m_t);
			NavIntersection::Element prevElem = hits[0].m_element;

			U32 iCurr = 1;
			while (iCurr < hits.Size())
			{
				Segment seg = GetNavEdgeProjectedSegment(*pEdge, hits[iCurr].m_element);
				Point next = Lerp(seg, hits[iCurr].m_t);
				NavIntersection::Element nextElem = hits[iCurr].m_element;

				if ((DistXzSqr(prev, next) < kLinkDist * kLinkDist) ||
					(prevElem != NavIntersection::kEdge && prevElem == nextElem))
				{
					NavIntersection* pDst = hits.Data() + iCurr;
					NavIntersection* pSrc = pDst + 1;

					std::memmove(pDst, pSrc, sizeof(NavIntersection) * (hits.Size() - 1));
					hits.Resize(hits.Size() - 1);
				}
				else
				{
					prev = next;
					prevElem = nextElem;
					iCurr++;
				}
			}
		}

		Segment segProjPs = GetNavEdgeProjectedSegment(*pEdge, NavIntersection::kEdge);
		Segment segLs = Segment(pEdge->m_v0Ls, pEdge->m_v1Ls);

		while (iHit + 1 < hits.Size())
		{
			I32F iNewEdge = SplitEdgeElement(segProjPs,
											 segLs,
											 hits[iHit],
											 hits[iHit + 1],
											 *pEdge,
											 pEdges,
											 numEdges,
											 newNumEdges,
											 maxNumEdges,
											 edgeTable);

			if (iNewEdge == -1)
			{
				break;
			}

			pEdge = pEdges + iNewEdge;
			iHit += 2;
		}

		return newNumEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F SplitEdges(const PathFindContext& pathContext,
						   NavEdge* pEdges,
						   U32F numEdges,
						   U32F maxNumEdges,
						   NavItemRegistrationTable& edgeTable,
						   SafeAllocator& allocator)
	{
		struct IntersectCount
		{
			U16 m_count = 0;
		};

		IntersectCount* pIntersectCount = allocator.Allocate<IntersectCount>(numEdges, FILE_LINE_FUNC);
		NavEdgeIntersectionArray& intersections = *allocator.Allocate<NavEdgeIntersectionArray>(FILE_LINE_FUNC);
		NavIntersectionArray& hitTimes = *allocator.Allocate<NavIntersectionArray>(FILE_LINE_FUNC);

		for (U32F iEdge = 0; iEdge < numEdges; iEdge++)
		{
			const NavEdge& e0 = pEdges[iEdge];

			NavItemBitArray nearby = edgeTable.At(e0.m_regTableId);
			nearby.ClearBitRange(0, iEdge);

			for (U64 iOtherEdge : nearby)
			{
				U32 numIntersections = intersections.Size();
				IntersectNavEdges(iEdge, iOtherEdge, pEdges, intersections);

				for (U32F i = numIntersections; i < intersections.Size(); i++)
				{
					const NavEdgeIntersection& intersection = intersections[i];
					for (U32F k = 0; k < 2; k++)
					{
						pIntersectCount[intersection.m_edges[k].m_iEdge].m_count++;
					}
				}
			}
		}

		U32F newNumEdges = numEdges;

		for (U32F iEdge = 0; iEdge < numEdges; iEdge++)
		{
			if (pIntersectCount[iEdge].m_count > 1)
			{
				newNumEdges = SplitEdgeBetweenIntersectionPairs(pathContext,
																iEdge,
																intersections,
																hitTimes,
																pEdges,
																numEdges,
																newNumEdges,
																maxNumEdges,
																edgeTable);
			}
		}

		return newNumEdges;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void LinkEdges(const PathFindContext& pathContext,
						  NavEdge* pEdges,
						  U32F numEdges,
						  const NavItemRegistrationTable& edgeTable)
	{
		const float pathRadius = pathContext.m_pathRadius;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			NavEdge& e0 = pEdges[iEdge];

			const Vector offsetPs = (e0.m_normalPs * pathRadius);
			e0.m_projected0Ps = e0.m_v0Ps + offsetPs;
			e0.m_projected1Ps = e0.m_v1Ps + offsetPs;
		}

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			NavEdge& e0 = pEdges[iEdge];

			const NavItemBitArray& nearbyEdges = edgeTable.At(e0.m_regTableId);

			I32F iBestLinkRight		= -1;
			I32F iBestLinkLeft		= -1;
			float bestRightAngleDeg = e0.m_link1.m_pEdge ? e0.m_link1.m_linkAngleDeg : -kLargeFloat;
			float bestLeftAngleDeg	= e0.m_link0.m_pEdge ? e0.m_link0.m_linkAngleDeg : -kLargeFloat;
			bool bestRightInterior	= false;
			bool bestLeftInterior	= false;

			for (const U64 iOtherEdge : nearbyEdges)
			{
				if (iOtherEdge == iEdge)
				{
					continue;
				}

				NavEdge& e1 = pEdges[iOtherEdge];

				if (Dist(e0.m_v1Ps, e1.m_v0Ps) < kLinkDist)
				{
					const Vector edgeDir0XzPs = e0.m_pNavMesh->LocalToParent(e0.m_edgeDirXzLs);
					const Vector edgeDir1XzPs = e1.m_pNavMesh->LocalToParent(e1.m_edgeDirXzLs);
					const float cy = CrossY(edgeDir0XzPs, edgeDir1XzPs);
					const float linkAngleDeg = RADIANS_TO_DEGREES(SafeAsin(cy));

					if (linkAngleDeg > bestRightAngleDeg)
					{
						bestRightAngleDeg = linkAngleDeg;
						iBestLinkRight = iOtherEdge;
						bestRightInterior = cy > 0.0f;
					}
				}

				if (Dist(e0.m_v0Ps, e1.m_v1Ps) < kLinkDist)
				{
					const Vector edgeDir0XzPs = e0.m_pNavMesh->LocalToParent(e0.m_edgeDirXzLs);
					const Vector edgeDir1XzPs = e1.m_pNavMesh->LocalToParent(e1.m_edgeDirXzLs);
					const float cy = CrossY(edgeDir1XzPs, edgeDir0XzPs);
					const float linkAngleDeg = RADIANS_TO_DEGREES(SafeAsin(cy));

					if (linkAngleDeg > bestLeftAngleDeg)
					{
						bestLeftAngleDeg = linkAngleDeg;
						iBestLinkLeft = iOtherEdge;
						bestLeftInterior = cy > 0.0f;
					}
				}
			}

			if (iBestLinkRight >= 0)
			{
				NavEdge& e1 = pEdges[iBestLinkRight];
				LinkEdgeRight(pEdges, e0, e1, bestRightInterior, bestRightAngleDeg, pathRadius);
			}

			if (iBestLinkLeft >= 0)
			{
				NavEdge& e1 = pEdges[iBestLinkLeft];
				LinkEdgeLeft(pEdges, e0, e1, bestLeftInterior, bestLeftAngleDeg, pathRadius);
			}
		}

		// Connect the projected edge endpoints to their exterior links. We do this to have accurate projected edges
		// to detect when we need to split edges.
		for (U32F iEdge = 0; iEdge < numEdges; iEdge++)
		{
			NavEdge& e = pEdges[iEdge];

			if (!e.m_link0.m_interior && e.m_link0.m_iEdge != -1)
			{
				e.m_projected0Ps = e.m_link0.m_vertsPs[1];
			}

			if (!e.m_link1.m_interior && e.m_link1.m_iEdge != -1)
			{
				e.m_projected1Ps = e.m_link1.m_vertsPs[0];
			}
		}

		ValidateEdgeNetwork(pEdges, numEdges);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void GenerateProjectedEdgeVerts(const PathFindContext& pathContext, NavEdge* pEdges, U32F numEdges)
	{
		const float pathRadius = pathContext.m_pathRadius;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			NavEdge& e = pEdges[iEdge];

			const Vector offsetPs = (e.m_normalPs * pathContext.m_pathRadius);
			e.m_projected0Ps = e.m_v0Ps + offsetPs;
			e.m_projected1Ps = e.m_v1Ps + offsetPs;

			float proj0TT = 0.0f;
			float proj1TT = 1.0f;

			if (e.m_link0.m_pEdge)
			{
				const NavEdge& le0 = *e.m_link0.m_pEdge;
				const Vector vertNormalLs = e.m_link0.m_vertNormalLs;
				const bool interiorCorner = DotXz(e.m_edgeDirXzLs, vertNormalLs) > 0.0f;

				if (interiorCorner)
				{
					const float normDot = DotXz(e.m_normalLs, le0.m_normalLs);
					const float normAngleRad = SafeAcos(normDot);

					const float pushOutDist = pathRadius / Cos(normAngleRad * 0.5f);

					const Point startPosLs = e.m_v0Ls + (vertNormalLs * pushOutDist);

					e.m_projected0Ps = e.m_pNavMesh->LocalToParent(startPosLs);

					Scalar tt;
					DistPointLineXz(startPosLs, e.m_v0Ls, e.m_v1Ls, nullptr, &tt);
					proj0TT = tt;
				}
			}

			if (e.m_link1.m_pEdge)
			{
				const NavEdge& le1 = *e.m_link1.m_pEdge;
				const Vector vertNormalLs = e.m_link1.m_vertNormalLs;
				const bool interiorCorner = DotXz(e.m_edgeDirXzLs, vertNormalLs) > 0.0f;

				if (interiorCorner)
				{
					const float normDot = DotXz(e.m_normalLs, le1.m_normalLs);
					const float normAngleRad = SafeAcos(normDot);

					const float pushOutDist = pathRadius / Cos(normAngleRad * 0.5f);

					const Point startPosLs = e.m_v1Ls + (vertNormalLs * pushOutDist);

					e.m_projected1Ps = e.m_pNavMesh->LocalToParent(startPosLs);

					Scalar tt;
					DistPointLineXz(startPosLs, e.m_v0Ls, e.m_v1Ls, nullptr, &tt);
					proj1TT = tt;
				}
			}

			if (proj1TT <= proj0TT)
			{
				e.m_shadowed = true;
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static float GetEdgeMidTT(const NavCorner& c, bool link0, float pathRadius, bool debugDraw)
	{
		const NavCorner::Link& link = link0 ? c.m_link0 : c.m_link1;

		if (!link.m_pCorner)
		{
			return link0 ? 0.0f : 1.0f;
		}

		const NavEdge& edge = link0 ? c.m_e0 : c.m_e1;

		if (edge.m_pNavPoly->IsPreLink())
		{
			return link0 ? 0.0f : 1.0f;
		}

		const Point base0 = c.m_e0.m_v1Ls;
		const Point base1 = link.m_pCorner->m_e0.m_v1Ls;
		const Vector baseEdgeDir = AsUnitVectorXz(base1 - base0, kZero);

		const float dot0 = DotXz(c.m_vertNormal, baseEdgeDir);
		const float dot1 = DotXz(link.m_pCorner->m_vertNormal, baseEdgeDir);

		Scalar tt0 = SCALAR_LC(0.0f);
		Scalar tt1 = SCALAR_LC(1.0f);
		Point c0, c1;

		if (dot0 > 0.0f)
		{
			const float theta = SafeAcos(dot0);
			const float sinTheta = Sin(theta);
			const float push0 = pathRadius / sinTheta;
			const Point p0 = base0 + (c.m_vertNormal * push0);

			DistPointSegmentXz(p0, base0, base1, &c0, &tt0);
		}

		if (dot1 < 0.0f)
		{
			const float theta = SafeAcos(dot1);
			const float sinTheta = Sin(theta);
			const float push1 = pathRadius / sinTheta;
			const Point p1 = base1 + (link.m_pCorner->m_vertNormal * push1);

			DistPointSegmentXz(p1, base0, base1, &c1, &tt1);

			if (debugDraw)
			{
				g_prim.Draw(DebugLine(base1, p1, kColorBlueTrans));
				g_prim.Draw(DebugLine(base0, base1, kColorBlue, 4.0f));
				g_prim.Draw(DebugLine(p1, c1, kColorBlue));
				g_prim.Draw(DebugCross(c1, 0.1f, kColorBlue));
			}
		}

		//tt0 = Min(tt0, SCALAR_LC(0.5f));
		//tt1 = Max(tt1, SCALAR_LC(0.5f));

		const float tt = Lerp(float(tt0), float(tt1), 0.5f);

		return link0 ? (1.0f - tt) : tt;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void GenerateCornerVerts(NavCorner& c, float pathRadius, bool debugDraw)
	{
		PROFILE_ACCUM(GenerateCornerVerts);

		c.m_e0midTT = GetEdgeMidTT(c, true, pathRadius, false) - 0.01f;

		c.m_numPortalVerts = 0;
		const Point e0mid = Lerp(c.m_e0.m_v0Ls, c.m_e0.m_v1Ls, c.m_e0midTT);
		const Point e0outerMid = e0mid + (c.m_e0.m_normalLs * pathRadius);

		c.m_portalVerts[c.m_numPortalVerts++] = e0outerMid;

		const Point vertPos = c.m_e0.m_v1Ls;

		const bool interiorCorner = DotXz(AsUnitVectorXz(c.m_e0.m_v0Ls - c.m_e0.m_v1Ls, kZero), c.m_vertNormal) > 0.0f;

		if (debugDraw)
		{
			const Point v00Ws = c.m_e0.m_pNavMesh->LocalToWorld(c.m_e0.m_v0Ls);
			const Point v01Ws = c.m_e0.m_pNavMesh->LocalToWorld(c.m_e0.m_v1Ls);

			const Point v10Ws = c.m_e1.m_pNavMesh->LocalToWorld(c.m_e1.m_v0Ls);
			const Point v11Ws = c.m_e1.m_pNavMesh->LocalToWorld(c.m_e1.m_v1Ls);

			g_prim.Draw(DebugLine(v00Ws, v01Ws, kColorYellow, 4.0f));
			g_prim.Draw(DebugLine(v10Ws, v11Ws, kColorOrange, 4.0f));

			g_prim.Draw(DebugLine(AveragePos(v00Ws, v01Ws), c.m_e0.m_normalLs, kColorYellow));
			g_prim.Draw(DebugLine(AveragePos(v10Ws, v11Ws), c.m_e0.m_normalLs, kColorOrange));

			g_prim.Draw(DebugCircle(v01Ws, kUnitYAxis, pathRadius, kColorYellowTrans));
		}

		if (interiorCorner)
		{
			const float normDot = DotXz(c.m_e0.m_normalLs, c.m_e1.m_normalLs);
			const float normAngleRad = SafeAcos(normDot);

			const float pushOutDist = pathRadius / Cos(normAngleRad * 0.5f);

			const Point startPos = c.m_e0.m_v1Ls + (c.m_vertNormal * pushOutDist);
			c.m_portalVerts[c.m_numPortalVerts++] = startPos;

			if (debugDraw)
			{
				g_prim.Draw(DebugLine(startPos, c.m_vertNormal * pathRadius, kColorOrange));
				g_prim.Draw(DebugString(vertPos + (c.m_vertNormal * pathRadius), StringBuilder<256>("%0.1f", RADIANS_TO_DEGREES(normAngleRad)).c_str(), kColorOrange, 0.5f));
			}
		}
		else
		{
			const float intDot0 = DotXz(c.m_e0.m_normalLs, c.m_vertNormal);
			const float intDot1 = DotXz(c.m_e1.m_normalLs, c.m_vertNormal);
			const float intAngleRad = SafeAcos(intDot0);
			const float intAngleRad2 = SafeAcos(intDot1);

			const float l = Tan(intAngleRad * 0.5f) * pathRadius;

			const Point basePos = vertPos + (c.m_vertNormal * pathRadius);
			const Vector offset = RotateY90(c.m_vertNormal) * l;
			const Point p0 = basePos + offset;
			const Point p1 = basePos - offset;

			if (debugDraw)
			{
				g_prim.Draw(DebugLine(vertPos, basePos, kColorYellow));
				g_prim.Draw(DebugLine(p0, p1, kColorOrange, 4.0f));
				g_prim.Draw(DebugLine(p0, vertPos + (c.m_e0.m_normalLs * pathRadius), kColorOrange, 4.0f));
				g_prim.Draw(DebugLine(p1, vertPos + (c.m_e1.m_normalLs * pathRadius), kColorOrange, 4.0f));

				g_prim.Draw(DebugString(p0, "p0", kColorOrange, 0.5f));
				g_prim.Draw(DebugString(p1, "p1", kColorOrange, 0.5f));
			}

			if (c.m_oppEdgeDist < (pathRadius * 2.5f))
			{
				const NavEdge& oe = c.m_oppEdge;

				const Segment cylSeg = Segment(oe.m_v0Ls, oe.m_v1Ls);

				float tt = -1.0f;

				if (IntersectSegmentStadiumXz(Segment(p0, p1), cylSeg, pathRadius, tt))
				{
					const Point mid0 = Lerp(p0, p1, tt);
					c.m_portalVerts[c.m_numPortalVerts++] = p0;
					c.m_portalVerts[c.m_numPortalVerts++] = mid0;

					const Segment revSeg = Segment(c.m_e1.m_v1Ls + (c.m_e1.m_normalLs * pathRadius), p1);
					if (IntersectSegmentStadiumXz(revSeg, cylSeg, pathRadius, tt))
					{
						const Point mid1 = Lerp(revSeg.a, revSeg.b, tt);
						c.m_portalVerts[c.m_numPortalVerts++] = mid1;
					}
					else
					{
						c.m_portalVerts[c.m_numPortalVerts++] = p1;
					}
				}
				else if (IntersectSegmentStadiumXz(Segment(p1, p0), cylSeg, pathRadius, tt))
				{
					const Point mid0 = Lerp(p1, p0, tt);
					const Point e0outer0 = c.m_e0.m_v0Ls + (c.m_e0.m_normalLs * pathRadius);
					const Segment revSeg = Segment(e0outer0, p0);
					if (IntersectSegmentStadiumXz(revSeg, cylSeg, pathRadius, tt))
					{
						const Point mid1 = Lerp(revSeg.a, revSeg.b, tt);

						//g_prim.Draw(DebugString(mid0, "mid0", kColorRed, 0.5f));
						//g_prim.Draw(DebugString(mid1, "mid1", kColorRed, 0.5f));
						//g_prim.Draw(DebugCircle(c.m_e0.m_v1, kUnitYAxis, pathRadius, kColorYellowTrans));

						c.m_portalVerts[c.m_numPortalVerts++] = mid1;
						c.m_portalVerts[c.m_numPortalVerts++] = mid0;
						c.m_portalVerts[c.m_numPortalVerts++] = p1;
					}
					else
					{
						c.m_portalVerts[c.m_numPortalVerts++] = p0;
						c.m_portalVerts[c.m_numPortalVerts++] = p1;
					}
				}
				else
				{
					c.m_portalVerts[c.m_numPortalVerts++] = p0;
					c.m_portalVerts[c.m_numPortalVerts++] = p1;
				}
			}
			else
			{
				c.m_portalVerts[c.m_numPortalVerts++] = p0;
				c.m_portalVerts[c.m_numPortalVerts++] = p1;
			}
		}

		c.m_e1midTT = GetEdgeMidTT(c, false, pathRadius, false) + 0.01f;
		const Point e1mid = Lerp(c.m_e1.m_v0Ls, c.m_e1.m_v1Ls, c.m_e1midTT);
		const Point e1outerMid = e1mid + (c.m_e1.m_normalLs * pathRadius);
		c.m_portalVerts[c.m_numPortalVerts++] = e1outerMid;

		NAV_ASSERT(c.m_numPortalVerts < ARRAY_COUNT(c.m_portalVerts));
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static I32F FindOpposingEdge(NavCorner* pCorner,
								 const NavEdge* pEdges,
								 const U32F numEdges,
								 float pathRadius,
								 I32F iIgnoreEdge0,
								 I32F iIgnoreEdge1)
	{
		PROFILE_ACCUM(FindOpposingEdge);

		if (!pCorner || !pEdges || (0 == numEdges) || (pathRadius < kSmallFloat))
			return -1;

		I32F bestEdge = -1;
		float bestDist = (pathRadius * 2.5f);

		const NavEdge& e0 = pEdges[iIgnoreEdge0];
		const NavEdge& e1 = pEdges[iIgnoreEdge1];

		for (U32F iOppEdge = 0; iOppEdge < numEdges; ++iOppEdge)
		{
			if ((iOppEdge == iIgnoreEdge0) || (iOppEdge == iIgnoreEdge1))
				continue;

			Scalar closestVertDist = SCALAR_LC(kLargeFloat);
			const NavEdge& oe = pEdges[iOppEdge];

			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v0Ls, e0.m_v0Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v0Ls, e0.m_v1Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v0Ls, e1.m_v0Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v0Ls, e1.m_v1Ls));

			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v1Ls, e0.m_v0Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v1Ls, e0.m_v1Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v1Ls, e1.m_v0Ls));
			closestVertDist = Min(closestVertDist, DistXzSqr(oe.m_v1Ls, e1.m_v1Ls));

			if (closestVertDist < SCALAR_LC(0.01f))
				continue;

			Point closestPosLs;
			const float d = DistPointSegmentXz(e0.m_v1Ls, oe.m_v0Ls, oe.m_v1Ls, &closestPosLs);

			const Vector toEdgeLs = closestPosLs - pCorner->m_e0.m_v1Ls;
			const float dotP = DotXz(toEdgeLs, pCorner->m_vertNormal);
			if (dotP < 0.2f)
				continue;

			if ((d > 0.01f) && (d < bestDist))
			{
				if (d < pCorner->m_oppEdgeDist)
				{
					bestDist = d;
					bestEdge = iOppEdge;
					break;
				}
			}
		}

		if (bestEdge >= 0)
		{
			pCorner->m_oppEdgeDist = bestDist;
			pCorner->m_oppEdge = pEdges[bestEdge];
		}

		return bestEdge;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void LinkCorners(NavCorner* pCornerA, U32F iA, NavCorner* pCornerB, U32F iB)
	{
		//NAV_ASSERT(pCornerA && (pCornerA->m_link0.m_pCorner == nullptr) && (pCornerA->m_link1.m_pCorner == nullptr));
		//NAV_ASSERT(pCornerB && (pCornerB->m_link0.m_pCorner == nullptr) && (pCornerB->m_link1.m_pCorner == nullptr));

		NavCorner::Link* pLinkA = nullptr;
		NavCorner::Link* pLinkB = nullptr;

		if (pCornerA->m_e0.m_iEdge == pCornerB->m_e0.m_iEdge)
		{
			pLinkA = &pCornerA->m_link0;
			pLinkB = &pCornerB->m_link0;
		}
		else if (pCornerA->m_e0.m_iEdge == pCornerB->m_e1.m_iEdge)
		{
			pLinkA = &pCornerA->m_link0;
			pLinkB = &pCornerB->m_link1;
		}
		else if (pCornerA->m_e1.m_iEdge == pCornerB->m_e0.m_iEdge)
		{
			pLinkA = &pCornerA->m_link1;
			pLinkB = &pCornerB->m_link0;
		}
		else if (pCornerA->m_e1.m_iEdge == pCornerB->m_e1.m_iEdge)
		{
			pLinkA = &pCornerA->m_link1;
			pLinkB = &pCornerB->m_link1;
		}
		else
		{
			NAV_ASSERTF(false,
						("Tried to link incompatible corners [%d, %d] != [%d, %d]",
						 pCornerA->m_e0.m_iEdge,
						 pCornerA->m_e1.m_iEdge,
						 pCornerB->m_e0.m_iEdge,
						 pCornerB->m_e1.m_iEdge));

			return;
		}

		//NAV_ASSERT(pLinkA->m_pCorner == nullptr);
		//NAV_ASSERT(pLinkB->m_pCorner == nullptr);

		/*
		if (pLinkA->m_pCorner)
		{
		MsgOut("ERROR! Corner %d is already linked\n", iA);
		return;
		}

		if (pLinkB->m_pCorner)
		{
		MsgOut("ERROR! Corner %d is already linked\n", iB);
		return;
		}
		*/

		pLinkA->m_iCorner = iB;
		pLinkA->m_pCorner = pCornerB;

		pLinkB->m_iCorner = iA;
		pLinkB->m_pCorner = pCornerA;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F BuildPotentialCornerList(const NavEdge* pEdges,
										 U32F numEdges,
										 float pathRadius,
										 NavCorner* pCornerListOut,
										 U32F maxCornersOut)
	{
		PROFILE_AUTO(Navigation);
		PROFILE_ACCUM(BuildPotentialCornerList);

		if (!pEdges || (0 == numEdges))
			return 0;

		if (!pCornerListOut || (0 == maxCornersOut))
			return 0;

		U32F numCorners = 0;

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		I32F* pPairTable = NDI_NEW I32F[numEdges];
		memset(pPairTable, -1, sizeof(I32F) * numEdges);

		const float minDist = 1.25f * pathRadius;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const NavEdge& e0 = pEdges[iEdge];

			for (U32F iOtherEdge = 0; iOtherEdge < numEdges; ++iOtherEdge)
			{
				if (iEdge == iOtherEdge)
					continue;

				const NavEdge& e1 = pEdges[iOtherEdge];

				if (e0.m_sourceNavId.m_navMeshIndex != e1.m_sourceNavId.m_navMeshIndex)
					continue;

				if (Dist(e0.m_v1Ls, e1.m_v0Ls) > 0.0001f)
					continue;

				const Vector vertNormalLs = AsUnitVectorXz(e0.m_normalLs + e1.m_normalLs, kZero);

				if (numCorners >= maxCornersOut)
					break;

				NavCorner& newCorner = pCornerListOut[numCorners];
				newCorner.m_e0 = e0;
				newCorner.m_e1 = e1;
				newCorner.m_vertNormal = vertNormalLs;
				newCorner.m_oppEdgeDist = kLargeFloat;

				const I32F iOppEdge = FindOpposingEdge(&newCorner, pEdges, numEdges, pathRadius, iEdge, iOtherEdge);

				if (newCorner.m_oppEdgeDist < minDist)
				{
					//ignoreList[iEdge] = true;
					//ignoreList[iOtherEdge] = true;
					//ignoreList[iOppEdge] = true;
					if (false)
					{
						const NavEdge& oe = pEdges[iOtherEdge];
						oe.DebugDraw(pathRadius);

						const Point cornerPosWs = e0.m_pNavMesh->LocalToWorld(e0.m_v1Ls);
						const Point oe0Ws = oe.m_pNavMesh->LocalToWorld(oe.m_v0Ls);
						const Point oe1Ws = oe.m_pNavMesh->LocalToWorld(oe.m_v1Ls);
						Point closestWs;
						const float d = DistPointSegmentXz(cornerPosWs, oe0Ws, oe1Ws, &closestWs);
						g_prim.Draw(DebugArrow(cornerPosWs, closestWs, kColorRed), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugSphere(cornerPosWs, 0.2f));
					}
					continue;
				}

				if (pPairTable[iEdge] < 0)
				{
					//MsgOut("pPairTable[%d] = %d\n", iEdge, numCorners);
					pPairTable[iEdge] = numCorners;
				}
				else
				{
					const I64 iOtherCorner = pPairTable[iEdge];
					//MsgOut("Linking %d and %d with shared edge %d\n", numCorners, iOtherCorner, iEdge);
					LinkCorners(&newCorner, numCorners, &pCornerListOut[iOtherCorner], iOtherCorner);
				}

				if (pPairTable[iOtherEdge] < 0)
				{
					//MsgOut("pPairTable[%d] = %d\n", iOtherEdge, numCorners);
					pPairTable[iOtherEdge] = numCorners;
				}
				else
				{
					const I64 iOtherCorner = pPairTable[iOtherEdge];
					//MsgOut("Linking %d and %d with shared edge %d\n", numCorners, iOtherCorner, iOtherEdge);
					LinkCorners(&newCorner, numCorners, &pCornerListOut[iOtherCorner], iOtherCorner);
				}

				++numCorners;
			}
		}

		bool failed = false;

		// find neighboring corners from nav mesh links
		if (numCorners > 0)
		{
			for (U32F iCorner = 0; iCorner < (numCorners - 1); ++iCorner)
			{
				NavCorner& c0 = pCornerListOut[iCorner];
				if (!c0.m_e0.m_fromLink || !c0.m_e1.m_fromLink)
					continue;

				for (U32F iOtherCorner = iCorner + 1; iOtherCorner < numCorners; ++iOtherCorner)
				{
					NavCorner& c1 = pCornerListOut[iOtherCorner];

					if (c0.m_e0.m_pNavMesh == c1.m_e0.m_pNavMesh)
						continue;

					if (!c1.m_e0.m_fromLink || !c1.m_e1.m_fromLink)
						continue;

					const Point pos0Ws = c0.m_e0.m_pNavMesh->LocalToWorld(c0.m_e0.m_v0Ls);
					const Point pos1Ws = c1.m_e0.m_pNavMesh->LocalToWorld(c1.m_e0.m_v0Ls);

					if (Dist(pos0Ws, pos1Ws) > 0.01f)
						continue;

					if ((c0.m_overlap.m_pCorner == nullptr) && (c1.m_overlap.m_pCorner == nullptr))
					{
						// link c0.m_link0 to c1.m_link1
						c0.m_overlap.m_iCorner = iOtherCorner;
						c0.m_overlap.m_pCorner = &c1;

						c1.m_overlap.m_iCorner = iCorner;
						c1.m_overlap.m_pCorner = &c0;
					}
				}
			}
		}

#ifdef JBELLOMY
		if (FALSE_IN_FINAL_BUILD(failed))
		{
			for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
			{
				const NavEdge& e0 = pEdges[iEdge];
				e0.DebugDraw(pathRadius, kPrimDuration1Frame);
			}

			g_ndConfig.m_pDMenuMgr->SetProgPause(true);
			g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
		}
#endif

		// do this after we link because our verts depend on the angle of neighboring corners
		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			NavCorner& c = pCornerListOut[iCorner];
			GenerateCornerVerts(c, pathRadius, false);
		}

		return numCorners;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddNavItemRegistration(NavItemRegistrationTable* pTableOut, const NavManagerId navId, U64 iItem)
	{
		NavItemRegistrationTable::Iterator itr = pTableOut->Find(navId);

		if (itr != pTableOut->End())
		{
			itr->m_value.SetBit(iItem);
		}
		else
		{
			if (pTableOut->IsFull())
			{
				const size_t oldSize = pTableOut->Size();
				const size_t newSize = oldSize * 2;

#if DEBUG_NAV_ITEM_TABLE
				MsgCon("Nav registration table overflow! Reallocating [capacity: %d -> %d]\n", oldSize, newSize);
#endif

				AllocateJanitor frameAlloc(kAllocSingleFrame, FILE_LINE_FUNC);

				NavItemRegistrationTable* pNewTable = NDI_NEW NavItemRegistrationTable;
				pNewTable->Init(newSize, FILE_LINE_FUNC);

				for (const NavItemRegistrationTable::Bucket* pSrcBucket : *pTableOut)
				{
					pNewTable->Set(pSrcBucket->m_key, pSrcBucket->m_value);
				}

				*pTableOut = *pNewTable;
			}

			NavItemBitArray freshBits(false);
			freshBits.SetBit(iItem);
			pTableOut->Set(navId, freshBits);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddNavItemRegistrationFromEdge(const NavEdge& edge,
											   U32F iItem,
											   float maxDist,
											   NavItemRegistrationTable* pTableOut,
											   bool forCorner)
	{
		PROFILE_AUTO(Navigation);

		const U32F polyCount = edge.m_pNavMesh->GetPolyCount();
		const NavManagerId polyNavId = edge.m_regTableId;

		AddNavItemRegistration(pTableOut, polyNavId, iItem);

		const NavPoly* pPoly = edge.m_pNavPoly;

		if (pPoly->IsLink())
		{
			if (const NavPoly* pLinkedPoly = pPoly->GetLinkedPoly())
			{
				const NavManagerId linkedMgrId = pLinkedPoly->GetNavManagerId();

				AddNavItemRegistration(pTableOut, linkedMgrId, iItem);
			}
		}

		if (const NavPolyDistEntry* pDistList = pPoly->GetPolyDistList())
		{
			NavManagerId navIdNeighbor = polyNavId;

			for (U32F i = 0; (pDistList[i].GetDist() < maxDist); ++i)
			{
				const U32F iPoly = pDistList[i].GetPolyIndex();
				if (iPoly >= polyCount)
					break;

				navIdNeighbor.m_iPoly = iPoly;
				AddNavItemRegistration(pTableOut, navIdNeighbor, iItem);

				if (!forCorner)
				{
					const NavPoly* pAdjPoly = edge.m_pNavMesh->UnsafeGetPolyFast(iPoly);
					if (pAdjPoly->IsLink())
					{
						if (const NavPoly* pLinkedPoly = pAdjPoly->GetLinkedPoly())
						{
							const NavManagerId linkedMgrId = pLinkedPoly->GetNavManagerId();

							AddNavItemRegistration(pTableOut, linkedMgrId, iItem);
						}
					}
				}
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool CreateCornerRegistrationTable(const NavCorner* pCorners,
											  U32F numCorners,
											  float expansionRadius,
											  NavItemRegistrationTable* pTableOut)
	{
		if (!pCorners || (0 == numCorners) || !pTableOut)
			return false;

		const float maxDist = (expansionRadius * 1.15f) + 0.1f;

		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			const NavCorner& c = pCorners[iCorner];

			AddNavItemRegistrationFromEdge(c.m_e0, iCorner, maxDist, pTableOut, true);

			AddNavItemRegistrationFromEdge(c.m_e1, iCorner, maxDist, pTableOut, true);
		}

#if DEBUG_NAV_ITEM_TABLE
		MsgCon("Final corner registration table: %d entries for %d corners (%0.3f per)\n",
			   pTableOut->Size(),
			   numCorners,
			   float(pTableOut->Size()) / float(numCorners));
#endif

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void CreateEdgeRegistrationTable(const NavEdge* pEdges,
											U32F numEdges,
											float expansionRadius,
											NavItemRegistrationTable* pTableOut)
	{
		const float maxDist = (expansionRadius * 1.15f) + 0.1f;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const NavEdge& e = pEdges[iEdge];

			AddNavItemRegistrationFromEdge(e, iEdge, maxDist, pTableOut, false);
		}

#if DEBUG_NAV_ITEM_TABLE
		MsgCon("Final edge registration table: %d entries for %d edges (%0.3f per)\n",
			   pTableOut->Size(),
			   numEdges,
			   float(pTableOut->Size()) / float(numEdges));
#endif
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsCornerWithinRange(const Segment& probeSeg, const NavCorner& corner, float radius)
	{
		const float yThreshold = 1.0f;

		const float probeHeightMin = Min(probeSeg.a.Y(), probeSeg.b.Y()) - (yThreshold * 0.5f);
		const float probeHeightMax = Max(probeSeg.a.Y(), probeSeg.b.Y()) + (yThreshold * 0.5f);

		const float cornerHeightMin = Min(Min(corner.m_e0.m_v0Ls.Y(), corner.m_e0.m_v1Ls.Y()), corner.m_e1.m_v1Ls.Y()) - (yThreshold * 0.5f);
		const float cornerHeightMax = Max(Max(corner.m_e0.m_v0Ls.Y(), corner.m_e0.m_v1Ls.Y()), corner.m_e1.m_v1Ls.Y()) + (yThreshold * 0.5f);

		if ((probeHeightMin <= cornerHeightMax) && (cornerHeightMin <= probeHeightMax))
		{
			Scalar t0, t1;
			const Segment e0seg = Segment(corner.m_e0.m_v0Ls, corner.m_e0.m_v1Ls);
			if (DistSegmentSegmentXz(e0seg, probeSeg, t0, t1) <= radius)
				return true;

			const Segment e1seg = Segment(corner.m_e1.m_v0Ls, corner.m_e1.m_v1Ls);
			if (DistSegmentSegmentXz(e1seg, probeSeg, t0, t1) <= radius)
				return true;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static float IntersectLegWithCorner(const Segment& probeSeg, const NavCorner& c, bool reverse, float& probeTT)
	{
		const float kNudgeDist = 0.01f;

		const Vector probeVec = probeSeg.b - probeSeg.a;
		const Vector probeDir = SafeNormalize(probeVec, kZero);
		const Segment nudgedProbeSeg = Segment(probeSeg.a - (probeDir * kNudgeDist), probeSeg.b + (probeDir * kNudgeDist));

		const float flip = reverse ? -1.0f : 1.0f;

		float cornerTT = -1.0f;

		probeTT = kLargeFloat;

		for (U32F iV = 0; iV < (c.m_numPortalVerts - 1); ++iV)
		{
			const Point v0 = c.m_portalVerts[iV];
			const Point v1 = c.m_portalVerts[iV + 1];
			const Vector edgeDir = SafeNormalize(v1 - v0, kZero);
			const Vector normDir = RotateY90(edgeDir);

			const float dotP = flip * DotXz(normDir, probeDir);
			if (dotP >= -NDI_FLT_EPSILON)
				continue;

			//const Segment edgeSeg = Segment(v0 - (edgeDir * kNudgeDist), v1 + (edgeDir * kNudgeDist));
			const Segment edgeSeg = Segment(v0, v1);

			Scalar t0, t1;
			if (!IntersectSegmentSegmentXz(nudgedProbeSeg, edgeSeg, t0, t1))
				continue;

			if ((t0 < probeTT) && (t0 > 0.0f))
			{
				probeTT = t0;
				cornerTT = t1 + float(iV);
			}
		}

		return cornerTT;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F GetCornerVertsForRange(const NavCorner& c,
									   float ttStart,
									   float ttEnd,
									   Point* pPointsLsOut,
									   U32F maxPointsOut)
	{
		const bool reverse = ttEnd < ttStart;

		const I32F iVertStart = reverse ? Floor(ttStart) : Ceil(ttStart);
		const I32F iVertEnd = reverse ? Floor(ttEnd) : Ceil(ttEnd);
		const I32F iVertDir = reverse ? -1 : 1;

		U32F numOut = 0;

		for (I32F iV = iVertStart; iV != iVertEnd; iV += iVertDir)
		{
			NAV_ASSERT((iV < c.m_numPortalVerts) && (iV >= 0));

			pPointsLsOut[numOut++] = c.m_portalVerts[iV];
		}

		return numOut;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static Point GetCornerVert(const NavCorner& c, float tt)
	{
		const I32F iVert0 = Floor(tt);
		const I32F iVert1 = Ceil(tt);
		const Scalar param = Fmod(tt);

		NAV_ASSERT(iVert0 >= 0 && iVert0 < c.m_numPortalVerts);
		NAV_ASSERT(iVert1 >= 0 && iVert1 < c.m_numPortalVerts);

		const Point p0 = c.m_portalVerts[iVert0];
		const Point p1 = c.m_portalVerts[iVert1];

		const Point p = Lerp(p0, p1, param);
		return p;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsCornerIgnored(I32F iCorner, const I32F* pIgnoredCorners, const NavCorner* pCorners)
	{
		const I32F iOverlapCorner = pCorners[iCorner].m_overlap.m_iCorner;

		for (const I32F* pIgnore = pIgnoredCorners; (pIgnore && (*pIgnore >= 0)); ++pIgnore)
		{
			const I32F iIgnore = *pIgnore;
			if (iIgnore == iCorner)
				return true;

			if (iIgnore == iOverlapCorner)
				return true;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void WalkMarkPotentialCorners(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData)
	{
		CornerWalkData* pWalkData = (CornerWalkData*)userData;

		const NavItemRegistrationTable::ConstIterator itr = pWalkData->m_pCornerTable->Find(pPoly->GetNavManagerId());

		if (itr != pWalkData->m_pCornerTable->End())
		{
			NavItemBitArray::BitwiseOr(&pWalkData->m_cornerBits, pWalkData->m_cornerBits, itr->m_value);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static NavCornerTestResult CheckLegAgainstOtherCorners(const NavMesh* pStartMesh,
														   Point_arg startPosWs,
														   Point_arg endPosWs,
														   const I32F* pIgnoreCorners,
														   const NavItemRegistrationTable& cornerTable,
														   const NavCorner* pCorners,
														   U32F numCorners,
														   float pathRadius)
	{
		CornerWalkData walkData;
		walkData.m_pCornerTable = &cornerTable;
		walkData.m_cornerBits.ClearAllBits();

		if (pStartMesh)
		{
			const Point startPosLs = pStartMesh->WorldToLocal(startPosWs);
			const Point endPosLs   = pStartMesh->WorldToLocal(endPosWs);
			NavMesh::ProbeParams walkParams;
			walkParams.m_start = startPosLs;
			walkParams.m_move  = endPosLs - startPosLs;
			walkParams.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
			walkParams.m_crossLinks = false;

			pStartMesh->WalkPolysInLineLs(walkParams, WalkMarkPotentialCorners, (uintptr_t)&walkData);
		}
		else if (numCorners > 0)
		{
			walkData.m_cornerBits.SetBitRange(0, numCorners - 1);
		}

		NavCornerTestResult res;
		res.m_iCorner = -1;
		res.m_pCorner = nullptr;
		res.m_cornerTT = -1.0f;
		res.m_probeTT = -1.0f;
		res.m_hitPointLs = kOrigin;

		float bestProbeTT = kLargeFloat;

		NavItemBitArray::Iterator iter(walkData.m_cornerBits);

		for (U32F iCorner = iter.First(); iCorner < iter.End(); iCorner = iter.Advance())
		{
			const NavCorner& corner = pCorners[iCorner];

			if (IsCornerIgnored(iCorner, pIgnoreCorners, pCorners))
				continue;

			const Point startPosLs = corner.m_e0.m_pNavMesh->WorldToLocal(startPosWs);
			const Point endPosLs = corner.m_e0.m_pNavMesh->WorldToLocal(endPosWs);
			const Segment probeSegLs = Segment(startPosLs, endPosLs);

			if (!IsCornerWithinRange(probeSegLs, corner, pathRadius * 1.1f))
				continue;

			float thisProbeTT = kLargeFloat;
			const float thisCornerTT = IntersectLegWithCorner(probeSegLs, corner, false, thisProbeTT);

			if (thisCornerTT < 0.0f)
				continue;

			if (thisProbeTT < bestProbeTT)
			{
				bestProbeTT = thisProbeTT;

				res.m_iCorner = iCorner;
				res.m_pCorner = &corner;
				res.m_hitPointLs = Lerp(startPosLs, endPosLs, thisProbeTT);
				res.m_cornerTT = thisCornerTT;
				res.m_probeTT = thisProbeTT;
			}
		}

		return res;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static NavCornerTestResult FindContainingCorner(const NavMesh* pNavMesh,
													Point_arg posWs,
													const NavManagerId navMgrId,
													const I32F* pIgnoreCorners,
													const NavItemRegistrationTable& cornerTable,
													const NavCorner* pCorners,
													U32F numCorners,
													float pathRadius,
													bool ignoreLinkedCorners)
	{
		const NavPoly* pPoly = &pNavMesh->GetPoly(navMgrId.m_iPoly);
		const Point posLs = pNavMesh->WorldToLocal(posWs);

		if (!pPoly || !pPoly->PolyContainsPointLs(posLs))
		{
			pPoly = pNavMesh->FindContainingPolyLs(posLs);
		}

		NavCornerTestResult res;
		res.m_iCorner = -1;
		res.m_pCorner = nullptr;
		res.m_cornerTT = -1.0f;
		res.m_probeTT = -1.0f;
		res.m_hitPointLs = kOrigin;

		if (!pPoly)
		{
			return res;
		}

		const NavItemRegistrationTable::ConstIterator itr = cornerTable.Find(pPoly->GetNavManagerId());

		if (itr == cornerTable.End())
		{
			return res;
		}

		const NavItemBitArray& potentialCorners = itr->m_value;

		NavItemBitArray::Iterator iter(potentialCorners);

		for (U32F iCorner = iter.First(); iCorner < iter.End(); iCorner = iter.Advance())
		{
			if (IsCornerIgnored(iCorner, pIgnoreCorners, pCorners))
				continue;

			const NavCorner& c = pCorners[iCorner];

			if (ignoreLinkedCorners)
			{
				if (c.m_link0.m_pCorner && IsCornerIgnored(c.m_link0.m_iCorner, pIgnoreCorners, pCorners))
					continue;

				if (c.m_link1.m_pCorner && IsCornerIgnored(c.m_link1.m_iCorner, pIgnoreCorners, pCorners))
					continue;
			}

			const Point posCornerLs = c.m_e0.m_pNavMesh->WorldToLocal(posWs);

			float tt = -1.0f;
			if (c.ContainsPointLs(posCornerLs, pathRadius, &tt))
			{
				res.m_iCorner = iCorner;
				res.m_pCorner = &c;
				res.m_cornerTT = tt;
				res.m_probeTT = 0.0f;
				res.m_hitPointLs = GetCornerVert(c, tt);
				break;
			}
		}

		return res;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static NavCornerTestResult TryAddNewVertsToPath(const PathFindContext& pathContext,
													Point_arg startPosWs,
													const Point* pNewVertsLs,
													U32F numNewVerts,
													const I32F iCurrentCorner,
													const I32F iPrevCorner,
													const NavItemRegistrationTable& cornerTable,
													const NavCorner* pCorners,
													U32F numCorners,
													PathWaypointsEx* pWorkingResultsPs)
	{
		const Locator& destParentSpace = pathContext.m_parentSpace;

		const float travelThresh = Max(pathContext.m_pathRadius * 0.16666666666666666666666666666667f, 0.05f);

		Point prevPosWs = startPosWs;

		NavCornerTestResult res;
		res.m_iCorner = -1;
		res.m_pCorner = nullptr;
		res.m_cornerTT = -1.0f;
		res.m_hitPointLs = Point(kOrigin);

		I32F ignoreCorners[3] = { iCurrentCorner, iPrevCorner, -1 };
		const NavCorner& curCorner = pCorners[iCurrentCorner];

		for (U32F iPos = 0; iPos < numNewVerts; ++iPos)
		{
			const NavMesh* pNavMesh = curCorner.m_e0.m_pNavMesh;
			const Point vertPosLs = pNewVertsLs[iPos];
			const Point vertPosWs = pNavMesh->LocalToWorld(vertPosLs);

			const float travelDist = DistXz(prevPosWs, vertPosWs);

			if (travelDist < travelThresh)
			{
				continue;
			}

			const NavCornerTestResult thisRes = CheckLegAgainstOtherCorners(pNavMesh,
																			prevPosWs,
																			vertPosWs,
																			ignoreCorners,
																			cornerTable,
																			pCorners,
																			numCorners,
																			pathContext.m_pathRadius * 0.9f);

			if (thisRes.m_pCorner)
			{
				const Point hitPosWs = curCorner.m_e0.m_pNavMesh->LocalToWorld(thisRes.m_hitPointLs);
				const Point hitPosPs = destParentSpace.UntransformPoint(hitPosWs);

				NavPathNodeProxy hitPathNode;
				hitPathNode.Invalidate();
				hitPathNode.m_nodeType = NavPathNode::kNodeTypePoly;
				hitPathNode.m_navMgrId = thisRes.m_pCorner->m_e0.m_pNavMesh->GetManagerId();

				pWorkingResultsPs->AddWaypoint(hitPosPs, hitPathNode);

				res = thisRes;
				break;
			}
			else
			{
				const Point vertPosPs = destParentSpace.UntransformPoint(vertPosWs);

				NavPathNodeProxy vertPathNode;
				vertPathNode.Invalidate();
				vertPathNode.m_nodeType = NavPathNode::kNodeTypePoly;
				vertPathNode.m_navMgrId = pNavMesh->GetManagerId();

				pWorkingResultsPs->AddWaypoint(vertPosPs, vertPathNode);

				prevPosWs = vertPosWs;
			}
		}

		return res;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool CanTraverseCornerLink(const NavCorner::Link& link, I32F iPrevCorner)
	{
		if (!link.m_pCorner)
			return false;

		if (iPrevCorner < 0)
			return true;

		if (link.m_iCorner == iPrevCorner)
			return false;

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool PushPathAwayFromCorners(const PathFindContext& pathContext,
										const PathWaypointsEx& inputPathPs,
										I32F iNext,
										const NavItemRegistrationTable& cornerTable,
										const NavCorner* pCorners,
										U32F numCorners,
										PathWaypointsEx* pWorkingResultsPs,
										I32F* pNewReaderOut,
										I32F& iPrevCorner)
	{
		PROFILE_AUTO(Navigation);
		PROFILE_ACCUM(PushPathAwayFromCorners);

		const U32F inputWaypointCount = inputPathPs.GetWaypointCount();

		if (iNext >= inputWaypointCount)
			return false;

		const float pathRadius = pathContext.m_pathRadius;

		const Locator& parentSpace = pathContext.m_parentSpace;

		if (false)
		{
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorCyan;
			cs.m_groundLeg1 = kColorMagenta;

			Locator modParentSpace = parentSpace;
			modParentSpace.SetPos(parentSpace.Pos() + Vector(0.0f, 0.5f, 0.0f));
			inputPathPs.DebugDraw(modParentSpace, true, 0.0f, cs);
		}

		const Point initialGoalPs = inputPathPs.GetWaypoint(iNext);
		const Point probeStartPs = pWorkingResultsPs->GetEndWaypoint();
		const NavManagerId probeStartNavId = pWorkingResultsPs->GetEndNavId();
		const NavMesh* pProbeStartMesh = NavMeshHandle(probeStartNavId).ToNavMesh();

		if (!pProbeStartMesh)
			return false;

		const Point initialGoalWs = parentSpace.TransformPoint(initialGoalPs);
		const Point baseProbeStartWs = parentSpace.TransformPoint(probeStartPs);
		const Vector initialProbeDirWs = SafeNormalize(initialGoalWs - baseProbeStartWs, kZero);
		const Point probeStartWs = baseProbeStartWs - (initialProbeDirWs * SCALAR_LC(0.0001f));

		I32F iCurrentCorner = -1;
		float currentCornerTT = -1.0f;

		I32F ignoreCorners[2] = { iPrevCorner, -1 };
		NavCornerTestResult initialRes = CheckLegAgainstOtherCorners(pProbeStartMesh,
																	 probeStartWs,
																	 initialGoalWs,
																	 ignoreCorners,
																	 cornerTable,
																	 pCorners,
																	 numCorners,
																	 pathContext.m_pathRadius);

		if (!initialRes.m_pCorner)
		{
			bool ignoreLinkedCorners = false;
			if (iPrevCorner >= 0)
			{
				const Point posCornerLs = pCorners[iPrevCorner].m_e0.m_pNavMesh->WorldToLocal(probeStartWs);
				float tt = -1.0f;
				ignoreLinkedCorners = pCorners[iPrevCorner].ContainsPointLs(posCornerLs, pathRadius, &tt);
			}

			initialRes = FindContainingCorner(pProbeStartMesh,
											  probeStartWs,
											  probeStartNavId,
											  ignoreCorners,
											  cornerTable,
											  pCorners,
											  numCorners,
											  pathContext.m_pathRadius,
											  ignoreLinkedCorners);
		}

		iCurrentCorner = initialRes.m_iCorner;
		currentCornerTT = initialRes.m_cornerTT;

		if (iCurrentCorner < 0)
		{
			// if we probed away from any corners, reset previous so that if we double back and re-enter
			// our last corner it wont be ignored
			iPrevCorner = -1;
			return false;
		}

		//const Point enterPosWs = Lerp(probeStartWs, initialGoalWs, initialRes.m_probeTT);
		const Point enterPosWs = initialRes.m_pCorner->m_e0.m_pNavMesh->LocalToWorld(initialRes.m_hitPointLs);
		const Point enterPosPs = parentSpace.UntransformPoint(enterPosWs);
		NavPathNodeProxy enterPathNode;
		enterPathNode.Invalidate();
		enterPathNode.m_nodeType = NavPathNode::kNodeTypePoly;
		enterPathNode.m_navMgrId = initialRes.m_pCorner->m_e0.m_pNavMesh->GetManagerId();

		pWorkingResultsPs->AddWaypoint(enterPosPs, enterPathNode);

		I32F iInputExit = iNext;

		Point prevPosWs = parentSpace.TransformPoint(enterPosPs);
		U32F loopCount = 0;

		while ((iCurrentCorner >= 0) && (iInputExit < inputWaypointCount) && !pWorkingResultsPs->IsFull())
		{
			NAV_ASSERT(loopCount++ < 1000);

			const NavCorner& c = pCorners[iCurrentCorner];

			const Point inputLeg0Ws = prevPosWs;
			const Point inputLeg1Ps = inputPathPs.GetWaypoint(iInputExit);
			const Point inputLeg1Ws = parentSpace.TransformPoint(inputLeg1Ps);
			const Point inputLeg0Ls = c.m_e0.m_pNavMesh->WorldToLocal(inputLeg0Ws);
			const Point inputLeg1Ls = c.m_e0.m_pNavMesh->WorldToLocal(inputLeg1Ws);
			const Segment inputLegLs = Segment(inputLeg0Ls, inputLeg1Ls);
			const Vector inpuptDirLs = AsUnitVectorXz(inputLeg1Ls - inputLeg0Ls, kZero);
			const Vector inputNudgeLs = inpuptDirLs * 0.01f;
			const Segment nudgedInputLegLs = Segment(inputLeg0Ls - inputNudgeLs, inputLeg1Ls + inputNudgeLs);

			float thisProbeTT = kLargeFloat;
			const float linkAdj = 0.01f; // we extend our link segments inward a nudge because our input path might hug the wall so close it doesn't intersect

			const Point link0Pos = Lerp(c.m_e0.m_v0Ls, c.m_e0.m_v1Ls, c.m_e0midTT);
			const Vector link0Adj = linkAdj * c.m_e0.m_normalLs;
			const Segment link0Seg = Segment(link0Pos - link0Adj, link0Pos + (c.m_e0.m_normalLs * (pathRadius + linkAdj)));

			const Point link1Pos = Lerp(c.m_e1.m_v0Ls, c.m_e1.m_v1Ls, c.m_e1midTT);
			const Vector link1Adj = linkAdj * c.m_e1.m_normalLs;
			const Segment link1Seg = Segment(link1Pos - link1Adj, link1Pos + (c.m_e1.m_normalLs * (pathRadius + linkAdj)));

			const float thisCornerTT = IntersectLegWithCorner(inputLegLs, c, true, thisProbeTT);

			Scalar t0, t1;

			if (thisCornerTT >= 0.0f)
			{
				Point newVertsLs[10];
				U32F numNewVerts = GetCornerVertsForRange(c, currentCornerTT, thisCornerTT, newVertsLs, 10);

				const I32F exitLeg0 = Floor(thisCornerTT);
				const I32F exitLeg1 = Ceil(thisCornerTT);
				const float exitLegTT = Fmod(thisCornerTT);

				const Point v0 = c.m_portalVerts[exitLeg0];
				const Point v1 = c.m_portalVerts[exitLeg1];
				const Point exitPosLs = Lerp(v0, v1, exitLegTT);

				const Vector offset = SafeNormalize(exitPosLs - prevPosWs, kZero) * 0.005f;

				newVertsLs[numNewVerts++] = exitPosLs + offset;

				const NavCornerTestResult res = TryAddNewVertsToPath(pathContext,
																	 prevPosWs,
																	 newVertsLs,
																	 numNewVerts,
																	 iCurrentCorner,
																	 iPrevCorner,
																	 cornerTable,
																	 pCorners,
																	 numCorners,
																	 pWorkingResultsPs);

				iPrevCorner = iCurrentCorner;
				iCurrentCorner = res.m_iCorner;

				if (iCurrentCorner < 0)
				{
					break;
				}
				else
				{
					currentCornerTT = res.m_cornerTT;
					prevPosWs = res.m_pCorner->m_e0.m_pNavMesh->LocalToWorld(res.m_hitPointLs);
				}

				continue;
			}

			const NavCorner::Link* pLink0 = &c.m_link0;
			const NavCorner::Link* pLink1 = &c.m_link1;

			if (!pLink0->m_pCorner && c.m_overlap.m_pCorner && c.m_overlap.m_pCorner->m_link0.m_pCorner)
			{
				pLink0 = &c.m_overlap.m_pCorner->m_link0;
			}

			if (!pLink1->m_pCorner && c.m_overlap.m_pCorner && c.m_overlap.m_pCorner->m_link1.m_pCorner)
			{
				pLink1 = &c.m_overlap.m_pCorner->m_link1;
			}

			const bool canProgressLink0 = CanTraverseCornerLink(*pLink0, iPrevCorner);
			const bool canProgressLink1 = CanTraverseCornerLink(*pLink1, iPrevCorner);
			const float link0Dot = DotXz(nudgedInputLegLs.b - nudgedInputLegLs.a, RotateY90(link0Seg.b - link0Seg.a));
			const float link1Dot = DotXz(nudgedInputLegLs.b - nudgedInputLegLs.a, RotateYMinus90(link1Seg.b - link1Seg.a));

			bool movesOutThroughLink0 = link0Dot >= -NDI_FLT_EPSILON;
			bool movesOutThroughLink1 = link1Dot >= -NDI_FLT_EPSILON;

			bool overlapsLink0 = false;
			bool overlapsLink1 = false;

			//if (!movesOutThroughLink0)
			{
				const float dotP = Abs(DotXz(inpuptDirLs, c.m_e0.m_normalLs));
				if (dotP > 0.99f)
				{
					const float d0 = DistPointSegmentXz(inputLeg0Ls, link0Seg.a, link0Seg.b);
					const float d1 = DistPointSegmentXz(inputLeg1Ls, link0Seg.a, link0Seg.b);
					const float minD = Min(d0, d1);

					if (minD < 0.01f)
					{
						movesOutThroughLink0 = true;
						overlapsLink0 = true;
					}
				}
			}

			//if (!movesOutThroughLink1)
			{
				const float dotP = Abs(DotXz(inpuptDirLs, c.m_e1.m_normalLs));
				if (dotP > 0.99f)
				{
					const float d0 = DistPointSegmentXz(inputLeg0Ls, link1Seg.a, link1Seg.b);
					const float d1 = DistPointSegmentXz(inputLeg1Ls, link1Seg.a, link1Seg.b);
					const float minD = Min(d0, d1);

					if (minD < 0.01f)
					{
						movesOutThroughLink1 = true;
						overlapsLink1 = true;
					}
				}
			}

			if (canProgressLink0 && movesOutThroughLink0 && (overlapsLink0 || IntersectSegmentSegmentXz(nudgedInputLegLs, link0Seg, t0, t1)))
			{
				Point newVertsLs[8];
				const U32F numNewVerts = GetCornerVertsForRange(c, currentCornerTT, 0.5f, newVertsLs, 8);

				const NavCornerTestResult res = TryAddNewVertsToPath(pathContext,
																	 prevPosWs,
																	 newVertsLs,
																	 numNewVerts,
																	 iCurrentCorner,
																	 iPrevCorner,
																	 cornerTable,
																	 pCorners,
																	 numCorners,
																	 pWorkingResultsPs);
				iPrevCorner = iCurrentCorner;

				if (res.m_iCorner >= 0)
				{
					iCurrentCorner = res.m_iCorner;
					currentCornerTT = res.m_cornerTT;
					prevPosWs = res.m_pCorner->m_e0.m_pNavMesh->LocalToWorld(res.m_hitPointLs);
				}
				else
				{
					prevPosWs = Lerp(inputLeg0Ws, inputLeg1Ws, t0);
					iCurrentCorner = -1;

					if (pLink0->m_pCorner)
					{
						iCurrentCorner = pLink0->m_iCorner;
					}

					if (iCurrentCorner >= 0)
					{
						currentCornerTT = float(pCorners[iCurrentCorner].m_numPortalVerts) - 1.5f;
					}
				}

				continue;
			}

			if (canProgressLink1 && movesOutThroughLink1 && (overlapsLink1 || IntersectSegmentSegmentXz(nudgedInputLegLs, link1Seg, t0, t1)))
			{
				Point newVertsLs[8];
				const U32F numNewVerts = GetCornerVertsForRange(c,
																currentCornerTT,
																float(c.m_numPortalVerts) - 1.5f,
																newVertsLs,
																8);

				const NavCornerTestResult res = TryAddNewVertsToPath(pathContext,
																	 prevPosWs,
																	 newVertsLs,
																	 numNewVerts,
																	 iCurrentCorner,
																	 iPrevCorner,
																	 cornerTable,
																	 pCorners,
																	 numCorners,
																	 pWorkingResultsPs);

				iPrevCorner = iCurrentCorner;

				if (res.m_iCorner >= 0)
				{
					iCurrentCorner = res.m_iCorner;
					currentCornerTT = res.m_cornerTT;
					prevPosWs = res.m_pCorner->m_e0.m_pNavMesh->LocalToWorld(res.m_hitPointLs);
				}
				else
				{
					prevPosWs = Lerp(inputLeg0Ws, inputLeg1Ws, t0);
					iCurrentCorner = -1;
					currentCornerTT = 0.5f;

					if (pLink1->m_pCorner)
					{
						iCurrentCorner = pLink1->m_iCorner;
					}
				}

				continue;
			}

			if (NavPathNode::IsActionPackNode(inputPathPs.GetNodeType(iInputExit)))
			{
				// always emit our AP nodes so just stop here
				pWorkingResultsPs->AddWaypoint(inputPathPs, iInputExit);
				++iInputExit;
				break;
			}

			++iInputExit;
			prevPosWs = c.m_e0.m_pNavMesh->LocalToWorld(inputLeg1Ls);
		}

		if ((iInputExit >= inputWaypointCount) && (iCurrentCorner >= 0) && !pWorkingResultsPs->IsFull())
		{
			const Point lastPosPs = inputPathPs.GetEndWaypoint();
			const Point lastPosWs = parentSpace.TransformPoint(lastPosPs);

			const NavCorner& curCorner = pCorners[iCurrentCorner];

			const Point lastPosLs = curCorner.m_e0.m_pNavMesh->WorldToLocal(lastPosWs);

			const float bestExitTT = curCorner.GetClosestTTLs(lastPosLs);

			Point newVertsLs[8];
			const U32F numNewVerts = GetCornerVertsForRange(curCorner, currentCornerTT, bestExitTT, newVertsLs, 8);

			for (U32F iV = 0; iV < numNewVerts; ++iV)
			{
				const Point newVertWs = curCorner.m_e0.m_pNavMesh->LocalToWorld(newVertsLs[iV]);
				const Point newVertPs = parentSpace.UntransformPoint(newVertWs);
				pWorkingResultsPs->AddWaypoint(newVertPs, curCorner.m_e0.m_pNavPoly->GetNavManagerId(), NavPathNode::kNodeTypePoly);
			}

			pWorkingResultsPs->AddWaypoint(inputPathPs, inputPathPs.GetWaypointCount() - 1);
		}

		*pNewReaderOut = iInputExit;

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void PushPathAwayFromFeaturesPs(const PathFindContext& pathContext,
										   PathWaypointsEx* pPathPs,
										   const NavItemRegistrationTable& cornerTable,
										   const NavCorner* pCorners,
										   U32F numCorners)
	{
		PROFILE_AUTO(Navigation);

		const U32F numInputPoints = pPathPs ? pPathPs->GetWaypointCount() : 0;

		if ((numInputPoints < 2) || !pCorners || (0 == numCorners))
		{
			return;
		}

		const PathWaypointsEx inputPathPs = *pPathPs;

		if (false)
		{
			Locator ps = pathContext.m_parentSpace;
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorCyan;
			cs.m_groundLeg1 = kColorMagenta;
			ps.SetPos(ps.Pos() + Vector(0.0f, 0.2f, 0.0f));
			inputPathPs.DebugDraw(ps, true, 0.0f, cs);
		}

		pPathPs->Clear();
		pPathPs->AddWaypoint(inputPathPs, 0);

		I32F iReader = 1;
		I32F iPrevCorner = -1;

		while ((iReader < numInputPoints) && (!pPathPs->IsFull()))
		{
			I32F iNewReader = -1;

			const bool isGroundLeg = inputPathPs.IsGroundLeg(iReader - 1);

			if (isGroundLeg
				&& PushPathAwayFromCorners(pathContext,
										   inputPathPs,
										   iReader,
										   cornerTable,
										   pCorners,
										   numCorners,
										   pPathPs,
										   &iNewReader,
										   iPrevCorner))
			{
				iReader = iNewReader;
			}
			else
			{
				pPathPs->AddWaypoint(inputPathPs, iReader);
				++iReader;
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct GatherEdgesData
	{
		const NavItemRegistrationTable* m_pEdgeTable;
		NavItemBitArray* m_pFoundEdges;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void cbVisitGatherEdges(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData)
	{
		NAV_ASSERT(pPoly);

		const GatherEdgesData& data = *(const GatherEdgesData*)userData;

		NavItemRegistrationTable::ConstIterator itr = data.m_pEdgeTable->Find(pPoly->GetNavManagerId());

		if (itr != data.m_pEdgeTable->End())
		{
			NavItemBitArray::BitwiseOr(data.m_pFoundEdges, *data.m_pFoundEdges, itr->m_value);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const NavMesh* GatherEdgesInLinePs(const PathFindContext& pathContext,
											  const NavItemRegistrationTable& edgeTable,
											  const NavMesh* pNavMesh,
											  Point_arg startPosPs,
											  Point_arg endPosPs,
											  NavItemBitArray& foundEdges,
											  const NavMesh* pIgnoreReachedNavMesh = nullptr,
											  Point* pReachedPosPsOut = nullptr,
											  bool crossLinks = false)
	{
		GatherEdgesData data;
		data.m_pEdgeTable = &edgeTable;
		data.m_pFoundEdges = &foundEdges;

		NavMesh::ProbeParams params;
		params.m_start = startPosPs;
		params.m_move = endPosPs - startPosPs;
		params.m_pStartPoly = pNavMesh->FindContainingPolyPs(startPosPs, 2.0f, Nav::kStaticBlockageMaskNone);
		params.m_crossLinks = crossLinks;
		params.m_polyContainsPointTolerance = NavMeshClearanceProbe::kNudgeEpsilon;

		if (!params.m_pStartPoly && pathContext.m_pathRadius > NDI_FLT_EPSILON)
		{
			NAV_ASSERT(IsFinite(startPosPs));

			NavMesh::FindPointParams findParams;
			findParams.m_point = startPosPs;
			findParams.m_searchRadius = 2.0f * NavMeshClearanceProbe::kNudgeEpsilon;

			pNavMesh->FindNearestPointPs(&findParams);

			params.m_start = findParams.m_nearestPoint;
			params.m_move = endPosPs - findParams.m_nearestPoint;
			params.m_pStartPoly = findParams.m_pPoly;
		}

		pNavMesh->WalkPolysInLinePs(params, cbVisitGatherEdges, (uintptr_t)&data);

		const NavPoly* pReachedPoly = params.m_pReachedPoly;
		const NavMesh* pReachedMesh = pNavMesh;

		if (pReachedPoly && pReachedPoly->IsLink())
		{
			if (params.m_pStartPoly->IsLink() ||
				params.m_pStartPoly->IsPreLink() ||
				params.m_pStartPoly->GetLinkId() != pReachedPoly->GetLinkId())
			{
				const NavMesh* pLinkedMesh = nullptr;
				pReachedPoly->GetLinkedPoly(&pLinkedMesh);

				// Only ignore the reached mesh if we haven't moved
				if (pLinkedMesh == pIgnoreReachedNavMesh)
				{
					const Scalar distSq = DistXzSqr(startPosPs, params.m_endPoint);
					if (distSq >= NavMeshClearanceProbe::kNudgeEpsilon * NavMeshClearanceProbe::kNudgeEpsilon)
					{
						pIgnoreReachedNavMesh = nullptr;
					}
				}

				if (pLinkedMesh && pLinkedMesh != pIgnoreReachedNavMesh)
				{
					pReachedMesh = pLinkedMesh;
				}
			}
		}

		if (pReachedPosPsOut)
		{
			Point reachedPosPs = params.m_endPoint;
			if (pReachedPoly)
			{
				Point reachedPosLs = pNavMesh->ParentToLocal(reachedPosPs);
				Vector normLs;
				pReachedPoly->ProjectPointOntoSurfaceLs(&reachedPosLs, &normLs, reachedPosLs);

				reachedPosPs = pNavMesh->LocalToParent(reachedPosLs);
			}

			*pReachedPosPsOut = reachedPosPs;
		}

		return pReachedMesh;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	const U64 GetEdgeDistance(const NavEdge& startingEdge, U32F iDestEdge, bool travelDir, U64 maxDist)
	{
		U64 travelDist = 0;

		for (const NavEdge* pCurEdge = &startingEdge; pCurEdge->m_iEdge != iDestEdge; ++travelDist)
		{
			const NavEdge::Link& link = travelDir ? pCurEdge->m_link1 : pCurEdge->m_link0;

			if (travelDist > maxDist)
			{
				// early abort
				travelDist = -1;
				break;
			}

			if (!link.m_pEdge)
			{
				// not found
				travelDist = -1;
				break;
			}

			pCurEdge = link.m_pEdge;

			if (pCurEdge == &startingEdge)
			{
				// looped back to start
				travelDist = -1;
				break;
			}
		}

		return travelDist;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsConnected(const NavEdge* pEdges, U32 iStart, U32 iEnd, bool direction)
	{
		NavItemBitArray visited(false);

		if (iStart == iEnd)
		{
			return false;
		}

		while (iStart != -1 && !visited.IsBitSet(iStart))
		{
			if (iStart == iEnd)
			{
				return true;
			}

			visited.SetBit(iStart);

			const NavEdge& edge = pEdges[iStart];
			const NavEdge::Link& link = direction ? edge.m_link1 : edge.m_link0;

			iStart = link.m_iEdge;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void DetermineOutputTravelDir(NavEdgeContext& ctx, U32 iExitEdge, Point_arg exitPosPs)
	{
		// Ensure that the input path is always within the search radius of the edges we're going to output. This is
		// to catch cases were we have a loop and we are not able to decide the correct direction when we entered edge
		// space.

		if (!ctx.m_travelDirLooped)
		{
			return;
		}

		const float searchRadius = kSqrt2 * ctx.m_pPathContext->m_pathRadius + NavMeshClearanceProbe::kNudgeEpsilon;

		Segment readerSegPs(ctx.m_enterPosPs, ctx.m_enterPosPs);
		const NavEdge* pCurr = &ctx.m_pEdges[ctx.m_iEnterEdge];
		if (ctx.m_enteredLink == 1)
		{
			pCurr = pCurr->m_link1.m_pEdge;
		}

		// We always check the true direction and flip to false when we detected that the input path leaves edge space.
		ctx.m_travelDir = true;

		U32F iReader = ctx.m_iEnterReader;
		while (iReader <= ctx.m_iReader)
		{
			readerSegPs.a = readerSegPs.b;
			readerSegPs.b = iReader == ctx.m_iReader ? exitPosPs : ctx.m_pInputPathPs->GetWaypoint(iReader);

			while (pCurr && pCurr->m_iEdge != iExitEdge)
			{
				const Segment edgeSegPs(pCurr->m_projected0Ps, pCurr->m_projected1Ps);
				const Segment edgeSpacePs(pCurr->m_v1Ps, pCurr->m_projected1Ps);

				Scalar t0, t1;
				if (DistSegmentSegmentXz(edgeSpacePs, readerSegPs, t0, t1) <= NavMeshClearanceProbe::kNudgeEpsilon)
				{
					pCurr = pCurr->m_link1.m_pEdge;
					continue;
				}
				else if (DistSegmentSegmentXz(edgeSegPs, readerSegPs, t0, t1) <= searchRadius)
				{
					break;
				}

				ctx.m_travelDir = false;
				return;
			}

			iReader++;
		}

		if (!pCurr || pCurr->m_iEdge != iExitEdge || iReader <= ctx.m_iReader)
		{
			ctx.m_travelDir = false;
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool DetermineEdgeTravelDir(NavEdgeContext& ctx,
									   const NavEdge& edge,
									   Point_arg prevPosPs,
									   bool& looped)
	{
		const U32F numWaypoints = ctx.m_pInputPathPs->GetWaypointCount();
		const float searchRadius = ctx.m_pPathContext->m_pathRadius + 0.01f;

		Point nextPosPs = prevPosPs;

		const Point edge0Ps = edge.m_v0Ps;
		const Point edge1Ps = edge.m_v1Ps;
		const Vector edgeNormPs = edge.m_normalPs;

		U32F iReader = ctx.m_iReader;

		while (true)
		{
			nextPosPs = ctx.m_pInputPathPs->GetWaypoint(iReader);

			if (!ctx.m_pInputPathPs->IsGroundLeg(iReader))
			{
				break;
			}

			if (DotXz(nextPosPs - edge0Ps, edgeNormPs) < -NDI_FLT_EPSILON)
			{
				// if we move behind an edge consider that good enough, even if we're within searchRadius
				// because we might be pathing around a very thin wall
				break;
			}

			const float travelDist = DistXz(nextPosPs, prevPosPs);
			if (travelDist > searchRadius)
			{
				break;
			}

			if (iReader < numWaypoints - 1)
			{
				++iReader;
			}
			else
			{
				break;
			}
		}

		const NavMesh* pReaderMesh = NavMeshHandle(ctx.m_pInputPathPs->GetNavId(iReader)).ToNavMesh();

		const float kNudgeGather = 0.0001f;
		const Vector gatherProbeDirPs = AsUnitVectorXz(prevPosPs - nextPosPs, kZero);

		NavItemBitArray foundEdges(false);
		// go backwards in case we cross a link, we don't want to start off nav mesh
		GatherEdgesInLinePs(*ctx.m_pPathContext,
							*ctx.m_pEdgeTable,
							pReaderMesh,
							nextPosPs + kNudgeGather * gatherProbeDirPs,
							prevPosPs,
							foundEdges,
							nullptr,
							nullptr,
							true);

		I32F iBestLeftEdge = -1;
		I32F iBestRightEdge = -1;
		float bestLeftEdgeDist = kLargeFloat;
		float bestRightEdgeDist = kLargeFloat;

		const Segment moveSegPs = Segment(prevPosPs, nextPosPs);

		const NavEdge* pLeftEdge = edge.m_link0.m_pEdge;
		const NavEdge* pRightEdge = edge.m_link1.m_pEdge;

		// kind of a like a hacky 1-d A* to simultaenously expand in both directions
		// a potential problem case might be if we reach an edge in fewer steps but takes a
		// longer distance (because of the edge sizes involved) which might mean we need
		// to do a per-side distance accumulation

		U32F iterCount = 0;

		NavItemBitArray visitedLeft(false);
		NavItemBitArray visitedRight(false);

		NavItemBitArray foundEdgesLeft(foundEdges);
		NavItemBitArray foundEdgesRight(foundEdges);

		visitedLeft.SetBit(edge.m_iEdge);
		visitedRight.SetBit(edge.m_iEdge);

		while (pLeftEdge || pRightEdge)
		{
			if (pLeftEdge && !visitedLeft.IsBitSet(pLeftEdge->m_iEdge) && !foundEdgesLeft.AreAllBitsClear())
			{
				visitedLeft.SetBit(pLeftEdge->m_iEdge);

				if (foundEdgesLeft.IsBitSet(pLeftEdge->m_iEdge))
				{
					Scalar t0, t1;

					const Segment edgeSegPs = Segment(pLeftEdge->m_v0Ps, pLeftEdge->m_v1Ps);
					const float edgeDist = DistSegmentSegmentXz(moveSegPs, edgeSegPs, t0, t1);

					if (edgeDist < bestLeftEdgeDist)
					{
						bestLeftEdgeDist = edgeDist;
						iBestLeftEdge = pLeftEdge->m_iEdge;
					}

					foundEdgesLeft.ClearBit(pLeftEdge->m_iEdge);
				}

				pLeftEdge = pLeftEdge->m_link0.m_pEdge;
			}
			else
			{
				pLeftEdge = nullptr;
			}

			if (pRightEdge && !visitedRight.IsBitSet(pRightEdge->m_iEdge) && !foundEdgesRight.AreAllBitsClear())
			{
				visitedRight.SetBit(pRightEdge->m_iEdge);

				if (foundEdgesRight.IsBitSet(pRightEdge->m_iEdge))
				{
					Scalar t0, t1;

					const Segment edgeSegPs = Segment(pRightEdge->m_v0Ps, pRightEdge->m_v1Ps);
					const float edgeDist = DistSegmentSegmentXz(moveSegPs, edgeSegPs, t0, t1);

					if (edgeDist < bestRightEdgeDist)
					{
						bestRightEdgeDist = edgeDist;
						iBestRightEdge = pRightEdge->m_iEdge;
					}

					foundEdgesRight.ClearBit(pRightEdge->m_iEdge);
				}

				pRightEdge = pRightEdge->m_link1.m_pEdge;
			}
			else
			{
				pRightEdge = nullptr;
			}

			++iterCount;
		}

#ifdef JBELLOMY
		if (iterCount > kMaxNavItems)
		{
			DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
			g_hackPrintParams = true;
			g_ndConfig.m_pDMenuMgr->SetProgPause(true);
			g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
			ASSERT(false);
			MsgConErr("RADIAL PATH BUILD FAIL (DetermineEdgeTravelDir)\n");
		}
#endif

		bool travelDir = bestRightEdgeDist < bestLeftEdgeDist;

		// Last ditch fallback. If we didn't visit any edges then look at the direction of the probe edge with respect to the entry edge.
		if (bestLeftEdgeDist == kLargeFloat && bestRightEdgeDist == kLargeFloat)
		{
			Vector probeDir = moveSegPs.GetVec();
			Vector edgeDir = edge.m_projected1Ps - edge.m_projected0Ps;

			travelDir = DotXz(probeDir, edgeDir) >= 0.0f;
		}

		// Detect if we're dealing with a loop that we need to resolve once we exit edge space.
		NavItemBitArray hasLoop;
		NavItemBitArray::BitwiseAnd(&hasLoop, visitedLeft, visitedRight);
		hasLoop.ClearBit(edge.m_iEdge);

		looped = !hasLoop.AreAllBitsClear();

		return travelDir;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddIntermediateEdgeVerts(NavEdgeContext& ctx, I32F iStartEdge, I32F iEndEdge)
	{
		const NavEdge* pEdges = ctx.m_pEdges;
		const U32F numEdges = ctx.m_numEdges;

		const bool travelDir = ctx.m_travelDir;
		const I32F enteredLink = ctx.m_enteredLink;
		PathWaypointsEx* pPathPsOut = ctx.m_pPathPsOut;

		const I32F iFirstLink = ctx.m_travelDir ? 0 : 1;
		const I32F iLastLink = ctx.m_travelDir ? 1 : 0;
		const bool enteredFirstLink = ctx.m_enteredLink == iFirstLink;
		const bool enteredLastLink = ctx.m_enteredLink == iLastLink;

		for (I32F iEdge = iStartEdge; iEdge != iEndEdge;)
		{
			const NavEdge& e = pEdges[iEdge];
			const NavEdge::Link& link = travelDir ? e.m_link1 : e.m_link0;
			const NavEdge::Link& rLink = travelDir ? e.m_link0 : e.m_link1;

			NAV_ASSERT(link.m_pEdge);
			if (!link.m_pEdge)
			{
				break;
			}

			const NavManagerId mgrId = e.m_pNavPoly->GetNavManagerId();

			if (e.m_shadowed || link.m_pEdge->m_shadowed)
			{
				// ignore
			}
			else if (link.m_interior)
			{
				const Point posPs = link.m_vertsPs[0];

				pPathPsOut->AddWaypoint(posPs, mgrId, NavPathNode::kNodeTypePoly);

				if (false)
				{
					const Point posWs = e.m_pNavMesh->ParentToWorld(posPs);
					g_prim.Draw(DebugCross(posWs, 0.05f, kColorGreen, kPrimEnableHiddenLineAlpha));
				}
			}
			else if (enteredLastLink && (iEdge == iStartEdge))
			{
				const I32F i1 = travelDir ? 1 : 0;
				const Point pos1Ps = link.m_vertsPs[i1];

				pPathPsOut->AddWaypoint(pos1Ps, mgrId, NavPathNode::kNodeTypePoly);

				if (false)
				{
					const Point pos1Ws = e.m_pNavMesh->ParentToWorld(pos1Ps);
					g_prim.Draw(DebugCross(pos1Ws, 0.05f, kColorPink, kPrimEnableHiddenLineAlpha));
				}
			}
			else
			{
				if ((enteredFirstLink && (iEdge == iStartEdge)) || (rLink.m_linkIntersection != -1 && (iEdge != iStartEdge)))
				{
					const I32F i1 = travelDir ? 1 : 0;
					const Point prevPosPs = rLink.m_vertsPs[i1];
					pPathPsOut->AddWaypoint(prevPosPs, mgrId, NavPathNode::kNodeTypePoly);

					if (false)
					{
						const Point prevPosWs = e.m_pNavMesh->ParentToWorld(prevPosPs);
						g_prim.Draw(DebugCross(prevPosWs, 0.05f, kColorPink, kPrimEnableHiddenLineAlpha));
					}
				}

				const I32F i0 = travelDir ? 0 : 1;
				const I32F i1 = travelDir ? 1 : 0;

				const Point pos0Ps = link.m_vertsPs[i0];
				const Point pos1Ps = link.m_vertsPs[i1];

				pPathPsOut->AddWaypoint(pos0Ps, mgrId, NavPathNode::kNodeTypePoly);
				pPathPsOut->AddWaypoint(pos1Ps, mgrId, NavPathNode::kNodeTypePoly);

				if (false)
				{
					const Point pos0Ws = e.m_pNavMesh->ParentToWorld(pos0Ps);
					const Point pos1Ws = e.m_pNavMesh->ParentToWorld(pos1Ps);
					g_prim.Draw(DebugCross(pos0Ws, 0.05f, kColorCyan, kPrimEnableHiddenLineAlpha));
					g_prim.Draw(DebugCross(pos1Ws, 0.05f, kColorMagenta, kPrimEnableHiddenLineAlpha));
				}
			}

			iEdge = link.m_iEdge;
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddVertsForClosestEdgeRange(NavEdgeContext& ctx)
	{
		if ((ctx.m_iEnterEdge < 0) || !ctx.m_pPathPsOut)
			return;

		const Point exitPosPs = ctx.m_pInputPathPs->GetWaypoint(ctx.m_iReader);
		const NavManagerId exitNavId = ctx.m_pInputPathPs->GetNavId(ctx.m_iReader);
		const U32F exitNavMeshIndex = exitNavId.m_navMeshIndex;

		const I32F iEnterEdge = ctx.m_iEnterEdge;
		I32F iExitEdge = iEnterEdge;

		float iBestEdge = iEnterEdge;
		float exitTT = -1.0f;
		float bestDist = kLargeFloat;

		NavItemBitArray visitedEdges(false);

		do
		{
			const NavEdge& e = ctx.m_pEdges[iExitEdge];

			const U32F navMeshIndex = e.m_pNavMesh->GetManagerId().m_navMeshIndex;
			if ((navMeshIndex != exitNavMeshIndex) || e.m_shadowed)
			{
				// not on our exit nav mesh, so just keep walking
			}
			else
			{
				Scalar tt;
				const float edgeDist = DistPointSegmentXz(exitPosPs, e.m_v0Ps, e.m_v1Ps, nullptr, &tt);

				if (edgeDist < bestDist)
				{
					iBestEdge = iExitEdge;
					bestDist  = edgeDist;
					exitTT	  = tt;
				}
			}

			const NavEdge::Link& link = ctx.m_travelDir ? ctx.m_pEdges[iExitEdge].m_link1 : ctx.m_pEdges[iExitEdge].m_link0;
			if (link.m_pEdge && !visitedEdges.IsBitSet(link.m_iEdge))
			{
				visitedEdges.SetBit(iExitEdge);
				iExitEdge = link.m_iEdge;
			}
			else
			{
				break;
			}

		} while (iExitEdge != iEnterEdge);

		AddIntermediateEdgeVerts(ctx, iEnterEdge, iBestEdge);

		ctx.m_pPathPsOut->AddWaypoint(*ctx.m_pInputPathPs, ctx.m_iReader);

		++ctx.m_iReader;

		ctx.m_iEnterEdge = -1;
		ctx.m_travelDir	 = false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool DoesEnterEdge(const Segment& probeSegPs, const NavEdge& e, Point& intersectPosPs, I32F& intersectLink, F32 &t)
	{
		t = kLargeFloat;
		intersectLink = -1;

		if (e.m_link0.m_pEdge && !e.m_link0.m_interior && !e.m_link0.m_pEdge->m_shadowed)
		{
			Segment seg(e.m_link0.m_vertsPs[0], e.m_link0.m_vertsPs[1]);

			Scalar t0, t1;
			if (IntersectSegmentSegmentXz(probeSegPs, seg, t0, t1))
			{
				if (t0 < t)
				{
					t = t0;
					intersectLink = 0;
					intersectPosPs = Lerp(seg, t1);
				}
			}
		}

		if (e.m_link1.m_pEdge && !e.m_link1.m_interior && !e.m_link1.m_pEdge->m_shadowed)
		{
			Segment seg(e.m_link1.m_vertsPs[0], e.m_link1.m_vertsPs[1]);

			Scalar t0, t1;
			if (IntersectSegmentSegmentXz(probeSegPs, seg, t0, t1))
			{
				if (t0 < t)
				{
					t = t0;
					intersectLink = 1;
					intersectPosPs = Lerp(seg, t1);
				}
			}
		}

		{
			Segment seg(e.m_projected0Ps, e.m_projected1Ps);

			Scalar t0, t1;
			if (IntersectSegmentSegmentXz(probeSegPs, seg, t0, t1))
			{
				if (t0 < t)
				{
					t = t0;
					intersectLink = -1;
					intersectPosPs = Lerp(seg, t1);
				}
			}
		}

		return t != kLargeFloat;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool TryEnterEdgeSpace(NavEdgeContext& ctx)
	{
		if (ctx.m_iEnterEdge >= 0)
		{
			return false;
		}

		if (ctx.m_iReader >= ctx.m_pInputPathPs->GetWaypointCount())
		{
			return false;
		}

		const Point prevPosPs = AllComponentsEqual(ctx.m_enterProbeStartPs, kInvalidPoint) ? ctx.m_pPathPsOut->GetEndWaypoint() : ctx.m_enterProbeStartPs;
		Point nextPosPs = ctx.m_pInputPathPs->GetWaypoint(ctx.m_iReader);

		Point gatherEndPs = kInvalidPoint;
		NavItemBitArray foundEdges(false);
		const NavMesh* pReachedMesh = GatherEdgesInLinePs(*ctx.m_pPathContext,
														  *ctx.m_pEdgeTable,
														  ctx.m_pCurNavMesh,
														  prevPosPs,
														  nextPosPs,
														  foundEdges,
														  ctx.m_pIgnoreReachedNavMesh,
														  &gatherEndPs);

		// Clip our probe vector to stay within the current mesh
		if (pReachedMesh != ctx.m_pCurNavMesh)
		{
			nextPosPs = gatherEndPs;
		}

		const Vector probeVecPs = VectorXz(nextPosPs - prevPosPs);
		const Segment probeSegPs = Segment(prevPosPs, nextPosPs);

		const float probeRadius = ctx.m_pPathContext->m_pathRadius - NavMeshClearanceProbe::kNudgeEpsilon;

		float bestTT = kLargeFloat;
		I32F iBestEdge = -1;
		I32F enteredLink = -1;

		for (const U64 iEdge : foundEdges)
		{
			const NavEdge& e = ctx.m_pEdges[iEdge];

			if (e.m_shadowed || e.m_pNavMesh != ctx.m_pCurNavMesh)
			{
				continue;
			}

			const float dotP = DotXz(probeVecPs, e.m_normalPs);
			if (e.m_link0.m_linkIntersection == -1 && e.m_link1.m_linkIntersection == -1 && dotP > 0.0f)
			{
				continue;
			}

			float tt;
			if (!IntersectSegmentStadiumXz(probeSegPs, Segment(e.m_v0Ps, e.m_v1Ps), probeRadius, tt))
			{
				continue;
			}

			if ((ctx.m_iPrevEdge == iEdge) && (tt < bestTT))
			{
				// if we're revisiting an edge we just left, make sure we've moved a sufficient distance
				// and we're not just hitting it with the same probe segment we left with
				const Point probePosPs = probeSegPs.Interpolate(tt);
				const float moveDist = DistXz(prevPosPs, probePosPs);
				if (moveDist < 0.05f)
				{
					continue;
				}
			}

			if (tt < bestTT)
			{
				bestTT = tt;
				iBestEdge = iEdge;
			}
			else if (tt == bestTT && tt > 0.0f && iBestEdge != -1)
			{
				const NavEdge& b = ctx.m_pEdges[iBestEdge];

				I32F intersectLink;
				Point intersectPosPs;

				F32 tCurr, tBest;
				bool intersectCurr = DoesEnterEdge(probeSegPs, e, intersectPosPs, intersectLink, tCurr);
				bool intersectBest = DoesEnterEdge(probeSegPs, b, intersectPosPs, intersectLink, tBest);

				if ((intersectCurr && !intersectBest) || (intersectCurr && intersectBest && tCurr < tBest))
				{
					bestTT = tt;
					iBestEdge = iEdge;
				}
			}
		}

		if (iBestEdge < 0)
		{
			if (ctx.m_pCurNavMesh != pReachedMesh)
			{
				// don't advance ctx.m_iReader if we switch meshes
				ctx.m_pIgnoreReachedNavMesh = ctx.m_pCurNavMesh;
				ctx.m_pCurNavMesh = pReachedMesh;

				// Maintain the clipped probe segment as we switch to the next mesh
				Point enterProbeStartPsLs = ctx.m_pCurNavMesh->ParentToLocal(nextPosPs);
				ctx.m_enterProbeStartPs = pReachedMesh->LocalToParent(enterProbeStartPsLs);
				ctx.m_iPrevEnterReader = ctx.m_iReader;
				return true;
			}
			else
			{
				if (ctx.m_iPrevEnterReader < ctx.m_iReader)
				{
					ctx.m_enterProbeStartPs = ctx.m_pInputPathPs->GetWaypoint(ctx.m_iReader);
				}
				return false;
			}
		}

		const NavEdge& bestEdge = ctx.m_pEdges[iBestEdge];
		Point newPathPosPs = kInvalidPoint;
		F32 tTemp;

		// Locate where we enter the edge
		if (!DoesEnterEdge(probeSegPs, bestEdge, newPathPosPs, enteredLink, tTemp))
		{
			const Point enterPosPs = Lerp(prevPosPs, nextPosPs, bestTT);

			Scalar projTT;
			DistPointSegmentXz(enterPosPs, bestEdge.m_projected0Ps, bestEdge.m_projected1Ps, &newPathPosPs, &projTT);
			enteredLink = -1;

			if (bestEdge.m_link0.m_pEdge || bestEdge.m_link1.m_pEdge)
			{
				if (projTT < NDI_FLT_EPSILON)
				{
					if (bestEdge.m_link0.m_pEdge && !bestEdge.m_link0.m_interior && !bestEdge.m_link0.m_pEdge->m_shadowed)
					{
						const Point corner0Ps = bestEdge.m_pNavMesh->LocalToParent(bestEdge.m_link0.m_vertsLs[0]);
						const Point corner1Ps = bestEdge.m_pNavMesh->LocalToParent(bestEdge.m_link0.m_vertsLs[1]);

						DistPointSegmentXz(enterPosPs, corner0Ps, corner1Ps, &newPathPosPs);
						enteredLink = 0;
					}
				}
				else if (projTT + NDI_FLT_EPSILON >= 1.0f)
				{
					if (bestEdge.m_link1.m_pEdge && !bestEdge.m_link1.m_interior && !bestEdge.m_link1.m_pEdge->m_shadowed)
					{
						const Point corner0Ps = bestEdge.m_pNavMesh->LocalToParent(bestEdge.m_link1.m_vertsLs[0]);
						const Point corner1Ps = bestEdge.m_pNavMesh->LocalToParent(bestEdge.m_link1.m_vertsLs[1]);

						DistPointSegmentXz(enterPosPs, corner0Ps, corner1Ps, &newPathPosPs);
						enteredLink = 1;
					}
				}
			}
		}

		NAV_ASSERT(IsReasonable(newPathPosPs));

		ctx.m_pPathPsOut->AddWaypoint(newPathPosPs, bestEdge.m_pNavPoly->GetNavManagerId(), NavPathNode::kNodeTypePoly);

		ctx.m_iEnterEdge			= iBestEdge;
		ctx.m_iEnterReader			= ctx.m_iReader;
		ctx.m_enterPosPs			= newPathPosPs;
		ctx.m_pIgnoreReachedNavMesh	= nullptr;
		ctx.m_pCurNavMesh			= bestEdge.m_pNavMesh;
		ctx.m_enterProbeStartPs		= kInvalidPoint;
		ctx.m_exitProbeStartPs		= newPathPosPs;
		ctx.m_enteredLink			= enteredLink;
		ctx.m_travelDir				= DetermineEdgeTravelDir(ctx, bestEdge, newPathPosPs, ctx.m_travelDirLooped);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool TryExitEdgeSpace(NavEdgeContext& ctx)
	{
		if (ctx.m_iEnterEdge < 0)
		{
			return false;
		}

		const float kSqrt2Minus1 = 0.4142135624f;

		const Point prevReaderPs = ctx.m_pInputPathPs->GetWaypoint(ctx.m_iReader - 1);
		const Point probeEndPs	 = ctx.m_pInputPathPs->GetWaypoint(ctx.m_iReader);
		const Point probeStartPs = ctx.m_exitProbeStartPs;

		const Vector probeVecPs = AsUnitVectorXz(probeEndPs - probeStartPs, kZero);
		const Segment probeSegPs = Segment(probeStartPs, probeEndPs + ctx.m_pPathContext->m_pathRadius * kSqrt2Minus1 * probeVecPs);

		const NavEdge& enterEdge = ctx.m_pEdges[ctx.m_iEnterEdge];
		const NavEdge* pCurEdge	 = &enterEdge;
		const NavEdge* pExitEdge = nullptr;

		float exitT = kLargeFloat;
		float exitSegT = kLargeFloat;
		bool exitThroughLink = false;
		Segment exitSegPs;

		const float kNudgeGather = 0.0001f;

		Point gatherEndPs = kInvalidPoint;
		NavItemBitArray validEdges(false);
		const NavMesh* pReachedMesh = GatherEdgesInLinePs(*ctx.m_pPathContext,
														  *ctx.m_pEdgeTable,
														  ctx.m_pCurNavMesh,
														  probeStartPs + kNudgeGather * probeVecPs,
														  probeEndPs,
														  validEdges,
														  ctx.m_pIgnoreReachedNavMesh,
														  &gatherEndPs);

		NavItemBitArray visited(false);
		do
		{
			if (visited.IsBitSet(pCurEdge->m_iEdge))
			{
				break;
			}

			visited.SetBit(pCurEdge->m_iEdge);

			const NavEdge::Link& link = ctx.m_travelDir ? pCurEdge->m_link1 : pCurEdge->m_link0;

			if (validEdges.IsBitSet(pCurEdge->m_iEdge) && !pCurEdge->m_shadowed)
			{
				const bool movingAway = DotXz(probeVecPs, pCurEdge->m_normalPs) > 0.0f;
				if (movingAway)
				{
					const Segment edgeSegPs = Segment(pCurEdge->m_projected0Ps, pCurEdge->m_projected1Ps);

					Scalar t0, t1;
					if (IntersectSegmentSegmentXz(probeSegPs, edgeSegPs, t0, t1))
					{
						if (t0 < exitT)
						{
							pExitEdge = pCurEdge;
							exitT = t0;
							exitSegT = t1;
							exitThroughLink = false;
							exitSegPs = edgeSegPs;
						}
					}
				}

				if (link.m_pEdge && !link.m_pEdge->m_shadowed && !link.m_interior)
				{
					const Vector vertNormalPs = pCurEdge->m_pNavMesh->LocalToParent(link.m_vertNormalLs);
					const bool movingAwayFromLink = DotXz(probeVecPs, vertNormalPs) > 0.0f;
					if (movingAwayFromLink || (pCurEdge->m_iEdge != ctx.m_iEnterEdge && link.m_iEdge != ctx.m_iEnterEdge))
					{
						const Segment linkSegPs = Segment(link.m_vertsPs[0], link.m_vertsPs[1]);

						Scalar t0, t1;
						if (IntersectSegmentSegmentXz(probeSegPs, linkSegPs, t0, t1))
						{
							if (t0 < exitT)
							{
								pExitEdge = pCurEdge;
								exitT = t0;
								exitSegT = t1;
								exitThroughLink = true;
								exitSegPs = linkSegPs;
							}
						}
					}
				}

				validEdges.ClearBit(pCurEdge->m_iEdge);

				if (validEdges.AreAllBitsClear())
				{
					break;
				}
			}

			pCurEdge = link.m_pEdge;

		} while (pCurEdge && (pCurEdge->m_iEdge != ctx.m_iEnterEdge));

		if (!pExitEdge)
		{
			if (pReachedMesh != ctx.m_pCurNavMesh)
			{
				// don't advance ctx.m_iReader if we switch meshes,
				// but move our probe start so that the next gather
				// starts on the new mesh
				ctx.m_pIgnoreReachedNavMesh = ctx.m_pCurNavMesh;
				ctx.m_pCurNavMesh = pReachedMesh;
				ctx.m_exitProbeStartPs = gatherEndPs;
				return true;
			}
			else
			{
				return false;
			}
		}

		const Point exitPosPs = Lerp(exitSegPs, exitSegT);

		DetermineOutputTravelDir(ctx, pExitEdge->m_iEdge, exitPosPs);
		AddIntermediateEdgeVerts(ctx, ctx.m_iEnterEdge, pExitEdge->m_iEdge);

		const NavEdge::Link& rLink = ctx.m_travelDir ? pExitEdge->m_link0 : pExitEdge->m_link1;

		if (ctx.m_enteredLink != -1 && ctx.m_iEnterEdge == pExitEdge->m_iEdge)
		{
			const NavEdge::Link& link = ctx.m_enteredLink == 0 ? pExitEdge->m_link0 : pExitEdge->m_link1;
			if (link.m_linkIntersection != -1)
			{
				const I32F i1 = ctx.m_enteredLink == 0 ? 1 : 0;
				const Point posPs = link.m_vertsPs[i1];
				ctx.m_pPathPsOut->AddWaypoint(posPs,
											  pExitEdge->m_pNavPoly->GetNavManagerId(),
											  NavPathNode::kNodeTypePoly);
			}
		}

		if (exitThroughLink)
		{
			if (ctx.m_iEnterEdge != pExitEdge->m_iEdge && rLink.m_linkIntersection != -1)
			{
				const I32F i1 = ctx.m_travelDir ? 1 : 0;
				const Point posPs = rLink.m_vertsPs[i1];
				ctx.m_pPathPsOut->AddWaypoint(posPs,
											  pExitEdge->m_pNavPoly->GetNavManagerId(),
											  NavPathNode::kNodeTypePoly);
			}

			ctx.m_pPathPsOut->AddWaypoint(ctx.m_travelDir ? exitSegPs.a : exitSegPs.b,
										  pExitEdge->m_pNavPoly->GetNavManagerId(),
										  NavPathNode::kNodeTypePoly);
		}
		else if (rLink.m_linkIntersection != -1 && ctx.m_enteredLink == -1)
		{
			const I32F i1 = ctx.m_travelDir ? 1 : 0;
			const Point posPs = rLink.m_vertsPs[i1];
			ctx.m_pPathPsOut->AddWaypoint(posPs,
										  pExitEdge->m_pNavPoly->GetNavManagerId(),
										  NavPathNode::kNodeTypePoly);
		}

		const Point newPathEndPs = ctx.m_pPathPsOut->GetEndWaypoint();
		const float distToNewPosPs = Dist(exitPosPs, newPathEndPs);
		if (distToNewPosPs > NavMeshClearanceProbe::kNudgeEpsilon)
		{
			ctx.m_pPathPsOut->AddWaypoint(exitPosPs,
										  pExitEdge->m_pNavPoly->GetNavManagerId(),
										  NavPathNode::kNodeTypePoly);
		}

		ctx.m_iPrevEdge = ctx.m_iEnterEdge;
		ctx.m_iEnterEdge = -1;
		ctx.m_pIgnoreReachedNavMesh = nullptr;
		ctx.m_pCurNavMesh = pExitEdge->m_pNavMesh;

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void PushPathAwayFromEdges(const PathFindContext& pathContext,
									  const BuildPathParams& buildParams,
									  PathWaypointsEx* pPathPs,
									  const NavItemRegistrationTable& edgeTable,
									  const NavEdge* pEdges,
									  U32F numEdges)
	{
		const U32F numInputPoints = pPathPs ? pPathPs->GetWaypointCount() : 0;

		if ((numInputPoints < 2) || !pEdges || (0 == numEdges))
		{
			return;
		}

		const PathWaypointsEx inputPathPs = *pPathPs;

		if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawResults && false))
		{
			Locator ps = pathContext.m_parentSpace;
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorCyan;
			cs.m_groundLeg1 = kColorMagenta;
			ps.SetPos(ps.Pos() + Vector(0.0f, 0.5f, 0.0f));
			inputPathPs.DebugDraw(ps, true, 0.0f, cs, buildParams.m_debugDrawTime);
		}

		pPathPs->Clear();
		pPathPs->AddWaypoint(inputPathPs, 0);

		bool usingFallbackCtx = false;
		NavEdgeContext fallbackCtx(pathContext, inputPathPs, edgeTable);

		NavEdgeContext ctx(pathContext, inputPathPs, edgeTable);
		ctx.m_pEdges		= pEdges;
		ctx.m_numEdges		= numEdges;
		ctx.m_iReader		= 1;
		ctx.m_pPathPsOut	= pPathPs;
		ctx.m_pCurNavMesh	= NavMeshHandle(inputPathPs.GetNavId(0)).ToNavMesh();

		const Point startPosPs = inputPathPs.GetWaypoint(0);
		ctx.m_exitProbeStartPs = startPosPs;

		U32F iterCount = 0;
		U32F prevReader = -1;

		while ((ctx.m_iReader < numInputPoints) && (!pPathPs->IsFull()))
		{
#ifdef JBELLOMY
			if (g_hackPrintParams)
			{
				pPathPs->Clear();
				return;
			}
#endif

			if (ctx.m_iReader != prevReader)
			{
				iterCount = 0;
				prevReader = ctx.m_iReader;
				ctx.m_pIgnoreReachedNavMesh = nullptr;
			}
			else
			{
				++iterCount;

				if (iterCount > 1000)
				{
					if (FALSE_IN_FINAL_BUILD(true))
					{
						DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
						g_hackPrintParams = true;
						g_ndConfig.m_pDMenuMgr->SetProgPause(true);
						g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
						g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
						g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
						MsgConErr("RADIAL PATH BUILD FAIL (Main Inf. Loop)\n");
					}
					return;
				}
			}

			const bool isGroundLeg = inputPathPs.IsGroundLeg(ctx.m_iReader - 1);

			if (isGroundLeg)
			{
				if (TryExitEdgeSpace(ctx))
				{
					// inside radial (illegal) space, find where we exit

					// Disable the fallback context if we've found an exit
					if (ctx.m_iEnterEdge == -1)
					{
						fallbackCtx.m_iEnterEdge = -1;
						usingFallbackCtx = false;
					}
				}
				else if (TryEnterEdgeSpace(ctx))
				{
					// outside radial space, find any potential entry points

					// Save the current state of the context so we can fallback to using it in case we chose the wrong travel
					// direction and cannot find an exit.
					fallbackCtx = ctx;
				}
				else if ((ctx.m_iEnterEdge < 0) && (ctx.m_iReader < numInputPoints))
				{
					// outside, with no entry points found, so just take it straight up
					ctx.m_pPathPsOut->AddWaypoint(inputPathPs, ctx.m_iReader);
					ctx.m_exitProbeStartPs = inputPathPs.GetWaypoint(ctx.m_iReader);
					ctx.m_iReader++;
				}
				else
				{
					ctx.m_exitProbeStartPs = inputPathPs.GetWaypoint(ctx.m_iReader);
					ctx.m_iReader++;

					if (ctx.m_iReader == numInputPoints && fallbackCtx.m_iEnterEdge != -1)
					{
						if (usingFallbackCtx)
						{
							// We've failed to find and exit again, so just revert to the state before using the fallback context
							ctx = fallbackCtx;
							usingFallbackCtx = false;
						}
						else if (!ctx.m_pPathPsOut->IsFull())
						{
							// We've failed to find and exit so try winding back to the point where we entered edge space an try
							// going the other direction
							Swap(ctx, fallbackCtx);
							ctx.m_travelDir = !ctx.m_travelDir;
							usingFallbackCtx = true;
							prevReader = -1;
						}
					}
				}
			}
			else
			{
				if (ctx.m_iEnterEdge >= 0)
				{
					ctx.m_iReader--;
					AddVertsForClosestEdgeRange(ctx);
				}

				pPathPs->AddWaypoint(inputPathPs, ctx.m_iReader);

				const NavManagerId readerNavId = inputPathPs.GetNavId(ctx.m_iReader);
				if (readerNavId.m_navMeshIndex != ctx.m_pCurNavMesh->GetManagerId().m_navMeshIndex)
				{
					ctx.m_pIgnoreReachedNavMesh = nullptr;
					ctx.m_pCurNavMesh = NavMeshHandle(readerNavId).ToNavMesh();
				}

				ctx.m_iEnterEdge = -1;
				ctx.m_exitProbeStartPs = inputPathPs.GetWaypoint(ctx.m_iReader);
				ctx.m_enterProbeStartPs = kInvalidPoint;
				ctx.m_iReader++;

				fallbackCtx.m_iEnterEdge = -1;
				usingFallbackCtx = false;
			}
		}

		ctx.m_iReader = inputPathPs.GetWaypointCount() - 1;

		AddVertsForClosestEdgeRange(ctx);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const NavMesh* GetPathEdgeNavMeshForLoopRemoval(const PathWaypointsEx& path, U32 i)
	{
		if (!path.IsGroundLeg(i))
		{
			return nullptr;
		}

		NavMeshHandle handle(path.GetNavId(i));
		return handle.ToNavMesh();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const NavMesh* GetPathEdgeNavMeshForLoopRemoval(const PathWaypointsEx& path0, U32 i0,
														   const PathWaypointsEx& path1, U32 i1)
	{
		const NavMesh* pMesh0 = GetPathEdgeNavMeshForLoopRemoval(path0, i0);
		const NavMesh* pMesh1 = GetPathEdgeNavMeshForLoopRemoval(path1, i1);
		return pMesh0 == pMesh1 ? pMesh0 : nullptr;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static PathWaypointsEx RemoveLoopsFromPath(const PathWaypointsEx& path)
	{
		if (path.GetWaypointCount() <= 3)
		{
			return path;
		}

		PathWaypointsEx output;
		output.AddWaypoint(path, 0);

		U32 iCurr = 1;
		while (iCurr < path.GetWaypointCount())
		{
			const NavMesh* pMesh0 = GetPathEdgeNavMeshForLoopRemoval(output, output.GetWaypointCount() - 1, path, iCurr);
			Segment s0(output.GetEndWaypoint(), path.GetWaypoint(iCurr));

			output.AddWaypoint(path, iCurr);
			iCurr++;

			// Look for an intersection
			if (pMesh0)
			{
				for (U32 i = iCurr + 1; i < path.GetWaypointCount(); i++)
				{
					const NavMesh* pMesh1 = GetPathEdgeNavMeshForLoopRemoval(path, i - 1, path, i);
					if (pMesh0 != pMesh1)
					{
						continue;
					}

					Segment s1(path.GetWaypoint(i - 1), path.GetWaypoint(i));

					Scalar t0, t1;
					if (IntersectSegmentSegmentXz(s0, s1, t0, t1))
					{
						output.UpdateEndPoint(Lerp(s0, t0));
						iCurr = i;
						break;
					}
				}
			}
		}

		return output;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool ProbeForClearMotionWs(const PathFindContext& pathContext,
									  const NavMesh* pStartMesh,
									  Point_arg startPosWs,
									  Point_arg goalPosWs,
									  NavMeshHandle hGoalMesh,
									  bool debugDraw)
	{
		PROFILE_ACCUM(ProbeForClearMotionWs);

		if (!pStartMesh)
			return true;

		const Point probeStartPs = pStartMesh->WorldToParent(startPosWs);
		const Point probeGoalPs = pStartMesh->WorldToParent(goalPosWs);

		NavMesh::ProbeParams params;
		params.m_start = probeStartPs;
		params.m_move = probeGoalPs - probeStartPs;

		const NavMesh::ProbeResult basicRes = pStartMesh->ProbePs(&params);

		bool basicPassed = false;

		if (basicRes == NavMesh::ProbeResult::kReachedGoal)
		{
			if (params.m_pReachedPoly && (params.m_pReachedPoly->GetNavMeshHandle() == hGoalMesh))
			{
				basicPassed = true;
			}
		}

		if (!basicPassed)
		{
			return false;
		}

		bool reachedGoal = false;

		if (pathContext.m_dynamicSearch || (pathContext.m_pathRadius > 0.01f))
		{
			params.m_dynamicProbe = pathContext.m_dynamicSearch;
			params.m_obeyedBlockers = pathContext.m_obeyedBlockers;
			params.m_probeRadius = pathContext.m_pathRadius - 0.01f;

			const NavMesh::ProbeResult res = pStartMesh->ProbePs(&params);

			if (res == NavMesh::ProbeResult::kReachedGoal)
			{
				if (params.m_pReachedPoly && (params.m_pReachedPoly->GetNavMeshHandle() == hGoalMesh))
				{
					reachedGoal = true;
				}
			}
			else if (FALSE_IN_FINAL_BUILD(debugDraw) && (res == NavMesh::ProbeResult::kHitEdge))
			{
				g_prim.Draw(DebugLine(probeStartPs, probeGoalPs, kColorRed));
				g_prim.Draw(DebugCross(params.m_impactPoint, 0.1f, kColorRed));
			}
		}
		else
		{
			reachedGoal = true;
		}

		return reachedGoal;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static PathWaypointsEx FinalizePathWithProbes(const PathFindContext& pathContext,
												  const BuildPathParams& buildParams,
												  const PathWaypointsEx& inputPath)
	{
		PROFILE(Navigation, FinalizeWithProbes);
		PROFILE_ACCUM(FinalizeWithProbes);

		const U32F inputWaypointCount = inputPath.GetWaypointCount();

		if (false)
		{
			Locator ps = pathContext.m_parentSpace;
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorCyan;
			cs.m_groundLeg1 = kColorMagenta;
			ps.SetPos(ps.Pos() + Vector(0.0f, 0.2f, 0.0f));
			inputPath.DebugDraw(ps, true, 0.0f, cs);
		}

		if (inputWaypointCount <= 2)
		{
			return inputPath;
		}

		const NavMesh* pStartMesh = NavMeshHandle(inputPath.GetNavId(0)).ToNavMesh();
		const Point startPosPs = inputPath.GetWaypoint(0);

		PathWaypointsEx smoothedPath;
		smoothedPath.Clear();
		smoothedPath.AddWaypoint(inputPath, 0);

		I32F curInput = 0;
		float smoothedPathLen = 0.0f;
		const U64 startTick = TimerGetRawCount();
		Point smoothedPathEndPs = startPosPs;
		bool continueProbing = true;

		I32F maxProbeAheadVert = inputWaypointCount - 1;

		if (buildParams.m_finalizeProbeMaxDist >= 0.0f)
		{
			float prevDist = 0.0f;
			Point prevPos = startPosPs;
			for (U32F i = 1; i < inputWaypointCount; ++i)
			{
				if (inputPath.GetNodeType(i) == NavPathNode::kNodeTypeActionPackEnter)
					continue;

				const Point nextPos = inputPath.GetWaypoint(i);
				const float newDist = prevDist + Dist(prevPos, nextPos);

				if (newDist > buildParams.m_finalizeProbeMaxDist)
				{
					if ((buildParams.m_finalizeProbeMaxDist - prevDist) > (newDist - buildParams.m_finalizeProbeMaxDist))
					{
						// closer to next pos
						maxProbeAheadVert = i;
					}
					else
					{
						// closer to prev pos
						maxProbeAheadVert = i - 1;
					}

					break;
				}

				prevPos = nextPos;
				prevDist = newDist;
			}

			//g_prim.Draw(DebugCross(inputPath.GetWaypoint(maxProbeAheadVert), 0.25f, kColorCyan));
		}

		NavMeshHandle hActiveMesh = NavMeshHandle(pStartMesh);
		const NavMesh* pActiveMesh = pStartMesh;

		while (curInput < (inputWaypointCount - 1))
		{
			I32F nextInput = curInput + 1;

			if (continueProbing && inputPath.GetNavId(nextInput).IsValid())
			{
				const Point curPosPs = inputPath.GetWaypoint(curInput);
				const Point curPosWs = pathContext.m_parentSpace.TransformPoint(curPosPs);
				const NavMeshHandle hCurMesh = NavMeshHandle(inputPath.GetNavId(curInput));

				if (hCurMesh.IsValid() && (hCurMesh != hActiveMesh))
				{
					pActiveMesh = hCurMesh.ToNavMesh();
					hActiveMesh = hCurMesh;
				}

				for (I32F testVert = maxProbeAheadVert; testVert > curInput; --testVert)
				{
					const Point testPosPs = inputPath.GetWaypoint(testVert);
					const Point testPosWs = pathContext.m_parentSpace.TransformPoint(testPosPs);
					const NavMeshHandle hTestMesh = NavMeshHandle(inputPath.GetNavId(testVert));

					if (ProbeForClearMotionWs(pathContext, pActiveMesh, curPosWs, testPosWs, hTestMesh, false))
					{
						nextInput = testVert;
						break;
					}

#ifdef _DEBUG
					const bool durationExceeded = false;
#else
					const float curDurationMs = ConvertTicksToMilliseconds(TimerGetRawCount() - startTick);
					const bool durationExceeded = (buildParams.m_finalizeProbeMaxDurationMs >= 0.0f)
												  && (curDurationMs >= buildParams.m_finalizeProbeMaxDurationMs);
#endif
					const bool minDistanceMet = (buildParams.m_finalizeProbeMinDist < 0.0f)
												|| (smoothedPathLen >= buildParams.m_finalizeProbeMinDist);

					if (durationExceeded && minDistanceMet)
					{
						continueProbing = false;
						break;
					}
				}
			}

			const Point nextPoint = inputPath.GetWaypoint(nextInput);
			smoothedPath.AddWaypoint(inputPath, nextInput);
			curInput = nextInput;
			smoothedPathLen += Dist(smoothedPathEndPs, nextPoint);
			smoothedPathEndPs = nextPoint;
		}

		return smoothedPath;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool ProbeForClearMotionCachedWs(const PathFindContext& pathContext,
											const PathWaypointsEx& inputPathPs,
											U32F iProbeStart,
											U32F iProbeEnd,
											const NavMesh* pStartMesh,
											const NavEdge* pEdges,
											U32F numEdges,
											bool debugDraw)
	{
		PROFILE_ACCUM(ProbeForClearMotionCachedWs);

		if (!pStartMesh)
		{
			return true;
		}

		NAV_ASSERT(iProbeStart < inputPathPs.GetWaypointCount());
		NAV_ASSERT(iProbeEnd < inputPathPs.GetWaypointCount());

		const Point startPosWs = pathContext.m_parentSpace.TransformPoint(inputPathPs.GetWaypoint(iProbeStart));
		const Point endPosWs = pathContext.m_parentSpace.TransformPoint(inputPathPs.GetWaypoint(iProbeEnd));

		const NavManagerId startNavId = inputPathPs.GetNavId(iProbeStart);
		const NavManagerId endNavId = inputPathPs.GetNavId(iProbeEnd);
		const NavMeshHandle hEndMesh = NavMeshHandle(endNavId);
		const NavManagerId hStartMeshId = NavMeshHandle(startNavId).GetManagerId();
		const NavManagerId hEndMeshId = hEndMesh.GetManagerId();

		{
			NavMesh::ProbeParams params;
			params.m_start = pStartMesh->WorldToLocal(startPosWs);
			params.m_move = pStartMesh->WorldToLocal(endPosWs) - params.m_start;
			params.m_pStartPoly = &pStartMesh->GetPoly(startNavId.m_iPoly);

			const NavMesh::ProbeResult res = pStartMesh->ProbeLs(&params);
			if ((res != NavMesh::ProbeResult::kReachedGoal)
				|| !params.m_pReachedPoly
				|| (params.m_pReachedPoly->GetNavMeshHandle() != hEndMesh))
			{
				return false;
			}
		}

		const Segment probeSegWs = Segment(startPosWs, endPosWs);
		const Vector probeVecWs = endPosWs - startPosWs;
		//const float probeRadius = pathContext.m_pathRadius - NavMeshClearanceProbe::kNudgeEpsilon;
		const float probeRadius = pathContext.m_pathRadius - 0.0175f;

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const NavEdge& edge = pEdges[iEdge];
			const NavManagerId hEdgeMeshId = edge.m_pNavMesh->GetManagerId();

			if ((hEdgeMeshId != hStartMeshId) && (hEdgeMeshId != hEndMeshId))
				continue;

			const Point v0Ws = edge.m_pNavMesh->LocalToWorld(edge.m_v0Ls);
			const Point v1Ws = edge.m_pNavMesh->LocalToWorld(edge.m_v1Ls);

			const Segment edgeSegWs = Segment(v0Ws, v1Ws);
			/*
			const Vector edgeFacingWs = -RotateY90(VectorXz(edgeSegWs.a - edgeSegWs.b));

			if (DotXz(edgeFacingWs, probeVecWs) > 0.0f)
			continue;
			*/

			float tt = -1.0f;
			Scalar t0, t1;
			const bool intersects = (probeRadius > NDI_FLT_EPSILON)
										? IntersectSegmentStadiumXz(probeSegWs, edgeSegWs, probeRadius, tt)
										: IntersectSegmentSegmentXz(probeSegWs, edgeSegWs, t0, t1);

			if (intersects)
			{
				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					if (probeRadius <= NDI_FLT_EPSILON)
						tt = t0;

					const Point hitPosWs = Lerp(probeSegWs.a, probeSegWs.b, tt);
					g_prim.Draw(DebugArrow(startPosWs, endPosWs, kColorOrangeTrans));
					g_prim.Draw(DebugArrow(startPosWs, hitPosWs, kColorOrange));
					g_prim.Draw(DebugCross(hitPosWs, 0.1f, kColorRed));

					DebugDrawFlatCapsule(edgeSegWs.a, edgeSegWs.b, probeRadius, kColorRed);
					g_prim.Draw(DebugLine(edgeSegWs.a, edgeSegWs.b, kColorRed));
				}

				return false;
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void DepenetrateActionPackNodes(const PathFindContext& pathContext,
										   const BuildPathParams& buildParams,
										   PathWaypointsEx& inputPathPs)
	{
		PROFILE_AUTO(Navigation);
		PROFILE_ACCUM(DepenetrateActionPackNodes);

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		NavMesh::FindPointParams fpParams;
		fpParams.m_obeyedBlockers		= pathContext.m_obeyedBlockers;
		fpParams.m_obeyedStaticBlockers = pathContext.m_obeyedStaticBlockers;
		fpParams.m_depenRadius	= pathContext.m_pathRadius;
		fpParams.m_searchRadius = pathContext.m_pathRadius * 2.0f;
		fpParams.m_dynamicProbe = pathContext.m_dynamicSearch;

		const I32F waypointCount = inputPathPs.GetWaypointCount();

		const NavMesh* pCurMesh = nullptr;

		for (I32F i = 0; i < waypointCount; ++i)
		{
			if (!inputPathPs.IsActionPackNode(i))
				continue;

			const NavManagerId navId = inputPathPs.GetNavId(i);
			if (!pCurMesh || (navId.m_navMeshIndex != pCurMesh->GetManagerId().m_navMeshIndex))
			{
				pCurMesh = NavMeshHandle(navId).ToNavMesh();
			}

			if (!pCurMesh)
				continue;

			const Point posWs = pathContext.m_parentSpace.TransformPoint(inputPathPs.GetWaypoint(i));
			const Point posLs = pCurMesh->WorldToLocal(posWs);

			fpParams.m_point = posLs;
			fpParams.m_pStartPoly = &pCurMesh->GetPoly(navId.m_iPoly);

			pCurMesh->FindNearestPointLs(&fpParams);

			if (fpParams.m_pPoly)
			{
				const NavManagerId resolvedId = fpParams.m_pPolyEx ? fpParams.m_pPolyEx->GetNavManagerId()
																   : fpParams.m_pPoly->GetNavManagerId();

				const Point resolvedPosWs = pCurMesh->LocalToWorld(fpParams.m_nearestPoint);
				const Point resolvedPosPs = pathContext.m_parentSpace.UntransformPoint(resolvedPosWs);
				inputPathPs.UpdateWaypoint(i, resolvedPosPs);
				inputPathPs.UpdateNavManagerId(i, resolvedId);
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static PathWaypointsEx FinalizePathWithProbesCached(const PathFindContext& pathContext,
														const BuildPathParams& buildParams,
														const PathWaypointsEx& inputPathPs,
														const NavEdge* pEdges,
														U32F numEdges)
	{
		PROFILE(Navigation, FinalizeWithProbesCached);
		PROFILE_ACCUM(FinalizeWithProbesCached);

		const U32F inputWaypointCount = inputPathPs.GetWaypointCount();

		if (inputWaypointCount <= 2)
		{
			return inputPathPs;
		}

		PathWaypointsEx depenInputPathPs = inputPathPs;
		DepenetrateActionPackNodes(pathContext, buildParams, depenInputPathPs);

		if (FALSE_IN_FINAL_BUILD(false))
		{
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorCyan;
			cs.m_groundLeg1 = kColorMagenta;

			Locator modParentSpace = pathContext.m_parentSpace;
			modParentSpace.SetPos(pathContext.m_parentSpace.Pos() + Vector(0.0f, 0.2f, 0.0f));
			depenInputPathPs.DebugDraw(modParentSpace, true, 0.0f, cs, buildParams.m_debugDrawTime);
		}

		const Point startPosPs = inputPathPs.GetWaypoint(0);

		PathWaypointsEx smoothedPath;
		smoothedPath.Clear();
		smoothedPath.AddWaypoint(inputPathPs, 0);

		I32F curInput = 0;
		float smoothedPathLen = 0.0f;
		const U64 startTick = TimerGetRawCount();
		Point smoothedPathEndPs = startPosPs;
		bool continueProbing = true;

		I32F maxProbeAheadVert = inputWaypointCount - 1;

		if (buildParams.m_finalizeProbeMaxDist >= 0.0f)
		{
			float prevDist = 0.0f;
			Point prevPos = startPosPs;
			for (U32F i = 1; i < inputWaypointCount; ++i)
			{
				if (inputPathPs.GetNodeType(i) == NavPathNode::kNodeTypeActionPackEnter)
					continue;

				const Point nextPos = inputPathPs.GetWaypoint(i);
				const float newDist = prevDist + Dist(prevPos, nextPos);

				if (newDist > buildParams.m_finalizeProbeMaxDist)
				{
					if ((buildParams.m_finalizeProbeMaxDist - prevDist) > (newDist - buildParams.m_finalizeProbeMaxDist))
					{
						// closer to next pos
						maxProbeAheadVert = i;
					}
					else
					{
						// closer to prev pos
						maxProbeAheadVert = i - 1;
					}

					break;
				}

				prevPos = nextPos;
				prevDist = newDist;
			}

			//g_prim.Draw(DebugCross(inputPathPs.GetWaypoint(maxProbeAheadVert), 0.25f, kColorCyan));
		}

		NavMeshHandle hActiveMesh = NavMeshHandle(depenInputPathPs.GetNavId(0));
		const NavMesh* pActiveMesh = hActiveMesh.ToNavMesh();

		while (curInput < (inputWaypointCount - 1))
		{
			I32F nextInput = curInput + 1;

			if (continueProbing && depenInputPathPs.GetNavId(nextInput).IsValid())
			{
				const NavMeshHandle hCurMesh = NavMeshHandle(depenInputPathPs.GetNavId(curInput));

				if (!hCurMesh.IsNull() && (hCurMesh != hActiveMesh))
				{
					pActiveMesh = hCurMesh.ToNavMesh();
					hActiveMesh = hCurMesh;
				}

				for (I32F testVert = maxProbeAheadVert; testVert > curInput; --testVert)
				{
					if (ProbeForClearMotionCachedWs(pathContext,
													depenInputPathPs,
													curInput,
													testVert,
													pActiveMesh,
													pEdges,
													numEdges,
													false))
					{
						nextInput = testVert;
						break;
					}

#ifdef _DEBUG
					const bool durationExceeded = false;
#else
					const float curDurationMs	= ConvertTicksToMilliseconds(TimerGetRawCount() - startTick);
					const bool durationExceeded = (buildParams.m_finalizeProbeMaxDurationMs >= 0.0f)
												  && (curDurationMs >= buildParams.m_finalizeProbeMaxDurationMs);
#endif
					const bool minDistanceMet = (buildParams.m_finalizeProbeMinDist < 0.0f)
												|| (smoothedPathLen >= buildParams.m_finalizeProbeMinDist);

					if (durationExceeded && minDistanceMet)
					{
						continueProbing = false;
						break;
					}
				}
			}

			const Point nextPoint = inputPathPs.GetWaypoint(nextInput);
			smoothedPath.AddWaypoint(inputPathPs, nextInput);
			curInput = nextInput;
			smoothedPathLen += Dist(smoothedPathEndPs, nextPoint);
			smoothedPathEndPs = nextPoint;
		}

		return smoothedPath;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ClipLineSegmentToPortal(Point* pOutPt,
										Point_arg pt0,
										Point_arg pt1,
										const NavPortal& clipPortal,
										Scalar_arg portalShrink)
	{
		Point portalPt0 = clipPortal.m_pos[0];
		Point portalPt1 = clipPortal.m_pos[1];
		const Point ptOnPlane = portalPt0;
		Vector portalVec = (portalPt1 - portalPt0);

		// using unnormalized vector here to avoid roundoff error
		const Vector planePerpVec = RotateY90(VectorXz(portalVec));
		const Scalar zero = SCALAR_LC(0.0f);
		const Scalar one = SCALAR_LC(1.0f);
		Scalar d0 = Dot(pt0 - ptOnPlane, planePerpVec);
		Scalar d1 = Dot(pt1 - ptOnPlane, planePerpVec);
		Scalar diffD = d1 - d0;
		Point planeCrossingPt = pt0;

		if (Abs(diffD) > SCALAR_LC(kSmallFloat))
		{
			Scalar tt = -d0 / diffD;
			//NAV_ASSERT(tt >= zero && tt <= one);
			planeCrossingPt = Lerp(pt0, pt1, tt);
		}
		else if (d0 == zero)
		{
			planeCrossingPt = pt0;
		}
		else if (d1 == zero)
		{
			planeCrossingPt = pt1;
		}
		else
		{
			NAV_ASSERT(false);
			// not sure what to do here
		}

		Scalar tPortal = IndexClosestPointOnEdgeToPointXz(portalPt0, portalPt1, planeCrossingPt);
		NAV_ASSERT(tPortal >= 0.0f && tPortal <= 1.0f);
		tPortal = Clamp(tPortal, zero, one);
		Point pt = Lerp(portalPt0, portalPt1, tPortal);
		// terminate at the clipped point
		*pOutPt = pt;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddWaypointAndResetTapStack(PathWaypointsEx* pPathPsOut,
											Point_arg nextPosPs,
											const NavPortal* aPortalList,
											U32F iStartPortal,
											U32F iEndPortal, // portal index to add up to
											U32F wideTapStack)
	{
		const Point prevPosPs = pPathPsOut->GetEndWaypoint();

		const Segment pathSegPs = Segment(prevPosPs, nextPosPs);

		//g_prim.Draw(DebugArrow(prevPosPs + kUnitYAxis, nextPosPs + kUnitYAxis, kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha));

		for (U32F iPortal = iStartPortal + 1; iPortal < iEndPortal; ++iPortal)
		{
			const NavPortal& portalPs = aPortalList[iPortal];

			if (portalPs.m_type == NavPortal::Type::kWideTap)
			{
				const float portalWidth = Dist(portalPs.m_pos[0], portalPs.m_pos[1]);
				Scalar t0, t1;
				const Segment portalSegPs = Segment(portalPs.m_pos[0], portalPs.m_pos[1]);
				if ((portalWidth < NDI_FLT_EPSILON) || IntersectLinesXz(pathSegPs, portalSegPs, t0, t1))
				{
					const Point portalPosPs = Lerp(portalPs.m_pos[0], portalPs.m_pos[1], t1);
					//g_prim.Draw(DebugCross(portalPosPs, 0.25f, kColorRed));

					pPathPsOut->AddWaypoint(portalPosPs, portalPs.m_pNodeData->m_pathNode);
				}
			}
		}

		pPathPsOut->AddWaypoint(nextPosPs, aPortalList[iEndPortal].m_pNodeData->m_pathNode);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	///
	/// ThreadPathThroughPortals
	///
	/// Given a start and goal position and a list of portals, we just have to thread the path through all of the portals.
	/// This is a bit tricky.
	/// One thing to realize is that in the final smoothed path, every point that appears will be from the end points of a portal (except for
	///   the start and goal points).
	/// At each stage of the path threading, we have a "focus point" which is either the start point, or the most recently found waypoint on
	///   the path.  Given this focus point and the subsequent portals along the path, we want to find the next waypoint.
	///   In order to do this we create a NavFrustum.
	///   The NavFrustum represents the range of possible directions which can be traveled in from the focus point
	///   to pass through some number of portals ahead on the path.  The NavFrustum is represented as a pair of ordered (unnormalized) vectors.
	///   The NavFrustum remains valid as long as the direction vectors don't get crossed.
	///   Initially we set up the NavFrustum as vectors from the focus point to the right and left endpoints of the next portal.
	///   We then inspect subsequent portals one by one and decide what to do based on how the portal is arranged with respect to the current
	///   NavFrustum:
	///     - if the new portal lies completely inside the NavFrustum then we narrow (or shrink) the frustum to match the new portal
	///     - if the new portal is only partially inside the NavFrustum, then we update the left or right side of the NavFrustum only (so that it shrinks)
	///     - if the new portal completely encloses the NavFrustum, then we don't change the NavFrustum
	///     - if the new portal is disjoint (completely to the left or right of the frustum), then we need to add a new waypoint to the path,
	///         which becomes the new focus point and we restart this stage at the new focus point.
	///   This is glossing over the details, such as which point becomes the waypoint, etc..
	///
	static void ThreadPathThroughPortals(const PathFindContext& pathContext,
										 const BuildPathParams& buildParams,
										 BuildPathResults* pPathResults,
										 Point_arg startPosPs,
										 Point_arg finalGoalPosPs,
										 const NavPortal* aPortalList,
										 U32F portalCount,
										 PathWaypointsEx* pPathPsOut)
	{
		if (portalCount == 0)
			return;

		//
		// now to thread the path through all of the portals (this is where it gets tricky)
		//
		Point goalPosPs = finalGoalPosPs;

		// smoothed output
		pPathPsOut->AddWaypoint(startPosPs, aPortalList[0].m_pNodeData->m_pathNode);

		Point focusPosPs = startPosPs;
		// setup initial frustum
		NavFrustum frustum(focusPosPs, aPortalList[1], 1);

		U32F wideTapStack = 0;
		U32F iLastPathPortal = 0;

		const float epsilon = -NDI_FLT_EPSILON;

		for (U32F iPortal = 1; iPortal < portalCount; ++iPortal)
		{
			NavFrustum nextFrustum(focusPosPs, aPortalList[iPortal], iPortal);

			const Point portalCenter = AveragePos(aPortalList[iPortal].m_pos[0], aPortalList[iPortal].m_pos[1]);

			// from the focusPosPs,
			//   vec[0] is the right-hand side of the frustum
			//   vec[1] is the left-hand side of the frustum
			const Scalar portalWidth = Dist(frustum.m_vec[0], frustum.m_vec[1]);
			const Vector frust0perp = RotateY90(frustum.m_vec[0]);
			const Vector frust1perp = RotateY90(frustum.m_vec[1]);
			const Scalar f0n1 = DotXz(frust0perp, nextFrustum.m_vec[1]);  // negative means nextFrustum is disjoint to the right
			const Scalar f1n0 = DotXz(frust1perp, nextFrustum.m_vec[0]);  // positive means nextFrustum is disjoint to the left

			const bool disjointRight = (f0n1 < -epsilon);
			const bool disjointLeft = (f1n0 > epsilon);

			if ((disjointRight || disjointLeft) || (portalWidth < NDI_FLT_EPSILON)) // if portal is disjoint with the frustum, time to output a waypoint
			{
				I32F iSelectVert = 0; // iSelectVert select right or left vert (0 = right hand vert; 1 = left)

				if (disjointLeft && disjointRight && (portalWidth > NDI_FLT_EPSILON))
				{
					// we may be lying *exactly* on a portal boundary (because reasons) so pick the closer of the verts
					const float distLeft = DistXzSqr(frustum.m_vec[0] + Point(kOrigin), focusPosPs);
					const float distRight = DistXzSqr(frustum.m_vec[1] + Point(kOrigin), focusPosPs);

					if (distLeft < distRight)
					{
						iSelectVert = 1;
					}
					else
					{
						// (implied) iSelectVert = 0;
					}
				}
				else if (disjointLeft)
				{
					iSelectVert = 1;
				}
				else
				{
					// (implied) iSelectVert = 0;
				}

				const U32F iRewindPortal = frustum.m_iPortal[iSelectVert]; // iRewindPortal is the portal from which the new waypoint will come from
				const NavPortal& rewindPortal = aPortalList[iRewindPortal];

				if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawPortals))
				{
					PrimServerWrapper ps = PrimServerWrapper(pathContext.m_parentSpace);
					ps.EnableHiddenLineAlpha();

					const Vector offset = Vector(0.0f, 0.1f * float(iPortal), 0.0f);

					const NavPortal thisPortal = aPortalList[iPortal];
					const Point p0 = thisPortal.m_pos[iSelectVert] + offset;
					const Point p1 = thisPortal.m_pos[iSelectVert ^ 1] + offset;
					const Point rw0 = rewindPortal.m_pos[iSelectVert] + offset;
					const Point rw1 = rewindPortal.m_pos[iSelectVert ^ 1] + offset;

					Scalar tt = -1.0f;
					Point closestPt = focusPosPs;
					DistPointSegment(rw0, focusPosPs + offset, p0, &closestPt, &tt);

					Color clr = Slerp(kColorMagenta, kColorYellow, tt);
					ps.DrawCross(closestPt, 0.1f, clr);
					ps.DrawArrow(closestPt, rw0, 0.35f, clr);
					ps.DrawCross(focusPosPs + offset, 0.05f, kColorMagenta);
					ps.DrawLine(p0, focusPosPs + offset, kColorYellow, kColorMagenta);

					ps.SetLineWidth(3.0f);

					ps.DrawLine(rw0, rw1, kColorCyan, kColorCyanTrans);
					ps.DrawCross(rw0, 0.05f, kColorRed);

					ps.DrawLine(p0, p1, kColorYellow, kColorYellowTrans);
					ps.DrawCross(p0, 0.05f, kColorYellow);
				}

				focusPosPs = rewindPortal.m_pos[iSelectVert];

				AddWaypointAndResetTapStack(pPathPsOut,
											focusPosPs,
											aPortalList,
											iLastPathPortal,
											iRewindPortal,
											wideTapStack);
				wideTapStack = 0;
				iLastPathPortal = iRewindPortal;

				// scan forward until we reach a portal that does not have an endpoint on our focusPosPs
				for (U32F iNextPortal = iRewindPortal + 1; iNextPortal < portalCount; ++iNextPortal)
				{
					const float distToPortal = DistPointSegment(focusPosPs,
						aPortalList[iNextPortal].m_pos[0],
						aPortalList[iNextPortal].m_pos[1]);

					if (distToPortal > 0.001f)
					{
						// reset loop & rewinders
						iPortal = iNextPortal;
						frustum = NavFrustum(focusPosPs, aPortalList[iNextPortal], iNextPortal);
						break;
					}
				}
			}
			else if (aPortalList[iPortal].m_type == NavPortal::Type::kSingular) // for tap or other singular portals, always emit the path waypoint
			{
				AddWaypointAndResetTapStack(pPathPsOut, portalCenter, aPortalList, iLastPathPortal, iPortal, wideTapStack);
				wideTapStack = 0;
				iLastPathPortal = iPortal;

				// reset portal rewinders
				frustum = NavFrustum(portalCenter, aPortalList[iPortal + 1], iPortal + 1);

				focusPosPs = portalCenter;
			}
			else
			{
				if (aPortalList[iPortal].m_type == NavPortal::Type::kWideTap)
				{
					++wideTapStack;
				}

				if (portalWidth < NDI_FLT_EPSILON)
				{
					frustum = NavFrustum(focusPosPs, aPortalList[iPortal + 1], iPortal + 1);
				}
				else
				{
					// shrink our working frustum if it is shadowed by nextFrustum
					const Scalar f0n0 = DotXz(frust0perp, nextFrustum.m_vec[0]);  // positive means right side of frustum updated
					const Scalar f1n1 = DotXz(frust1perp, nextFrustum.m_vec[1]);  // negative means left side of frustum updated

					// if next frustum 0 is inside frustum
					if (f0n0 > 0.0f)
					{
						// shrink the frustum
						// remember where we are in the portal list when we updated this frustum side
						frustum.UpdateEdge(0, nextFrustum.m_vec[0], iPortal);
					}

					// if next frustum 1 is inside frustum
					if (f1n1 < 0.0f)
					{
						// shrink the frustum
						// remember where we are in the portal list when we updated this frustum side
						frustum.UpdateEdge(1, nextFrustum.m_vec[1], iPortal);
					}

					if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawPortals))
					{
						const Vector offset = Vector(0, 0.015f * float(iPortal), 0);
						PrimServerWrapper ps(pathContext.m_parentSpace);
						ps.SetDuration(buildParams.m_debugDrawTime);
						ps.DrawLine(focusPosPs + offset, focusPosPs + frustum.m_vec[0] + offset, kColorGreen);
						ps.DrawLine(focusPosPs + offset, focusPosPs + frustum.m_vec[1] + offset, kColorYellow);
					}
				}
			}
		}

		const Point curEnd = pPathPsOut->GetEndWaypoint();
		if ((DistXzSqr(curEnd, goalPosPs) > NDI_FLT_EPSILON) || (pPathPsOut->GetWaypointCount() == 1))
		{
			AddWaypointAndResetTapStack(pPathPsOut, goalPosPs, aPortalList, iLastPathPortal, portalCount - 1, wideTapStack);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool ComputeIsPathAlwaysToward(const BuildPathResults* pPathResults, Point_arg attractorPosPs)
	{
		PROFILE_AUTO(AI);
		NAV_ASSERT(pPathResults);

		static const Scalar kNearDistSqr = SCALAR_LC(6.0f * 6.0f);

		Scalar kMaxPathAngleCos = Cos(DEGREES_TO_RADIANS(30.0f));

		const U32F waypointCount = pPathResults->m_pathWaypointsPs.GetWaypointCount();

		static const Scalar kMaxAwayFromPlayerDist = SCALAR_LC(3.0f);
		Point closestPointToAttractorPs = (waypointCount > 0) ? pPathResults->m_pathWaypointsPs.GetWaypoint(0)
															  : Point(kOrigin);
		Scalar closestDistSqrToPlayer = DistSqr(closestPointToAttractorPs, attractorPosPs);

		for (U32F iWaypoint = 1; iWaypoint < waypointCount; ++iWaypoint)
		{
			const Point pathA = pPathResults->m_pathWaypointsPs.GetWaypoint(iWaypoint - 1);
			const Point pathB = pPathResults->m_pathWaypointsPs.GetWaypoint(iWaypoint);

			if (iWaypoint < waypointCount - 1)
			{
				const Point pathC = pPathResults->m_pathWaypointsPs.GetWaypoint(iWaypoint + 1);

				Vector vecA = pathB - pathA;
				Vector vecB = pathC - pathB;

				F32 dot = Dot(vecA, vecB);

				if (dot < kMaxPathAngleCos)
					return false;
			}

			const Vector dirAwayFromPlayerPs = SafeNormalize(pathA - attractorPosPs, kZero);
			const Scalar displacementAwayFromPlayer = Dot(pathB - closestPointToAttractorPs, dirAwayFromPlayerPs);
			const bool isAwayFromPlayer = displacementAwayFromPlayer > kMaxAwayFromPlayerDist;

			// path must never move away from player unless within near distance
			if (isAwayFromPlayer)
			{
				const Scalar ds = DistSqr(pathB, attractorPosPs);
				const bool isNearPlayer = ds < kNearDistSqr;
				if (!isNearPlayer)
					return false;
			}

			const Scalar segDistSqrToPlayer = DistSqr(pathB, attractorPosPs);
			if (segDistSqrToPlayer < closestDistSqrToPlayer)
			{
				closestDistSqrToPlayer = segDistSqrToPlayer;
				closestPointToAttractorPs = pathB;
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void ProcessResults(BuildPathResults* pPathResults,
							   const BuildPathParams& buildParams,
							   const PathFindContext& pathContext,
							   const PathFindConfig& config,
							   Point_arg startPosPs,
							   Point_arg goalPosPs)
	{
		PROFILE_AUTO(AI);

		pPathResults->m_length = pPathResults->m_pathWaypointsPs.ComputePathLength();

		F32 combatVectorCost = 0.0f;

		if (pathContext.m_hasCombatVector)
		{
			NavPathNodeProxy dummyNode;
			dummyNode.Invalidate();

			NavPathNode::Link dummyLink;

			Point prevPt = startPosPs;

			for (U32F i = 0; i < pPathResults->m_pathWaypointsPs.GetWaypointCount(); i++)
			{
				const Point pt = pPathResults->m_pathWaypointsPs.GetWaypoint(i); // to parent space

				combatVectorCost += CostFuncCombatVectorWeighted(pathContext, dummyNode, dummyLink, prevPt, pt, 0.0f);

				prevPt = pt;
			}
		}

		pPathResults->m_combatVectorCost = combatVectorCost;

		pPathResults->m_initialWaypointDirWs = kZero;
		pPathResults->m_finalWaypointDirWs = kZero;

		bool combatBacktrack  = false;
		F32 closestThreatDist = NDI_FLT_MAX;
		F32 closestFriendDist = NDI_FLT_MAX;

		if (buildParams.m_calculateAdditionalResults)
		{
			const PathWaypointsEx& path = pPathResults->m_pathWaypointsPs;
			const U32 waypointCount = path.GetWaypointCount();
			if (waypointCount)
			{
				NAV_ASSERT(waypointCount >= 2);

				const Vector initialDirPs = SafeNormalize(path.GetWaypoint(1) - path.GetWaypoint(0), kZero);
				pPathResults->m_initialWaypointDirWs = pathContext.m_parentSpace.TransformVector(initialDirPs);

				{
					const Point endPosPs = path.GetWaypoint(waypointCount - 1);
					Vector finalDir;
					Scalar dist = 0.0f;
					I32 waypointNum = waypointCount - 2;

					while (dist < 2.0f && waypointNum >= 0)
					{
						const Point waypointPs = path.GetWaypoint(waypointNum);
						finalDir = SafeNormalize(waypointPs - endPosPs, kZero, dist);
						waypointNum--;
					}

					pPathResults->m_finalWaypointDirWs = pathContext.m_parentSpace.TransformVector(finalDir);
				}

				// for each threat, we need to calculate two quantities related to how close the path comes to threats
				// (i.e. backtrack):
				// - does the path end too near the threat compared to where it starts?
				//	 we only apply this check if the threat is relevant to the ENDpoint of the path in terms of verticality.
				// - does the path run near a threat along the way?
				//   at each waypoint, we only apply the check if the threat is relevant to that waypoint in terms of verticality.

				for (U32 threatNum = 0; threatNum < pathContext.m_threatPositionCount; ++threatNum)
				{
					const Point threatPosPs = pathContext.m_threatPositionsPs[threatNum];
					const F32 toStart		= DistXz(threatPosPs, startPosPs);
					const F32 toEnd			= DistXz(threatPosPs, goalPosPs);

					// if endpoint is vertically relevant to this threat
					if (Abs((threatPosPs - goalPosPs).Y()) <= 0.14f * toEnd)
					{
						// check if start->end is moving directly toward threat
						if (!combatBacktrack)
						{
							if (toEnd < 3.5f && 2.0f * toEnd < toStart)
							{
								combatBacktrack = true;
							}
						}
					}

					for (I32 iWaypoint = 1; iWaypoint < waypointCount; ++iWaypoint)
					{
						const Point v1 = path.GetWaypoint(iWaypoint - 1);
						const Point v2 = path.GetWaypoint(iWaypoint);

						const F32 toPt = DistXz(threatPosPs, v2);

						// if waypoint is vertically relevant to this threat
						if (Abs((threatPosPs - v2).Y()) > 0.14f * toPt)
							continue;

						const F32 dist	  = DistPointSegmentXz(threatPosPs, Segment(v1, v2));
						closestThreatDist = Min(closestThreatDist, dist);

						// check if this waypoint is comparatively too close to threat
						if (!combatBacktrack)
						{
							if (dist < 6.0f && toStart > 3.0f && 1.1f * dist < Min(toStart, toEnd))
							{
								combatBacktrack = true;
							}
						}
					}
				}

				for (I32 iFriend = 0; iFriend < I32(pathContext.m_friendPositionCount); ++iFriend)
				{
					const Point friendPosPs = pathContext.m_friendPositionsPs[iFriend];

					for (I32 iWaypoint = 1; iWaypoint < waypointCount; ++iWaypoint)
					{
						const Point v1 = path.GetWaypoint(iWaypoint - 1);
						const Point v2 = path.GetWaypoint(iWaypoint);

						const F32 toPt = DistXz(friendPosPs, v2);

						// if waypoint is vertically relevant to this threat
						if (Abs((friendPosPs - v2).Y()) <= 0.14f * toPt)
						{
							const F32 dist = DistPointSegmentXz(friendPosPs, v1, v2);
							closestFriendDist = Min(dist, closestFriendDist);
						}
					}
				}
			}

			pPathResults->m_pathClosestThreatDist = closestThreatDist;
			pPathResults->m_pathClosestFriendDist = closestFriendDist;

			pPathResults->m_backtrack = combatBacktrack;
		}

		// calculate path exposure(s) as requested
		{
			AtomicLockJanitorRead_Jls exposureMapPointerLock(g_exposureMapMgr.GetExposureMapLock(), FILE_LINE_FUNC);

			for (U32 exposureParamsNum = 0; exposureParamsNum < BuildPathParams::kNumExposureResults; exposureParamsNum++)
			{
				const DC::ExposureSourceMask& exposureParams = buildParams.m_exposureParams[exposureParamsNum];
				F32 exposure = 0.f;

				if (exposureParams != 0)
				{
					ExposureMapType exposureMapType;
					DC::ExposureSource exposureSource;

					AI::GetExposureParamsFromMask(exposureParams, exposureSource, exposureMapType);
					exposure = g_exposureMapMgr.CalculatePathExposure(&pPathResults->m_pathWaypointsPs,
																	  exposureMapType,
																	  exposureSource,
																	  0.f);
				}

				pPathResults->m_exposure[exposureParamsNum] = exposure;
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F GatherNodeAndKeyList(const TrivialHashNavNodeData* pNodeTable,
									 NavPathNode::NodeId trivialKey,
									 NavNodeKey* pKeyListOut,
									 const NavNodeData** ppDataListOut,
									 U32F maxPathLength)
	{
		//PROFILE_AUTO(AI);

		U32F pathLength = 0;

		// build our list backwards from our desired goal
		while (trivialKey < NavPathNodeMgr::kMaxNodeCount)
		{
			if (pathLength >= maxPathLength)
				break;

			const TrivialHashNavNodeData& trivialD = pNodeTable[trivialKey];
			if (!trivialD.IsValid())
				break;

			const NavNodeKey key(trivialKey, 0);

			NavNodeData& d = *(NDI_NEW NavNodeData); // assuming calling function has pushed a scoped temp!
			d.m_fringeNode = false;
			d.m_fromDist = d.m_fromCost = trivialD.GetFromDist();
			d.m_parentNode = NavNodeKey(trivialD.m_iParent, 0);
			d.m_pathNodePosPs = trivialD.m_posPs;
			d.m_toCost = 0.0f;
			d.m_pathNode.Init(static_cast<NavPathNode::NodeType>(trivialD.m_nodeType),
							  NavManagerId(trivialD.m_navMeshIndex, trivialD.m_uniqueId, trivialD.m_iPoly, 0),
							  trivialD.m_hActionPack);

#if ENABLE_NAV_LEDGES
			if (pathLength > 0)
			{
				const NavNodeKey prevNodeKey = pKeyListOut[pathLength - 1];
				const NavNodeData* pPrevNodeData = ppDataListOut[pathLength - 1];

				if (d.m_pathNode.IsNavLedgeNode() && pPrevNodeData->m_pathNode.IsNavLedgeNode())
				{
					const NavLedgeHandle hSrcLedge = d.m_pathNode.GetNavLedgeHandle();
					const NavLedgeHandle hDstLedge = pPrevNodeData->m_pathNode.GetNavLedgeHandle();

					// fake some path waypoints for jumping between ledges (instead of just connecting the nav path nodes directly)
					const NavLedge* pSrcLedge = hSrcLedge.ToLedge();
					const NavLedge* pDstLedge = hDstLedge.ToLedge();

					if (pSrcLedge && pDstLedge && hSrcLedge != hDstLedge)
					{
						const NavLedge::Link* pSrcToDstLink = nullptr;
						for (U32F iLink = 0; iLink < pSrcLedge->GetNumNeighbors(); ++iLink)
						{
							const NavLedge::Link& srcLink = pSrcLedge->GetLink(iLink);
							if (srcLink.m_destLedgeId == pDstLedge->GetId())
							{
								pSrcToDstLink = &srcLink;
							}
						}

						NAV_ASSERTF(pSrcToDstLink, ("Trying to build a path between two nav ledges that aren't connected"));

						if (pSrcToDstLink)
						{
							const NavLedgeGraph* pLedgeGraph = pSrcLedge->GetNavLedgeGraph();

							const Point src0Ls = pSrcLedge->GetVertex0Ls();
							const Point src1Ls = pSrcLedge->GetVertex1Ls();
							const Point srcJumpLs = Lerp(src0Ls, src1Ls, pSrcToDstLink->m_closest.m_srcTT);
							const Point srcJumpPs = pLedgeGraph->LocalToParent(srcJumpLs);

							const Point dst0Ls = pDstLedge->GetVertex0Ls();
							const Point dst1Ls = pDstLedge->GetVertex1Ls();
							const Point dstJumpLs = Lerp(dst0Ls, dst1Ls, pSrcToDstLink->m_closest.m_destTT);
							const Point dstJumpPs = pLedgeGraph->LocalToParent(dstJumpLs);

							if (pathLength < maxPathLength)
							{
								NavNodeData* pInterimData = NDI_NEW NavNodeData(*pPrevNodeData); // assuming calling function has pushed a scoped temp!
								pInterimData->m_pathNodePosPs = dstJumpPs;
								pKeyListOut[pathLength] = prevNodeKey;
								ppDataListOut[pathLength] = pInterimData;
								++pathLength;
							}

							if (pathLength < maxPathLength)
							{
								NavNodeData* pInterimData = NDI_NEW NavNodeData(d); // assuming calling function has pushed a scoped temp!
								pInterimData->m_pathNodePosPs = srcJumpPs;

								pKeyListOut[pathLength] = key;
								ppDataListOut[pathLength] = pInterimData;
								++pathLength;
							}
						}
					}
				}
			}
#endif // ENABLE_NAV_LEDGES

			if (pathLength < maxPathLength)
			{
				pKeyListOut[pathLength] = key;
				ppDataListOut[pathLength] = &d;
				++pathLength;
			}

			trivialKey = trivialD.m_iParent;
		}

		// reverse list order because spacetime implies causality, and not visa versa
		if (pathLength > 1)
		{
			U32F iSrc = 0;
			U32F iDst = pathLength - 1;
			while (iSrc < iDst)
			{
				Swap(pKeyListOut[iSrc], pKeyListOut[iDst]);
				Swap(ppDataListOut[iSrc], ppDataListOut[iDst]);
				++iSrc;
				--iDst;
			}
		}

		return pathLength;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F GatherNodeAndKeyList(const NavNodeTable* pNodeTable,
									 const NavNodeKey& goalKey,
									 NavNodeKey* pKeyListOut,
									 const NavNodeData** ppDataListOut,
									 U32F maxPathLength)
	{
		//PROFILE_AUTO(AI);

		U32F pathLength = 0;

		// build our list backwards from our desired goal
		NavNodeKey key = goalKey;
		while (key.GetPathNodeId() != NavPathNodeMgr::kInvalidNodeId)
		{
			if (pathLength >= maxPathLength)
				break;

			NavNodeTable::ConstIterator itr = pNodeTable->Find(key);
			if (itr == pNodeTable->End())
				break;

			const NavNodeData& d = itr->m_data;

			if (pathLength > 0)
			{
				const NavNodeKey prevNodeKey = pKeyListOut[pathLength - 1];
				const NavNodeData* pPrevNodeData = ppDataListOut[pathLength - 1];

#if ENABLE_NAV_LEDGES
				if (d.m_pathNode.IsNavLedgeNode() && pPrevNodeData->m_pathNode.IsNavLedgeNode())
				{
					const NavLedgeHandle hSrcLedge = d.m_pathNode.GetNavLedgeHandle();
					const NavLedgeHandle hDstLedge = pPrevNodeData->m_pathNode.GetNavLedgeHandle();

					// fake some path waypoints for jumping between ledges (instead of just connecting the nav path nodes directly)
					const NavLedge* pSrcLedge = hSrcLedge.ToLedge();
					const NavLedge* pDstLedge = hDstLedge.ToLedge();

					if (pSrcLedge && pDstLedge && hSrcLedge != hDstLedge)
					{
						const NavLedge::Link* pSrcToDstLink = nullptr;
						for (U32F iLink = 0; iLink < pSrcLedge->GetNumNeighbors(); ++iLink)
						{
							const NavLedge::Link& srcLink = pSrcLedge->GetLink(iLink);
							if (srcLink.m_destLedgeId == pDstLedge->GetId())
							{
								pSrcToDstLink = &srcLink;
							}
						}

						NAV_ASSERTF(pSrcToDstLink, ("Trying to build a path between two nav ledges that aren't connected"));

						if (pSrcToDstLink)
						{
							const NavLedgeGraph* pLedgeGraph = pSrcLedge->GetNavLedgeGraph();

							const Point src0Ls = pSrcLedge->GetVertex0Ls();
							const Point src1Ls = pSrcLedge->GetVertex1Ls();
							const Point srcJumpLs = Lerp(src0Ls, src1Ls, pSrcToDstLink->m_closest.m_srcTT);
							const Point srcJumpPs = pLedgeGraph->LocalToParent(srcJumpLs);

							const Point dst0Ls = pDstLedge->GetVertex0Ls();
							const Point dst1Ls = pDstLedge->GetVertex1Ls();
							const Point dstJumpLs = Lerp(dst0Ls, dst1Ls, pSrcToDstLink->m_closest.m_destTT);
							const Point dstJumpPs = pLedgeGraph->LocalToParent(dstJumpLs);

							if (pathLength < maxPathLength)
							{
								NavNodeData* pInterimData = NDI_NEW NavNodeData(*pPrevNodeData); // assuming calling function has pushed a scoped temp!
								pInterimData->m_pathNodePosPs = dstJumpPs;
								pKeyListOut[pathLength] = prevNodeKey;
								ppDataListOut[pathLength] = pInterimData;
								++pathLength;
							}

							if (pathLength < maxPathLength)
							{
								NavNodeData* pInterimData = NDI_NEW NavNodeData(d); // assuming calling function has pushed a scoped temp!
								pInterimData->m_pathNodePosPs = srcJumpPs;

								pKeyListOut[pathLength] = key;
								ppDataListOut[pathLength] = pInterimData;
								++pathLength;
							}
						}
					}
				}
#endif // ENABLE_NAV_LEDGES
			}

			if (pathLength < maxPathLength)
			{
				pKeyListOut[pathLength] = key;
				ppDataListOut[pathLength] = &d;
				++pathLength;
			}

			key = d.m_parentNode;
		}

		// reverse list order because spacetime implies causality, and not visa versa
		if (pathLength > 1)
		{
			U32F iSrc = 0;
			U32F iDst = pathLength - 1;
			while (iSrc < iDst)
			{
				Swap(pKeyListOut[iSrc], pKeyListOut[iDst]);
				Swap(ppDataListOut[iSrc], ppDataListOut[iDst]);
				++iSrc;
				--iDst;
			}
		}

		return pathLength;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static U32F RemoveUnnecessaryNodes(NavNodeKey* aKeyListOut,
									   const NavNodeData** apDataListOut,
									   U32F initialPathLength)
	{
		//PROFILE_AUTO(AI);

		if (initialPathLength < 3)
			return initialPathLength;

		U32F pathLength = 1;

		for (U32F iCur = 1; iCur < (initialPathLength - 1); ++iCur)
		{
			const NavNodeData* pPrevNode = apDataListOut[iCur - 1];
			const NavNodeData* pCurNode = apDataListOut[iCur];
			const NavNodeData* pNextNode = apDataListOut[iCur - 1];

#if ENABLE_NAV_LEDGES
			const NavLedgeHandle hPrevLedge = pPrevNode->m_pathNode.GetNavLedgeHandle();
			const NavLedgeHandle hCurLedge = pCurNode->m_pathNode.GetNavLedgeHandle();
			const NavLedgeHandle hNextLedge = pNextNode->m_pathNode.GetNavLedgeHandle();

			const bool prevIsLedge = !hPrevLedge.IsNull();
			const bool curIsLedge = !hCurLedge.IsNull();
			const bool nextIsLedge = !hNextLedge.IsNull();
#endif // ENABLE_NAV_LEDGES

			bool skipCur = false;

#if ENABLE_NAV_LEDGES
			if (prevIsLedge && curIsLedge && nextIsLedge)
			{
				const U32F prevId = hPrevLedge.GetLedgeId();
				const U32F curId = hCurLedge.GetLedgeId();
				const U32F nextId = hNextLedge.GetLedgeId();

				skipCur = (prevId == curId) && (prevId == nextId);
			}
			else
#endif // ENABLE_NAV_LEDGES
				if (pPrevNode->m_pathNode.GetNodeType() == NavPathNode::kNodeTypeActionPackExit)
				{
					if (pCurNode->m_pathNode.IsNavMeshNode())
					{
						skipCur = pPrevNode->m_pathNode.GetNavManagerId() == pCurNode->m_pathNode.GetNavManagerId();
					}
#if ENABLE_NAV_LEDGES
					else if (curIsLedge)
					{
						skipCur = hPrevLedge == hCurLedge;
					}
#endif // ENABLE_NAV_LEDGES
				}

			aKeyListOut[pathLength] = aKeyListOut[iCur];
			apDataListOut[pathLength] = apDataListOut[iCur];

			if (skipCur)
			{

			}
			else
			{
				++pathLength;
			}
		}

		aKeyListOut[pathLength] = aKeyListOut[initialPathLength - 1];
		apDataListOut[pathLength] = apDataListOut[initialPathLength - 1];
		++pathLength; // stupid dummy terminator nonsense. who wrote this crap anyways?

		return pathLength;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool AreAllNodesValid(const NavNodeKey* aKeyList, const NavNodeData** apDataList, U32F pathLength, const bool useTrivialHashTable)
	{
		//PROFILE_AUTO(AI);

		NAV_ASSERT(aKeyList);
		NAV_ASSERT(apDataList);

		if (!aKeyList || !apDataList)
			return false;

		for (U32F iNode = 0; iNode < pathLength; ++iNode)
		{
			NAV_ASSERT(apDataList[iNode]);

			if (!apDataList[iNode])
			{
				return false;
			}

			const NavPathNode::NodeId pathNodeId = aKeyList[iNode].GetPathNodeId();
			if (pathNodeId > NavPathNodeMgr::kMaxNodeCount)
			{
				return false;
			}

			switch (apDataList[iNode]->m_pathNode.m_nodeType)
			{
			case NavPathNode::kNodeTypePoly:
			case NavPathNode::kNodeTypePolyEx:
				if (!apDataList[iNode]->m_pathNode.GetNavMeshHandle().IsValid())
				{
					return false;
				}
				break;

			case NavPathNode::kNodeTypeActionPackEnter:
			case NavPathNode::kNodeTypeActionPackExit:
				if (!apDataList[iNode]->m_pathNode.GetActionPackHandle().IsValid())
				{
					return false;
				}
				break;

#if ENABLE_NAV_LEDGES
			case NavPathNode::kNodeTypeNavLedge:
				if (!apDataList[iNode]->m_pathNode.GetNavLedgeHandle().IsValid())
				{
					return false;
				}
				break;
#endif // ENABLE_NAV_LEDGES

			default:
				return false;
			}
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static NavLocation GetNavLoc(const PathFindContext& ctx, const NavNodeData* data)
	{
		const Point posWs = data->m_pathNode.ParentToWorld(data->m_pathNodePosPs);
		const Point posPs = ctx.m_parentSpace.UntransformPoint(posWs);

		NavLocation ret;
		ret.SetPs(posPs, data->m_pathNode.GetNavPolyHandle());
		return ret;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static Point GetPtPs(const PathFindContext& ctx, const NavNodeData* data)
	{
		const Point posWs = data->m_pathNode.ParentToWorld(data->m_pathNodePosPs);
		return ctx.m_parentSpace.UntransformPoint(posWs);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void AddPt(PathWaypointsEx& to, const PathFindContext& ctx, const NavNodeData* data)
	{
		const Point posWs = data->m_pathNode.ParentToWorld(data->m_pathNodePosPs);
		const Point posPs = ctx.m_parentSpace.UntransformPoint(posWs);
		to.AddWaypoint(posPs, data->m_pathNode);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool SamePoly(const NavNodeData* a, const NavNodeData* b)
	{
		return a->m_pathNode.GetNavPolyHandle() == b->m_pathNode.GetNavPolyHandle();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	class ALIGNED(16) FastApproxSmoothNodeInfo
	{
	private:
		Point m_posPs;
		const NavPoly* m_pPoly;
		bool m_isNavMeshNode;

	public:
		void SetPoly(const NavPoly* pPoly)
		{
			m_pPoly = pPoly;
		}

		void SetPosPs(const Point posPs)
		{
			m_posPs = posPs;
		}

		const NavPoly* GetPoly() const
		{
			return m_pPoly;
		}

		const Point GetPosPs() const
		{
			return m_posPs;
		}

		void SetIsNavMeshNode(bool isNavMeshNode)
		{
			m_isNavMeshNode = isNavMeshNode;
		}

		bool IsNavMeshNode() const
		{
			return m_isNavMeshNode;
		}
	};
	STATIC_ASSERT(sizeof(FastApproxSmoothNodeInfo) == 32);
	STATIC_ASSERT(alignof(FastApproxSmoothNodeInfo) == 16);

#ifdef HEADLESS_BUILD
	/// --------------------------------------------------------------------------------------------------------------- ///
	static int GatherNodes(const NavNodeTable& nodeTable,
		const NavNodeKey& goalKey,
		FastApproxSmoothNodeInfo*& nodeInfo,
		int maxPathLength)
	{
		int pathLength = 0;

		// build our list backwards from our desired goal
		const NavNodeKey* pKey = &goalKey;
		while (pKey->GetPathNodeId() != NavPathNodeMgr::kInvalidNodeId)
		{
			if (pathLength >= maxPathLength)
				break;

			NavNodeTable::ConstIterator itr = nodeTable.Find(*pKey);
			if (itr == nodeTable.End())
				break;

			const NavNodeData& d = itr->m_data;

			if (pathLength < maxPathLength)
			{
				const NavPoly* pPoly = d.m_pathNode.GetNavPolyHandle().ToNavPoly();
				if (!pPoly)
					break;

				--nodeInfo;
				nodeInfo->SetPosPs(d.m_pathNodePosPs);
				nodeInfo->SetPoly(pPoly);
				nodeInfo->SetIsNavMeshNode(d.m_pathNode.GetNodeType() == NavPathNode::kNodeTypePoly);
				++pathLength;
			}

			pKey = &d.m_parentNode;
		}

		return pathLength;
	}
#else
	/// --------------------------------------------------------------------------------------------------------------- ///
	static int GatherNodes(const NavMeshMgr& mgr,
						   TrivialHashNavNodeData* pNodeTable,
						   const U16 goalKey,
						   FastApproxSmoothNodeInfo*& nodeInfo,
						   int maxPathLength)
	{
		int pathLength = 0;

		// build our list backwards from our desired goal
		U16 key = goalKey;
		while (key < NavPathNodeMgr::kMaxNodeCount)
		{
			if (pathLength >= maxPathLength)
				break;

			const TrivialHashNavNodeData& d = pNodeTable[key];
			if (!d.IsValid())
				break;

			const NavMesh* const pMesh = mgr.UnsafeFastLookupNavMesh(d.m_navMeshIndex, d.m_uniqueId);

			// possible for the mesh to not exist if unregistered/ABA'd
			if (!pMesh)
				break;

			// if we got our original valid mesh back the poly will always exist
			const NavPoly* const pPoly = pMesh->UnsafeGetPolyFast(d.m_iPoly);

			--nodeInfo;
			nodeInfo->SetPosPs(d.m_posPs);
			nodeInfo->SetPoly(pPoly);
			nodeInfo->SetIsNavMeshNode(d.m_nodeType == NavPathNode::kNodeTypePoly);
			++pathLength;

			key = d.m_iParent;
		}

		return pathLength;
	}
#endif

	/// --------------------------------------------------------------------------------------------------------------- ///
	float GetFastApproxSmoothDistanceOnly(const PathFindNodeData& searchData,
#ifdef HEADLESS_BUILD
										  const NavNodeKey goalKey,
#else
										  U16 key,
#endif
										  Point_arg requestedGoalPosWs,
										  bool* const pPathCrossesTap /* = nullptr */)
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

		const Point requestedGoalPosPs = searchData.m_context.m_parentSpace.UntransformPoint(requestedGoalPosWs);

		CONST_EXPR int kMaxNumNodes = 384;
		FastApproxSmoothNodeInfo rawNodeInfo[kMaxNumNodes];
		FastApproxSmoothNodeInfo* nodeInfo = rawNodeInfo + kMaxNumNodes;
#ifdef HEADLESS_BUILD
		int pathLength = GatherNodes(searchData.m_visitedNodes, goalKey, nodeInfo, kMaxNumNodes);
#else
		int pathLength = GatherNodes(*EngineComponents::GetNavMeshMgr(), searchData.m_trivialHashVisitedNodes, key, nodeInfo, kMaxNumNodes - 1);
#endif

		if (pathLength == 0)
		{
			return NDI_FLT_MAX;
		}

		if (pPathCrossesTap)
			*pPathCrossesTap = false;

		{
			// add back centroid of first poly as 2nd node
			--nodeInfo;
			nodeInfo[0] = nodeInfo[1];
			const NavPoly* pPoly = nodeInfo[1].GetPoly();
			const Vector localToParent = AsVector(pPoly->GetNavMesh()->GetOriginPs().GetPosition());
			nodeInfo[1].SetPosPs(localToParent + pPoly->GetCentroid());
			++pathLength;

			// replace goal node (currently centroid) with actual goal pos
			nodeInfo[pathLength - 1].SetPosPs(requestedGoalPosPs);
		}

		// this is handled gracefully in GatherNodes so it's okay to strip out in Final.
		// it's just here as a sanity check since we don't EXPECT this many, so it probably means
		// something else is wrong.
		NAV_ASSERT(pathLength < kMaxNumNodes - 32);

		Point lastPs = nodeInfo[0].GetPosPs();

		float pathDist = 0.0f;
		if (pathLength >= 3)
		{
			I32 iP = 0;
			for (; iP < pathLength - 1; ++iP)
			{
				// if P is a navmesh node, we've found our P
				if (nodeInfo[iP].IsNavMeshNode())
					break;

				// this is not a navmesh node
				if (pPathCrossesTap)
					*pPathCrossesTap = true;

				// if P is not the start node (already added) and is not a navmesh node, add directly to output, unmodified.
				if (iP)
				{
					const Point newPs = nodeInfo[iP].GetPosPs();
					pathDist += Dist(lastPs, newPs);
					lastPs = newPs;
				}
			}

			I32 iM = iP + 1;

			Point P = nodeInfo[iP].GetPosPs();
			Point M = nodeInfo[iM].GetPosPs();

			bool hasEntryEdge = false;
			Point exitVert1;
			Point exitVert2;

			//////////////// main loop
			for (I32 iQ = iM + 1; iQ < pathLength; ++iQ)
			{
				const NavPoly* Mpoly = nodeInfo[iM].GetPoly();
				const Point Q = nodeInfo[iQ].GetPosPs();

				if (nodeInfo[iM].IsNavMeshNode())
				{
					// everything in here is the very VERY hot loop. gentlemen, count your cycles.

					const NavPoly* Qpoly = nodeInfo[iQ].GetPoly();

					// we need to advance Q until it is different from M and (is the last entry OR is different from what comes after it)
					if (Mpoly == Qpoly || (iQ < pathLength - 1 && Qpoly == nodeInfo[iQ + 1].GetPoly()))
					{
						if (!nodeInfo[iQ].IsNavMeshNode())
						{
							// Q is a node we can't touch, advance working set to (M, Q, new node)
							P = M;
							M = Q;
							iM = iQ;

							// and discard cached edge because we haven't updated it this iteration
							hasEntryEdge = false;
						}

						continue;
					}

					const Vector localToParent = AsVector(Mpoly->GetNavMesh()->GetOriginPs().GetPosition());

					// vert order is flipped since it's the same edge but from the perspective of the
					// adjacent poly, which has the same winding order
					const Point enterVert1 = exitVert2;
					const Point enterVert2 = exitVert1;

					// check all edges to see if any edge's adjacent poly is Q
					//
					// no matching edge occurs when M is a pre-link and Q is the link, except the poly we have
					// for it is the pre-link of the OTHER navmesh. We could correct for this and thereby
					// ensure that every iteration has an exit edge, but it's slower, and actually slightly
					// decreases average path quality because of how long and thin link polys are
					// (therefore their centroids are not very representative of most paths through them)
					bool hasExitEdge = false;
					{
						const __m128i q = _mm_shufflelo_epi16(_mm_cvtsi32_si128(*reinterpret_cast<const int*>(Qpoly->GetPointerToId())), 0);
						const __m128i v = _mm_loadu_si64(Mpoly->GetAdjacencyArray());
						const __m128i cmp = _mm_cmpeq_epi16(q, v);
						const U32 msk = (U32)_mm_movemask_epi8(cmp) & 0xFF;
						if (msk)
						{
							const U32 iVert = (U32)CountTrailingZeros_U32(msk) >> 1;
							hasExitEdge = true;
							exitVert1 = localToParent + Mpoly->GetVertex(iVert);
							exitVert2 = localToParent + Mpoly->GetNextVertex(iVert);
						}
					}

					bool fixup = false;
					Point origP = P;

					// try to fixup entry of P->Q
					if (hasEntryEdge && !FastPointEqual(P, enterVert1) && !FastPointEqual(P, enterVert2))
					{
						const Vector t = enterVert2 - enterVert1;
						const __m128 d = (enterVert1 - P).QuadwordValue();
						const __m128 s = (Q - P).QuadwordValue();
						const __m128 sshuf = _mm_shuffle_ps(s, s, 198);
						const __m128 ds = _mm_mul_ps(d, sshuf);
						const __m128 numshuf = _mm_shuffle_ps(ds, ds, 198);
						const __m128 num = _mm_sub_ss(ds, numshuf);
						const __m128 ts = _mm_mul_ps(t.QuadwordValue(), sshuf);
						const __m128 denshuf = _mm_shuffle_ps(ts, ts, 198);
						const __m128 den = _mm_sub_ss(denshuf, ts);
						const float tq = _mm_cvtss_f32(_mm_mul_ss(num, _mm_rcp_ss(den)));

						CONST_EXPR float kThresh = 0.08f;
						CONST_EXPR float kInvThresh = 1.0f - kThresh;

						if (tq != Min(Max(tq, kThresh), kInvThresh))
						{
							const __m128 MQ = (Q - M).QuadwordValue();
							const __m128 MP = (P - M).QuadwordValue();
							const __m128 MV = ((enterVert1 + Min(Max(tq, 0.0f), 1.0f) * t) - M).QuadwordValue();
							const __m128 MPshuf = _mm_shuffle_ps(MP, MP, 198);
							const __m128 MVMP = _mm_mul_ps(MV, MPshuf);
							const __m128 crossPVshuf = _mm_shuffle_ps(MVMP, MVMP, 198);
							const __m128 crossPV = _mm_sub_ss(MVMP, crossPVshuf);
							const __m128 MQMP = _mm_mul_ps(MQ, MPshuf);
							const __m128 crossPQshuf = _mm_shuffle_ps(MQMP, MQMP, 198);
							const __m128 crossPQ = _mm_sub_ss(crossPQshuf, MQMP);
							const __m128 MVshuf = _mm_shuffle_ps(MV, MV, 198);
							const __m128 MQMV = _mm_mul_ps(MQ, MVshuf);
							const __m128 crossQVshuf = _mm_shuffle_ps(MQMV, MQMV, 198);
							const __m128 crossQV = _mm_sub_ss(MQMV, crossQVshuf);
							const __m128 msk = _mm_xor_ps(_mm_and_ps(_mm_xor_ps(crossPV, crossPQ), _mm_xor_ps(crossQV, crossPQ)), _mm_set_ss(tq - kInvThresh));
							P = Point(_mm_blendv_ps(enterVert1.QuadwordValue(), enterVert2.QuadwordValue(), _mm_shuffle_ps(msk, msk, 0)));

							fixup = true;
						}
					}

					// current exit edge will become our entry edge next iteration
					hasEntryEdge = hasExitEdge;

					// try to fixup exit of P->Q (where P may have been updated)
					if (hasExitEdge && !FastPointEqual(P, exitVert1) && !FastPointEqual(P, exitVert2))
					{
						const Vector t = exitVert2 - exitVert1;
						const __m128 d = (exitVert1 - P).QuadwordValue();
						const __m128 s = (Q - P).QuadwordValue();
						const __m128 sshuf = _mm_shuffle_ps(s, s, 198);
						const __m128 ds = _mm_mul_ps(d, sshuf);
						const __m128 numshuf = _mm_shuffle_ps(ds, ds, 198);
						const __m128 num = _mm_sub_ss(ds, numshuf);
						const __m128 ts = _mm_mul_ps(t.QuadwordValue(), sshuf);
						const __m128 denshuf = _mm_shuffle_ps(ts, ts, 198);
						const __m128 den = _mm_sub_ss(denshuf, ts);
						const float tq = _mm_cvtss_f32(_mm_mul_ss(num, _mm_rcp_ss(den)));
						const float clampedTq = Min(Max(tq, 0.0f), 1.0f);

						if (tq != clampedTq)
						{
							const __m128 msk = _mm_set1_ps(clampedTq - 1.0f);
							const Point v1 = Point(_mm_blendv_ps(exitVert2.QuadwordValue(), exitVert1.QuadwordValue(), msk));

							if (fixup)
							{
								// we had an entry fixup. if origP->v1 goes through the
								// entry edge, we don't need it, so replace it. Otherwise,
								// commit it.

								const Vector enterT = enterVert2 - enterVert1;
								const __m128 enterD = (enterVert1 - origP).QuadwordValue();
								const __m128 enterS = (v1 - origP).QuadwordValue();
								const __m128 enterSshuf = _mm_shuffle_ps(enterS, enterS, 198);
								const __m128 enterDs = _mm_mul_ps(enterD, enterSshuf);
								const __m128 enterNumshuf = _mm_shuffle_ps(enterDs, enterDs, 198);
								const __m128 enterNum = _mm_sub_ss(enterDs, enterNumshuf);
								const __m128 enterTs = _mm_mul_ps(enterT.QuadwordValue(), enterSshuf);
								const __m128 enterDenshuf = _mm_shuffle_ps(enterTs, enterTs, 198);
								const __m128 enterDen = _mm_sub_ss(enterDenshuf, enterTs);
								const float enterTq = _mm_cvtss_f32(_mm_mul_ss(enterNum, _mm_rcp_ss(enterDen)));
								if (enterTq != Min(Max(enterTq, 0.0f), 1.0f))
								{
									// doesn't go through entry edge, so commit entry fixup
									const Point newPs = P;

#ifdef KOMAR
									if (DistSqr(lastPs, newPs) == 0.0f)
									{
										//g_prim.Draw(DebugLine(newPs, Vector(0.0f, 2.0f, 0.0f), kColorMagenta, kColorMagenta, 9.0f), kPrimDuration1Frame);
										__debugbreak();
									}
#endif // KOMAR

									pathDist += Dist(lastPs, newPs);
									lastPs = newPs;
								}
							}
							// fixup is now v1 (exit)
							P = v1;
							fixup = true;
						}
					}

					if (fixup)
					{
						const Point newPs = P;

#ifdef KOMAR
						if (DistSqr(lastPs, newPs) == 0.0f)
						{
							//g_prim.Draw(DebugLine(newPs, Vector(0.0f, 2.0f, 0.0f), kColorMagenta, kColorMagenta, 9.0f), kPrimDuration1Frame);
							__debugbreak();
						}
#endif // KOMAR

						pathDist += Dist(lastPs, newPs);
						lastPs = newPs;
					}
				}
				else
				{
					if (pPathCrossesTap)
						*pPathCrossesTap = true;

					// if we encounter a node we can't touch for M, we have (P, M, Q) and we want to commit M
					// and advance the working set to (M, Q, new node)

					const Point newPs = M;
					pathDist += Dist(lastPs, newPs);
					lastPs = newPs;

					P = M;

					// and discard cached edge because we haven't updated it this iteration
					hasEntryEdge = false;
				}
				M = Q;
				iM = iQ;
			} //////////////// end of main loop
		}

		// add the end
		const Point newPs = requestedGoalPosPs;
		pathDist += Dist(lastPs, newPs);

		return pathDist;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void BuildPath(const PathFindNodeData& searchData,
				   const BuildPathParams& buildParams,
				   const NavNodeKey& goalKey,
				   Point_arg requestedGoalPosWs,
				   BuildPathResults* pPathResults)
	{
		PROFILE_AUTO(AI);
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

		if (!pPathResults)
			return;

		pPathResults->Clear();

		const bool useTrivialHashTable = searchData.m_trivialHashVisitedNodes;
		const TrivialHashNavNodeData* const pTrivialNodeTable = useTrivialHashTable ? searchData.m_trivialHashVisitedNodes : nullptr;
		const NavNodeTable* const pNodeTable = useTrivialHashTable ? nullptr : &searchData.m_visitedNodes;

		NavNodeTable::ConstIterator gItr;
		NavPathNode::NodeId gI = goalKey.GetPathNodeId();
		if (!useTrivialHashTable)
		{
			gItr = pNodeTable->Find(goalKey);
		}

		if (useTrivialHashTable)
		{
			if (gI > NavPathNodeMgr::kMaxNodeCount || !pTrivialNodeTable[gI].IsValid())
				return;
		}
		else
		{
			if (gItr == pNodeTable->End())
				return;
		}

		bool usesTap = false;
		const PathFindConfig& config = searchData.m_config;
		const PathFindContext& pathContext = buildParams.m_pContextOverride ? *buildParams.m_pContextOverride
																			: searchData.m_context;

		// build list of nodes by walking backwards from the goal following the
		// parent node references back to the start
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		// pad it because we might introduce extra ledge nodes
		const size_t maxNumNodes = Max(useTrivialHashTable ? 10ULL * PathWaypointsEx::kMaxCount + 1
														   : pNodeTable->Size() * 3,
									   (size_t)PathWaypointsEx::kMaxCount);

		NavNodeKey* aNodeKeyList = NDI_NEW NavNodeKey[maxNumNodes + 1];
		const NavNodeData** apNodeDataList = NDI_NEW const NavNodeData*[maxNumNodes + 1];

#ifndef FINAL_BUILD
		memset(aNodeKeyList, 0, sizeof(NavNodeKey) * (maxNumNodes + 1));
		memset(apNodeDataList, 0, sizeof(NavNodeData*) * (maxNumNodes + 1));
#endif

		if (buildParams.m_smoothPath == kApproxSmoothing)
		{
			++aNodeKeyList;
			++apNodeDataList;
		}

		U32F initialPathLength;
		if (useTrivialHashTable)
		{
			initialPathLength = GatherNodeAndKeyList(pTrivialNodeTable, gI, aNodeKeyList, apNodeDataList, maxNumNodes - 1);
		}
		else
		{
			initialPathLength = GatherNodeAndKeyList(pNodeTable, goalKey, aNodeKeyList, apNodeDataList, maxNumNodes - 1);
		}

		if (initialPathLength == 0)
		{
			return;
		}

		// requested goal, in our OWN parent space
		const Point requestedGoalPosPs = pathContext.m_parentSpace.UntransformPoint(requestedGoalPosWs);

		if (buildParams.m_smoothPath != kApproxSmoothing)
		{
			NavNodeData* pEndData = NDI_NEW NavNodeData(*apNodeDataList[initialPathLength - 1]);
			pEndData->m_pathNodePosPs = pEndData->m_pathNode.WorldToParent(requestedGoalPosWs);

			// reuse the same nav node data for our end point
			if (buildParams.m_reversePath)
			{
				apNodeDataList[initialPathLength - 1] = pEndData;
			}
			else
			{
				aNodeKeyList[initialPathLength] = aNodeKeyList[initialPathLength - 1];
				apNodeDataList[initialPathLength] = pEndData;
				++initialPathLength;
			}
		}
		else
		{
			if (const NavPoly* pPoly = apNodeDataList[0]->m_pathNode.GetNavPolyHandle().ToNavPoly())
			{
				// add back centroid of first poly as 2nd node
				--aNodeKeyList;
				--apNodeDataList;

				aNodeKeyList[0] = aNodeKeyList[1];
				apNodeDataList[0] = apNodeDataList[1];

				NavNodeData* pA = NDI_NEW NavNodeData(*apNodeDataList[1]);
				pA->m_pathNodePosPs = pPoly->GetNavMesh()->LocalToParent(pPoly->GetCentroid());
				apNodeDataList[1] = pA;

				++initialPathLength;
			}

			// replace goal node (currently centroid) with actual goal pos
			NavNodeData* pB = NDI_NEW NavNodeData(*apNodeDataList[initialPathLength - 1]);
			pB->m_pathNodePosPs = requestedGoalPosPs;
			apNodeDataList[initialPathLength - 1] = pB;
		}

		I32F pathLength = initialPathLength;
		if (buildParams.m_smoothPath != kApproxSmoothing)
			pathLength = RemoveUnnecessaryNodes(aNodeKeyList, apNodeDataList, initialPathLength);

		if (!AreAllNodesValid(aNodeKeyList, apNodeDataList, pathLength, useTrivialHashTable))
			return;

		// setup some initial data about our raw path (add one for when we make a dummy goal 'portal')
		const Point startPosPs = apNodeDataList[0]->m_pathNodePosPs;
		Point truncatedGoalPosPs = requestedGoalPosPs;

		if (apNodeDataList[0]->m_pathNode.IsActionPackNode())
			usesTap = true;

		const float tapDepenRadius = Max(buildParams.m_apEntryDistance, pathContext.m_pathRadius);

		for (U32F iNode = 1; iNode < pathLength; ++iNode)
		{
			const NavPathNodeProxy& node = apNodeDataList[iNode]->m_pathNode;
			const NavMeshHandle hNodeMesh = node.GetNavMeshHandle();

			if (node.IsActionPackNode())
			{
				usesTap = true;

				if ((node.GetNodeType() == NavPathNode::kNodeTypeActionPackEnter) && (tapDepenRadius >= NDI_FLT_EPSILON))
				{
					const NavMesh* pMesh = hNodeMesh.ToNavMesh();

					const Point basePosWs = pathContext.m_parentSpace.TransformPoint(apNodeDataList[iNode]->m_pathNodePosPs);
					const Point basePosLs = pMesh->WorldToLocal(basePosWs);

					NavMesh::FindPointParams params;
					params.m_pStartPoly = node.GetNavPolyHandle().ToNavPoly();
					params.m_pStartPolyEx = nullptr;
					params.m_obeyedBlockers = pathContext.m_obeyedBlockers;
					params.m_obeyedStaticBlockers = pathContext.m_obeyedStaticBlockers;
					params.m_dynamicProbe = pathContext.m_dynamicSearch;
					params.m_obeyStealthBoundary = false;
					params.m_crossLinks = true;

					params.m_point = basePosLs;
					params.m_searchRadius = tapDepenRadius;
					params.m_depenRadius = tapDepenRadius + NavMeshClearanceProbe::kNudgeEpsilon;

					NavMeshDepenetrator2 depenProbe;
					depenProbe.Init(basePosLs, pMesh, params);
					depenProbe.Execute(pMesh, params.m_pStartPoly, params.m_pStartPolyEx);

					const Point resolvedPosLs = depenProbe.GetResolvedPosLs();
					const Point resolvedPosWs = pMesh->LocalToWorld(resolvedPosLs);

					const Point resolvedPosPs = pathContext.m_parentSpace.UntransformPoint(resolvedPosWs);

					if (const NavPoly* pResolvedPoly = depenProbe.GetResolvedPoly())
					{
						NavNodeData* pNewNodeData = NDI_NEW NavNodeData(*apNodeDataList[iNode]);
						pNewNodeData->m_pathNode.m_navMgrId = pResolvedPoly->GetNavManagerId();
						pNewNodeData->m_pathNodePosPs = resolvedPosPs;
						apNodeDataList[iNode] = pNewNodeData;
					}
				}
			}

			if (pPathResults->m_hNextNavMesh.IsNull() && (hNodeMesh != apNodeDataList[0]->m_pathNode.GetNavMeshHandle()))
			{
				pPathResults->m_hNextNavMesh = hNodeMesh;
			}

			if (node.IsActionPackNode() && !pPathResults->m_hNavActionPack.IsValid())
			{
				pPathResults->m_hNavActionPack = node.GetActionPackHandle();
			}
		}

		pPathResults->m_usesTap = usesTap;

		// cache the first few nodes before smoothing
		{
			U32 iOut = 0;

			for (U32 iIn = 0; iIn < pathLength && iOut < BuildPathResults::kNumCachedPolys; ++iIn)
			{
				// do not create duplicate entries
				const NavPolyHandle hPoly = apNodeDataList[iIn]->m_pathNode.GetNavPolyHandle();
				if (iOut == 0 || pPathResults->m_cachedPolys[iOut - 1] != hPoly)
				{
					if (const NavPoly* pPoly = hPoly.ToNavPoly())
						pPathResults->m_cachedPolys[iOut++] = hPoly;
				}
			}

			// invalidate remainder of cache
			for (; iOut < BuildPathResults::kNumCachedPolys; ++iOut)
			{
				pPathResults->m_cachedPolys[iOut] = NavPolyHandle();
			}
		}

		if (0 == pathLength)
			return;

		PathWaypointsEx workingPathPs;
		workingPathPs.Clear();

		if (buildParams.m_smoothPath == kFullSmoothing)
		{
			NavPortal* pPortalList = NDI_NEW NavPortal[pathLength];

			const U32F portalCount = BuildPortalList(pathContext,
													 buildParams,
													 startPosPs,
													 truncatedGoalPosPs,
													 aNodeKeyList,
													 apNodeDataList,
													 pathLength,
													 tapDepenRadius,
													 pPortalList);
			ThreadPathThroughPortals(pathContext,
									 buildParams,
									 pPathResults,
									 startPosPs,
									 truncatedGoalPosPs,
									 pPortalList,
									 portalCount,
									 &workingPathPs);
		}
		else if (buildParams.m_smoothPath == kApproxSmoothing)
		{
			SmoothPathApproximate(pathLength, workingPathPs, apNodeDataList, truncatedGoalPosPs);
		}
		else
		{
			PROFILE_ACCUM(AiNoPathSmoothing);

			// simple unsmoothed output
			for (U32F iNode = 0; iNode < pathLength - 1; ++iNode)
			{
				const Point posWs = apNodeDataList[iNode]->m_pathNode.ParentToWorld(apNodeDataList[iNode]->m_pathNodePosPs);
				const Point posPs = pathContext.m_parentSpace.UntransformPoint(posWs);
				workingPathPs.AddWaypoint(posPs, apNodeDataList[iNode]->m_pathNode);
			}

			workingPathPs.AddWaypoint(truncatedGoalPosPs, apNodeDataList[pathLength - 1]->m_pathNode);
		}

		if (buildParams.m_reversePath)
		{
			workingPathPs.Reverse();
		}

		pathLength = workingPathPs.GetWaypointCount();

		if (buildParams.m_truncateAfterTap > 0)
		{
			U32F curTapCount = 0;

			for (I32F iWaypoint = 0; iWaypoint < pathLength - 1; ++iWaypoint)
			{
				if (workingPathPs.GetNodeType(iWaypoint) == NavPathNode::kNodeTypeActionPackEnter)
				{
					NAV_ASSERT(workingPathPs.GetActionPackHandle(iWaypoint).IsValid());

					++curTapCount;

					const ActionPack* pAp = workingPathPs.GetActionPackHandle(iWaypoint).ToActionPack();
					if (pAp && (curTapCount == 1))
					{
						const Point posPs = workingPathPs.GetWaypoint(iWaypoint);
						const Point nextPosPs = workingPathPs.GetWaypoint(iWaypoint + 1);
						const Vector pathDirPs = SafeNormalize(VectorXz(nextPosPs - posPs), kZero);

						const Locator apLocWs = pAp->GetLocatorWs();
						const Locator apLocPs = pathContext.m_parentSpace.UntransformLocator(apLocWs);
						const Vector pathDirLs = apLocPs.UntransformVector(pathDirPs);

						const float angleDeg = AngleFromXZVec(pathDirLs).ToDegrees();

						pPathResults->m_navTapRotationDeg = angleDeg;
					}

					if (curTapCount >= buildParams.m_truncateAfterTap)
					{
						pathLength = iWaypoint + 1;
						workingPathPs.TruncatePath(pathLength);

						break;
					}
				}
			}
		}

		if (pPathResults->m_pNodeData)
		{
			const I32F numOutputNodes = Min(pathLength, I32F(pPathResults->m_maxNodeData));
			for (I32F i = 0; i < numOutputNodes; ++i)
			{
				pPathResults->m_pNodeData[i] = *apNodeDataList[i];
			}
			pPathResults->m_numNodeData = numOutputNodes;
		}

		NavEdge* pEdges = nullptr;
		U32F numEdges = 0;

		if (pathContext.m_pathRadius > kSmallFloat)
		{
			PROFILE(Navigation, BuildPath_Radial);
			PROFILE_ACCUM(NavPathBuildRadial);

			pEdges = NDI_NEW NavEdge[kMaxNavItems];

			numEdges = BuildPotentialEdgeList(pathContext,
											  buildParams,
											  apNodeDataList,
											  initialPathLength,
											  pEdges,
											  kMaxNavItems);

			if (numEdges > 0)
			{
				if (g_navOptions.m_useNewRadialPathBuild)
				{
					NavItemRegistrationTable edgeTable;

					{
						PROFILE(Navigation, CreateEdgeRegistrationTable);

						const size_t edgeTableCount = Max(numEdges * 32, 80ULL);
						const size_t edgeTableSize = NavItemRegistrationTable::DetermineSizeInBytes(edgeTableCount);
						const Memory::Allocator* pTopAllocator = Memory::TopAllocator();

						if (!pTopAllocator || !pTopAllocator->CanAllocate(edgeTableSize, kAlign16))
						{
							// HACK fix out of temp memory. I saw over 200 corners here.
							AllocateJanitor frameAlloc(kAllocSingleFrame, FILE_LINE_FUNC);

							edgeTable.Init(edgeTableCount, FILE_LINE_FUNC);
						}
						else
						{
							edgeTable.Init(edgeTableCount, FILE_LINE_FUNC);
						}

						CreateEdgeRegistrationTable(pEdges, numEdges, pathContext.m_pathRadius, &edgeTable);
					}

					SafeAllocator allocator;

					LinkEdges(pathContext, pEdges, numEdges, edgeTable);
					numEdges = SplitEdges(pathContext, pEdges, numEdges, kMaxNavItems, edgeTable, allocator);

					{
						ProbeAndShadowEdgesContext ctx(pathContext, numEdges, allocator);

						ProbeEdges(ctx, pEdges, numEdges, edgeTable);
						ResolveLinkIntersections(ctx, pEdges, numEdges, edgeTable);
						ShadowEdges(ctx, pEdges, numEdges, edgeTable);
					}

					PushPathAwayFromEdges(pathContext,
										  buildParams,
										  &workingPathPs,
										  edgeTable,
										  pEdges,
										  numEdges);

					workingPathPs = RemoveLoopsFromPath(workingPathPs);
				}
				else
				{
					NavCorner* pCorners = NDI_NEW NavCorner[kMaxNavItems];

					numEdges = EliminateNarrowWedges(pEdges, numEdges, pathContext.m_pathRadius);

					const U32F numCorners = BuildPotentialCornerList(pEdges,
																	 numEdges,
																	 pathContext.m_pathRadius,
																	 pCorners,
																	 kMaxNavItems);

					if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawRadialCorners))
					{
						const DebugPrimTime tt = buildParams.m_debugDrawTime;
						for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
						{
							const NavCorner& c = pCorners[iCorner];

							c.DebugDraw(iCorner, pathContext.m_pathRadius, tt);
						}
					}

					NavItemRegistrationTable cornerTable;

					if (numCorners > 0)
					{
						PROFILE(Navigation, CreateCornerRegistrationTable);

						const size_t cornerTableCount = Max(numCorners * 32, 80ULL);
						const size_t cornerTableSize = NavItemRegistrationTable::DetermineSizeInBytes(cornerTableCount);
						const Memory::Allocator* pTopAllocator = Memory::TopAllocator();

						if (!pTopAllocator || !pTopAllocator->CanAllocate(cornerTableSize, kAlign16))
						{
							// HACK fix out of temp memory. I saw over 200 corners here.
							AllocateJanitor frameAlloc(kAllocSingleFrame, FILE_LINE_FUNC);

							cornerTable.Init(cornerTableCount, FILE_LINE_FUNC);
						}
						else
						{
							cornerTable.Init(cornerTableCount, FILE_LINE_FUNC);
						}

						CreateCornerRegistrationTable(pCorners, numCorners, pathContext.m_pathRadius, &cornerTable);
					}

					PushPathAwayFromFeaturesPs(pathContext, &workingPathPs, cornerTable, pCorners, numCorners);
				}

				if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawRadialEdges))
				{
					const DebugPrimTime tt = buildParams.m_debugDrawTime;
					for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
					{
						const NavEdge& e = pEdges[iEdge];
						e.DebugDraw(pathContext.m_pathRadius, tt);
					}
				}
			}
		}

		if (buildParams.m_finalizePathWithProbes)
		{
			if (pEdges)
			{
				workingPathPs = FinalizePathWithProbesCached(pathContext, buildParams, workingPathPs, pEdges, numEdges);
			}
			else
			{
				workingPathPs = FinalizePathWithProbes(pathContext, buildParams, workingPathPs);
			}
		}

		pPathResults->m_pathWaypointsPs = workingPathPs;

		// calculate some values
		ProcessResults(pPathResults, buildParams, pathContext, config, startPosPs, requestedGoalPosPs);

		if (FALSE_IN_FINAL_BUILD(buildParams.m_debugDrawResults))
		{
			workingPathPs.DebugDraw(pathContext.m_parentSpace,
									true,
									pathContext.m_pathRadius,
									PathWaypointsEx::ColorScheme(),
									buildParams.m_debugDrawTime);
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void BuildPathFast(const PathFindNodeData& searchData,
					   const NavNodeKey goalKey,
					   Point_arg goalPosWs,
					   BuildPathFastResults* pPathResults)
	{
		PROFILE_AUTO(AI);
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
		NAV_ASSERT(pPathResults);

		const bool useTrivialHashTable = searchData.m_trivialHashVisitedNodes;
		const TrivialHashNavNodeData* const pTrivialNodeTable = useTrivialHashTable ? searchData.m_trivialHashVisitedNodes : nullptr;
		const NavNodeTable* const pNodeTable = useTrivialHashTable ? nullptr : &searchData.m_visitedNodes;

		NavNodeTable::ConstIterator gItr;
		NavPathNode::NodeId gI = goalKey.GetPathNodeId();
		if (!useTrivialHashTable)
		{
			gItr = pNodeTable->Find(goalKey);
		}

		if (useTrivialHashTable)
		{
			if (gI > NavPathNodeMgr::kMaxNodeCount || !pTrivialNodeTable[gI].IsValid())
				return;
		}
		else
		{
			if (gItr == pNodeTable->End())
				return;
		}

		bool usesTap = false;
		bool initialPathTowardsGoal = false;
		bool initialPathForward = false;

		// build our list backwards from our desired goal
		NavNodeKey key = goalKey;
		Point curPosPs = kInvalidPoint;
		Point prevPosPs = kInvalidPoint;

		while (key.GetPathNodeId() < NavPathNodeMgr::kMaxNodeCount)
		{
			if (useTrivialHashTable)
			{
				const NavPathNode::NodeId i = key.GetPathNodeId();
				const TrivialHashNavNodeData& data = pTrivialNodeTable[i];
				if (!data.IsValid())
					break;

				if (data.m_nodeType == NavPathNode::kNodeTypeActionPackEnter || data.m_nodeType == NavPathNode::kNodeTypeActionPackExit)
					usesTap = true;

				prevPosPs = curPosPs;
				curPosPs = data.m_posPs;
				key = NavNodeKey(data.m_iParent, 0);
			}
			else
			{
				const NavNodeTable::ConstIterator itr = pNodeTable->Find(key);
				if (itr == pNodeTable->End())
					break;
				const NavNodeData& d = itr->m_data;
				const NavPathNodeProxy& node = d.m_pathNode;

				if (node.IsActionPackNode())
				{
					usesTap = true;
				}

				prevPosPs = curPosPs;
				curPosPs = d.m_pathNodePosPs;
				key = d.m_parentNode;
			}
		}

		if (!AllComponentsEqual(curPosPs, kInvalidPoint) && !AllComponentsEqual(prevPosPs, kInvalidPoint)
			&& !AllComponentsEqual(prevPosPs, curPosPs))
		{
			const PathFindContext& pathContext = searchData.m_context;
			const Point goalPosPs = pathContext.m_parentSpace.UntransformPoint(goalPosWs);
			const Point startPosPs = curPosPs;
			const Point firstWaypointPs = prevPosPs;

			const Vector ownerDirPs = GetLocalZ(pathContext.m_ownerLocPs.GetRotation());
			const Vector initialPathDirPs = SafeNormalize(firstWaypointPs - startPosPs, kUnitZAxis);
			const Vector toGoalDirPs = SafeNormalize(goalPosPs - startPosPs, kUnitZAxis);
			const F32 kDotThreshold = Cos(DegreesToRadians(37.0f));

			initialPathTowardsGoal = (DotXz(initialPathDirPs, toGoalDirPs) > kDotThreshold);
			initialPathForward = (DotXz(initialPathDirPs, ownerDirPs) > kDotThreshold);
		}

		pPathResults->m_usesTap = usesTap;
		pPathResults->m_initialPathTowardsGoal = initialPathTowardsGoal;
		pPathResults->m_initialPathForward = initialPathForward;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void SmoothPathApproximate(U32F pathLength,
									  PathWaypointsEx& workingPathPs,
									  const NavNodeData** apNodeDataList,
									  Point truncatedGoalPosPs)
	{
		PROFILE_ACCUM(AiApproxPathSmoothing);

		// add start point always
		if (pathLength)
		{
			workingPathPs.AddWaypoint(apNodeDataList[0]->m_pathNodePosPs, apNodeDataList[0]->m_pathNode);
		}

		if (pathLength >= 3)
		{
			I32 iP = 0;
			for (; iP < pathLength - 1; ++iP)
			{
				// if P is a navmesh node, we've found our P
				if (apNodeDataList[iP]->m_pathNode.IsNavMeshNode())
					break;

				// if P is not the start node (already added) and is not a navmesh node, add directly to output, unmodified.
				if (iP)
				{
					workingPathPs.AddWaypoint(apNodeDataList[iP]->m_pathNodePosPs, apNodeDataList[iP]->m_pathNode);
				}
			}

			I32 iM = iP + 1;

			Point P = apNodeDataList[iP]->m_pathNodePosPs;
			Point M = apNodeDataList[iM]->m_pathNodePosPs;

			bool hasEntryEdge = false;
			Point exitVert1;
			Point exitVert2;

			//////////////// main loop
			for (I32 iQ = iM + 1; iQ < pathLength; ++iQ)
			{
				const NavPolyHandle hMPoly = apNodeDataList[iM]->m_pathNode.GetNavPolyHandle();
				const Point Q = apNodeDataList[iQ]->m_pathNodePosPs;

				if (apNodeDataList[iM]->m_pathNode.IsNavMeshNode())
				{
					// everything in here is the very VERY hot loop. gentlemen, count your cycles.

					const NavPolyHandle hQPoly = apNodeDataList[iQ]->m_pathNode.GetNavPolyHandle();

					// we need to advance Q until it is different from M and (is the last entry OR is different from what comes after it)
					if (hMPoly == hQPoly || (iQ < (I32)pathLength - 1 && hQPoly == apNodeDataList[iQ + 1]->m_pathNode.GetNavPolyHandle()))
					{
						if (!apNodeDataList[iQ]->m_pathNode.IsNavMeshNode())
						{
							// Q is a node we can't touch, advance working set to (M, Q, new node)
							P = M;
							M = Q;
							iM = iQ;

							// and discard cached edge because we haven't updated it this iteration
							hasEntryEdge = false;
						}

						continue;
					}

					const NavPoly* Mpoly = hMPoly.ToNavPoly();
					const Vector localToParent = AsVector(Mpoly->GetNavMesh()->GetOriginPs().GetPosition());

					// vert order is flipped since it's the same edge but from the perspective of the
					// adjacent poly, which has the same winding order
					const Point enterVert1 = exitVert2;
					const Point enterVert2 = exitVert1;

					// check all edges to see if any edge's adjacent poly is Q
					//
					// no matching edge occurs when M is a pre-link and Q is the link, except the poly we have
					// for it is the pre-link of the OTHER navmesh. We could correct for this and thereby
					// ensure that every iteration has an exit edge, but it's slower, and actually slightly
					// decreases average path quality because of how long and thin link polys are
					// (therefore their centroids are not very representative of most paths through them)
					bool hasExitEdge = false;
					{
						const __m128i q = _mm_shufflelo_epi16(_mm_cvtsi32_si128(*reinterpret_cast<const int*>(hQPoly.GetPointerToPolyId())), 0);
						const __m128i v = _mm_loadu_si64(Mpoly->GetAdjacencyArray());
						const __m128i cmp = _mm_cmpeq_epi16(q, v);
						const U32 msk = (U32)_mm_movemask_epi8(cmp) & 0xFF;
						if (msk)
						{
							const U32 iVert = (U32)CountTrailingZeros_U32(msk) >> 1;
							hasExitEdge = true;
							exitVert1 = localToParent + Mpoly->GetVertex(iVert);
							exitVert2 = localToParent + Mpoly->GetNextVertex(iVert);
						}
					}

					// #define DEBUG_APPROX_SMOOTH_BUILD_PATH

#ifdef DEBUG_APPROX_SMOOTH_BUILD_PATH
					CONST_EXPR int iDebugQ = 4;
					if (iQ == iDebugQ)
					{
						g_prim.Draw(DebugSphere(P, 0.06f, kColorRed), kPrimDuration1Frame);
						g_prim.Draw(DebugSphere(M, 0.06f, kColorOrange), kPrimDuration1Frame);
						g_prim.Draw(DebugSphere(Q, 0.06f, kColorYellow), kPrimDuration1Frame);

						if (hasEntryEdge)
						{
							g_prim.Draw(DebugLine(enterVert1, enterVert2, kColorWhite, kColorWhite, 4.0f), kPrimDuration1Frame);
						}

						if (hasExitEdge)
						{
							g_prim.Draw(DebugLine(exitVert1, exitVert2, kColorBlue, kColorBlue, 4.0f), kPrimDuration1Frame);
						}
					}
#endif // DEBUG_APPROX_SMOOTH_BUILD_PATH

					bool fixup = false;
					Point origP = P;

					// try to fixup entry of P->Q
					if (hasEntryEdge && !FastPointEqual(P, enterVert1) && !FastPointEqual(P, enterVert2))
					{
						const Vector t = enterVert2 - enterVert1;
						const __m128 d = (enterVert1 - P).QuadwordValue();
						const __m128 s = (Q - P).QuadwordValue();
						const __m128 sshuf = _mm_shuffle_ps(s, s, 198);
						const __m128 ds = _mm_mul_ps(d, sshuf);
						const __m128 numshuf = _mm_shuffle_ps(ds, ds, 198);
						const __m128 num = _mm_sub_ss(ds, numshuf);
						const __m128 ts = _mm_mul_ps(t.QuadwordValue(), sshuf);
						const __m128 denshuf = _mm_shuffle_ps(ts, ts, 198);
						const __m128 den = _mm_sub_ss(denshuf, ts);
						const float tq = _mm_cvtss_f32(_mm_mul_ss(num, _mm_rcp_ss(den)));

						CONST_EXPR float kThresh = 0.08f;
						CONST_EXPR float kInvThresh = 1.0f - kThresh;

						if (tq != Min(Max(tq, kThresh), kInvThresh))
						{
							const __m128 MQ = (Q - M).QuadwordValue();
							const __m128 MP = (P - M).QuadwordValue();
							const __m128 MV = ((enterVert1 + Min(Max(tq, 0.0f), 1.0f) * t) - M).QuadwordValue();
							const __m128 MPshuf = _mm_shuffle_ps(MP, MP, 198);
							const __m128 MVMP = _mm_mul_ps(MV, MPshuf);
							const __m128 crossPVshuf = _mm_shuffle_ps(MVMP, MVMP, 198);
							const __m128 crossPV = _mm_sub_ss(MVMP, crossPVshuf);
							const __m128 MQMP = _mm_mul_ps(MQ, MPshuf);
							const __m128 crossPQshuf = _mm_shuffle_ps(MQMP, MQMP, 198);
							const __m128 crossPQ = _mm_sub_ss(crossPQshuf, MQMP);
							const __m128 MVshuf = _mm_shuffle_ps(MV, MV, 198);
							const __m128 MQMV = _mm_mul_ps(MQ, MVshuf);
							const __m128 crossQVshuf = _mm_shuffle_ps(MQMV, MQMV, 198);
							const __m128 crossQV = _mm_sub_ss(MQMV, crossQVshuf);
							const __m128 msk = _mm_xor_ps(_mm_and_ps(_mm_xor_ps(crossPV, crossPQ), _mm_xor_ps(crossQV, crossPQ)), _mm_set_ss(tq - kInvThresh));
							P = Point(_mm_blendv_ps(enterVert1.QuadwordValue(), enterVert2.QuadwordValue(), _mm_shuffle_ps(msk, msk, 0)));

							fixup = true;
						}
					}

					// current exit edge will become our entry edge next iteration
					hasEntryEdge = hasExitEdge;

					// try to fixup exit of P->Q (where P may have been updated)
					if (hasExitEdge && !FastPointEqual(P, exitVert1) && !FastPointEqual(P, exitVert2))
					{
						const Vector t = exitVert2 - exitVert1;
						const __m128 d = (exitVert1 - P).QuadwordValue();
						const __m128 s = (Q - P).QuadwordValue();
						const __m128 sshuf = _mm_shuffle_ps(s, s, 198);
						const __m128 ds = _mm_mul_ps(d, sshuf);
						const __m128 numshuf = _mm_shuffle_ps(ds, ds, 198);
						const __m128 num = _mm_sub_ss(ds, numshuf);
						const __m128 ts = _mm_mul_ps(t.QuadwordValue(), sshuf);
						const __m128 denshuf = _mm_shuffle_ps(ts, ts, 198);
						const __m128 den = _mm_sub_ss(denshuf, ts);
						const float tq = _mm_cvtss_f32(_mm_mul_ss(num, _mm_rcp_ss(den)));
						const float clampedTq = Min(Max(tq, 0.0f), 1.0f);

						if (tq != clampedTq)
						{
							const __m128 msk = _mm_set1_ps(clampedTq - 1.0f);
							const Point v1 = Point(_mm_blendv_ps(exitVert2.QuadwordValue(), exitVert1.QuadwordValue(), msk));

							if (fixup)
							{
								// we had an entry fixup. if origP->v1 goes through the
								// entry edge, we don't need it, so replace it. Otherwise,
								// commit it.

								const Vector enterT = enterVert2 - enterVert1;
								const __m128 enterD = (enterVert1 - origP).QuadwordValue();
								const __m128 enterS = (v1 - origP).QuadwordValue();
								const __m128 enterSshuf = _mm_shuffle_ps(enterS, enterS, 198);
								const __m128 enterDs = _mm_mul_ps(enterD, enterSshuf);
								const __m128 enterNumshuf = _mm_shuffle_ps(enterDs, enterDs, 198);
								const __m128 enterNum = _mm_sub_ss(enterDs, enterNumshuf);
								const __m128 enterTs = _mm_mul_ps(enterT.QuadwordValue(), enterSshuf);
								const __m128 enterDenshuf = _mm_shuffle_ps(enterTs, enterTs, 198);
								const __m128 enterDen = _mm_sub_ss(enterDenshuf, enterTs);
								const float enterTq = _mm_cvtss_f32(_mm_mul_ss(enterNum, _mm_rcp_ss(enterDen)));
								if (enterTq != Min(Max(enterTq, 0.0f), 1.0f))
								{
									// doesn't go through entry edge, so commit entry fixup
									workingPathPs.AddWaypoint(P, apNodeDataList[iM]->m_pathNode);
								}
							}
							// fixup is now v1 (exit)
							P = v1;
							fixup = true;
						}
					}

					if (fixup)
					{
#ifdef DEBUG_APPROX_SMOOTH_BUILD_PATH
						if (iQ == iDebugQ)
						{
							g_prim.Draw(DebugLine(P, Vector(0.0f, 1.0f, 0.0f), kColorCyan, kColorCyan, 6.0f), kPrimDuration1Frame);
						}
#endif // DEBUG_APPROX_SMOOTH_BUILD_PATH

						workingPathPs.AddWaypoint(P, apNodeDataList[iM]->m_pathNode);
					}
				}
				else
				{
					// if we encounter a node we can't touch for M, we have (P, M, Q) and we want to commit M
					// and advance the working set to (M, Q, new node)
					workingPathPs.AddWaypoint(M, apNodeDataList[iM]->m_pathNode);
					P = M;

					// and discard cached edge because we haven't updated it this iteration
					hasEntryEdge = false;
				}
				M = Q;
				iM = iQ;
			} //////////////// end of main loop
		}

		// add the end
		workingPathPs.AddWaypoint(truncatedGoalPosPs, apNodeDataList[pathLength - 1]->m_pathNode);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void CreateBuildPathParamsDevMenu(DMENU::Menu* pMenu, BuildPathParams* pParamsToUse)
	{
		if (!pMenu || !pParamsToUse)
			return;

		pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Truncate After TAP Count", &pParamsToUse->m_truncateAfterTap, DMENU::IntRange(0, 64), DMENU::IntSteps(1, 10)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Wide TAP Instance Seed", &pParamsToUse->m_wideTapInstanceSeed, DMENU::IntRange(-1, 512), DMENU::IntSteps(1, 10)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Portal Shrink", &pParamsToUse->m_portalShrink, DMENU::FloatRange(0.0f, 3.0f), DMENU::FloatSteps(0.1f, 1.0f)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("Smooth Path Find", s_smoothPathFindFilters, DMENU::EditInt, &pParamsToUse->m_smoothPath));

		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Finalize Path Find With Probes", &pParamsToUse->m_finalizePathWithProbes));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Finalize Path Find Min Dist", &pParamsToUse->m_finalizeProbeMinDist, DMENU::FloatRange(-1.0f, 1000.0f), DMENU::FloatSteps(1.0f, 10.0f)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Finalize Path Find Max Dist", &pParamsToUse->m_finalizeProbeMaxDist, DMENU::FloatRange(-1.0f, 1000.0f), DMENU::FloatSteps(1.0f, 10.0f)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Finalize Path Find Max Duration (ms)", &pParamsToUse->m_finalizeProbeMaxDurationMs, DMENU::FloatRange(-1.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("AP Entry Distance", &pParamsToUse->m_apEntryDistance, DMENU::FloatRange(-1.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));

		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Reverse Path", &pParamsToUse->m_reversePath));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Results", &pParamsToUse->m_debugDrawResults));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Portals", &pParamsToUse->m_debugDrawPortals));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Radial Edges", &pParamsToUse->m_debugDrawRadialEdges));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Radial Corners", &pParamsToUse->m_debugDrawRadialCorners));
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugPrintBuildPathParams(const BuildPathParams& params,
								   MsgOutput output,
								   const char* preamble /* = "params" */)
	{
		STRIP_IN_FINAL_BUILD;

		// build params
		PrintTo(output, "%s.m_portalShrink = %ff;\n", preamble, params.m_portalShrink);
		PrintTo(output, "%s.m_finalizeProbeMinDist = %ff;\n", preamble, params.m_finalizeProbeMinDist);
		PrintTo(output, "%s.m_finalizeProbeMaxDist = %ff;\n", preamble, params.m_finalizeProbeMaxDist);
		PrintTo(output, "%s.m_finalizeProbeMaxDurationMs = %ff;\n", preamble, params.m_finalizeProbeMaxDurationMs);
		PrintTo(output, "%s.m_apEntryDistance = %ff;\n", preamble, params.m_apEntryDistance);

		PrintTo(output,
				"%s.m_smoothPath = Nav::%s;\n",
				preamble,
				params.m_smoothPath == kNoSmoothing
					? "kNoSmoothing"
					: params.m_smoothPath == kFullSmoothing
						  ? "kFullSmoothing"
						  : params.m_smoothPath == kApproxSmoothing
								? "kApproxSmoothing"
								: "<unrecognized smoothing option>");
		PrintTo(output, "%s.m_reversePath = %s;\n", preamble, params.m_reversePath ? "true" : "false");
		PrintTo(output, "%s.m_finalizePathWithProbes = %s;\n", preamble, params.m_finalizePathWithProbes ? "true" : "false");
		PrintTo(output, "%s.m_truncateAfterTap = %ull;\n", preamble, params.m_truncateAfterTap);
		PrintTo(output, "%s.m_wideTapInstanceSeed = %ull;\n", preamble, params.m_wideTapInstanceSeed);
		PrintTo(output, "%s.m_debugDrawPortals = %s;\n", preamble, params.m_debugDrawPortals ? "true" : "false");

		for (U32 exposureParamsNum = 0; exposureParamsNum < BuildPathParams::kNumExposureResults; exposureParamsNum++)
		{
			const DC::ExposureSourceMask& exposureParams = params.m_exposureParams[exposureParamsNum];
			PrintTo(output, "%s.m_exposureParams[%d] = 0x%x;\n", preamble, exposureParams, exposureParamsNum);
		}
	}
} // namespace Nav
