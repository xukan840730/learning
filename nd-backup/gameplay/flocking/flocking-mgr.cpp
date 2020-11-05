/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/flocking/flocking-mgr.h"

#include "corelib/math/aabb.h"
#include "corelib/math/intersection.h"
#include "corelib/math/segment-util.h"
#include "corelib/system/atomic-lock.h"
#include "corelib/util/bigsort.h"
#include "corelib/util/bit-array.h"
#include "corelib/util/random.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/flocking/flocking-config.h"
#include "gamelib/gameplay/flocking/flocking-agent.h"
#include "gamelib/gameplay/flocking/flocking-obstacle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/region/region-manager.h"

namespace Flocking
{
	class WanderRegionDesc
	{
	public:
		FlockingAgent* m_pAgent;
		WanderRegion* m_pWanderRegion;
	};

	struct CompareWanderRegions
	{
		int operator() (const WanderRegionDesc& desc0, const WanderRegionDesc& desc1) const
		{
			GAMEPLAY_ASSERT(desc0.m_pAgent == desc1.m_pAgent);
			const Point pos = desc0.m_pAgent->GetPosition();

			Point closestPt0;
			Point closestPt1;
			const bool isInside0 = desc0.m_pWanderRegion->m_pRegion->IsInsideWithClosest(pos, 0.0f, &closestPt0);
			const bool isInside1 = desc1.m_pWanderRegion->m_pRegion->IsInsideWithClosest(pos, 0.0f, &closestPt1);

			if (isInside0 != isInside1)
			{
				return isInside0 ? -1 : 1;
			}
			else
			{
				const float distXzSqr0 = DistXzSqr(pos, closestPt0);
				const float distXzSqr1 = DistXzSqr(pos, closestPt1);
				return (distXzSqr0 > distXzSqr1) - (distXzSqr1 > distXzSqr0);
			}
		}
	};

	class AgentDesc
	{
	public:
		FlockingAgent* m_pAgent;
		float m_dist;
	};

	struct CompareAgents
	{
		int operator() (const AgentDesc& desc0, const AgentDesc& desc1) const
		{
			return (desc0.m_dist > desc1.m_dist) - (desc1.m_dist > desc0.m_dist);
		}
	};

	static int QueueIncrement(int idx, int queueArraySize)
	{
		NAV_ASSERT(queueArraySize > 0);
		NAV_ASSERT(idx >= 0 && idx < queueArraySize);

		return (idx + 1) % queueArraySize;
	}

	class FlockManager2 : public RegionManager::Observer
	{
		FlockingAgent m_agents[kMaxFlockingAgents];
		ExternalBitArray m_agentMask;
		U64 m_agentMaskBuffer[kMaxFlockingAgents / 64];

		WanderRegion m_regions[kMaxFlockingRegions];
		ExternalBitArray m_regionMask;
		U64 m_regionMaskBuffer[kMaxFlockingRegions / 64];

		FlockingRvoObstacle m_rvoObstacles[kMaxFlockingRvoObstacles];
		ExternalBitArray m_rvoObstacleMask;
		U64 m_rvoObstacleMaskBuffer[kMaxFlockingRvoObstacles / 64];

	public:
		FlockManager2() : m_inited(false)
		{
		}

		void Init();

		void Shutdown()
		{
			GAMEPLAY_ASSERT(m_inited);
			
			g_regionManager.UnregisterObserver(this);
			m_inited = false;
		}

		FlockingAgent* AddFlockingAgent(const Point_arg initialAgentPos, const Vector_arg initialAgentForward, const StringId64 flockingParamsId)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_agentLock, FILE_LINE_FUNC);

			const U64 idx = m_agentMask.FindFirstClearBit();
			GAMEPLAY_ASSERTF(idx != kBitArrayInvalidIndex, ("FlockingMgr: flocking agent buffer full: %d\n", kMaxFlockingAgents));

			m_agents[idx].Init(idx, initialAgentPos, initialAgentForward, flockingParamsId);
			m_agentMask.SetBit(idx);

			return &m_agents[idx];
		}

		void RemoveFlockingAgent(FlockingAgent *const pAgent)
		{
			GAMEPLAY_ASSERT(m_inited);
			GAMEPLAY_ASSERT(pAgent);

			AtomicLockJanitor jj(&m_agentLock, FILE_LINE_FUNC);

			U64 idx = kBitArrayInvalidIndex;
			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				if (pAgent == &m_agents[i])
				{
					idx = i;
					break;
				}
			}

			GAMEPLAY_ASSERT(idx != kBitArrayInvalidIndex);
			m_agentMask.ClearBit(idx);
		}

		void FindAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
		{
			GAMEPLAY_ASSERT(m_inited);

			outNumAgents = 0;

			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];
				if (pAgent->IsEnabled())
				{
					outAgents[outNumAgents++] = pAgent;
				}
			}
		}

		void FindSelectedFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
		{
			GAMEPLAY_ASSERT(m_inited);

			outNumAgents = 0;

			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];

				if (pAgent->GetSelected())
				{
					outAgents[outNumAgents++] = pAgent;
				}
			}
		}

		void SelectAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
		{
			GAMEPLAY_ASSERT(m_inited);

			outNumAgents = 0;

			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];

				pAgent->SetSelected(true);
				outAgents[outNumAgents++] = pAgent;
			}
		}

		void SelectFlockingAgents(Point_arg startPtWs, Point_arg endPtWs, FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
		{
			GAMEPLAY_ASSERT(m_inited);

			const Point startPtXzWs = Point(startPtWs.X(), 0.0f, startPtWs.Z());
			const Point endPtXzWs = Point(endPtWs.X(), 0.0f, endPtWs.Z());

			Aabb selectionRect;
			selectionRect.IncludePoint(startPtXzWs);
			selectionRect.IncludePoint(endPtXzWs);

			outNumAgents = 0;

			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];

				const Point posWs = pAgent->GetPosition();
				const Point posWsXz = Point(posWs.X(), 0.0f, posWs.Z());

				pAgent->SetSelected(selectionRect.ContainsPoint(posWsXz));
			}
		}

		void SetRegionEnabled(const StringId64 regionNameId, bool isEnabled)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_regionLock, FILE_LINE_FUNC);

			for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
			{
				WanderRegion& wanderRegion = m_regions[i];
				if (wanderRegion.m_pRegion->GetNameId() == regionNameId)
				{
					wanderRegion.m_isEnabled = isEnabled;
					return;
				}
			}
		}

		bool IsRegionBlocked(const StringId64 regionNameId)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_regionLock, FILE_LINE_FUNC);

			for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
			{
				const WanderRegion& wanderRegion = m_regions[i];
				if (wanderRegion.m_pRegion->GetNameId() == regionNameId)
				{
					return wanderRegion.m_isBlocked;
				}
			}

			return false;
		}

		const WanderRegion* GetRegionByNameId(const StringId64 regionNameId)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_regionLock, FILE_LINE_FUNC);

			for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
			{
				const WanderRegion& wanderRegion = m_regions[i];
				if (wanderRegion.m_pRegion->GetNameId() == regionNameId)
				{
					return &wanderRegion;
				}
			}

			return nullptr;
		}

		FlockingRvoObstacle* AddFlockingRvoObstacle(const Point_arg p0, const Point_arg p1, const NavMesh *const pNavMesh)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_obstacleLock, FILE_LINE_FUNC);

			const U64 idx = m_rvoObstacleMask.FindFirstClearBit();
			GAMEPLAY_ASSERTF(idx != kBitArrayInvalidIndex, ("FlockingMgr: flocking obstacle buffer full: %d\n", kMaxFlockingRvoObstacles));

			m_rvoObstacles[idx].Init(p0, p1, pNavMesh);
			m_rvoObstacleMask.SetBit(idx);

			return &m_rvoObstacles[idx];
		}

		void RemoveFlockingRvoObstacle(const NavMesh *const pNavMesh)
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_obstacleLock, FILE_LINE_FUNC);

			for (U64 i = m_rvoObstacleMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_rvoObstacleMask.FindNextSetBit(i))
			{
				if (m_rvoObstacles[i].m_pNavMesh == pNavMesh)
				{
					m_rvoObstacleMask.ClearBit(i);
				}
			}
		}

		void ClearFlockingRvoObstacles()
		{
			GAMEPLAY_ASSERT(m_inited);

			AtomicLockJanitor jj(&m_obstacleLock, FILE_LINE_FUNC);

			m_rvoObstacleMask.ClearAllBits();
		}

		void FindAllFlockingRvoObstacles(FlockingRvoObstacle* outRvoObstacles[kMaxFlockingRvoObstacles], U32F& outNumRvoObstacles)
		{
			GAMEPLAY_ASSERT(m_inited);

			outNumRvoObstacles = 0;

			for (U64 i = m_rvoObstacleMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_rvoObstacleMask.FindNextSetBit(i))
			{
				FlockingRvoObstacle *const pRvoObstacle = &m_rvoObstacles[i];
				outRvoObstacles[outNumRvoObstacles++] = pRvoObstacle;
			}
		}

		void StepSimulation(const Point_arg playerPos)
		{
			GAMEPLAY_ASSERT(m_inited);

			if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
			{
				const U32F numAgents = m_agentMask.CountSetBits();
				const U32F numRegions = m_regionMask.CountSetBits();
				const U32F numRvoObstacles = m_rvoObstacleMask.CountSetBits();

				MsgConPauseable("Flocking Sim: %d/%d agents, %d/%d regions, %d/%d rvo obstacles - %d\n",
								numAgents,
								kMaxFlockingAgents,
								numRegions,
								kMaxFlockingRegions,
								numRvoObstacles,
								kMaxFlockingRvoObstacles,
								EngineComponents::GetNdFrameState()->m_gameFrameNumber);

				for (U64 i = m_rvoObstacleMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_rvoObstacleMask.FindNextSetBit(i))
				{
					FlockingRvoObstacle *const pRvoObstacle = &m_rvoObstacles[i];
					pRvoObstacle->DebugDraw(i);
				}

				for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
				{
					FlockingAgent *const pAgent = &m_agents[i];
					pAgent->DebugDraw();
				}
			}

			AssignWanderRegions(playerPos);

			if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation && 
									g_flockingConfig.m_debugRegions))
			{
				for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
				{
					const WanderRegion& wanderRegion = m_regions[i];
					const Region *const pRegion = wanderRegion.m_pRegion;

					Point centroid;
					pRegion->DetermineCentroid(centroid);

					Color drawColor;
					if (!wanderRegion.m_isEnabled)
					{
						drawColor = kColorGrayTrans;
					}
					else if (wanderRegion.m_isBlocked)
					{
						drawColor = kColorRedTrans;
					}
					else if (wanderRegion.m_population < wanderRegion.m_maxPopulation)
					{
						drawColor = kColorGreenTrans;
					}
					else
					{
						drawColor = kColorOrangeTrans;
					}

					g_prim.Draw(DebugString(centroid, StringBuilder<32>("rgn %d: %d/%d", i, wanderRegion.m_population, wanderRegion.m_maxPopulation).c_str(), drawColor, 0.6f), kPrimDuration1FramePauseable);
					pRegion->DebugDraw(kPrimDuration1FramePauseable, drawColor, false);
				}
			}

			ResolveQueueMoveTo();
		}

		virtual void OnRegionLogin(const Region* pRegion) override
		{
			GAMEPLAY_ASSERT(m_inited);
			GAMEPLAY_ASSERT(pRegion);

			const bool isFlockingWander = pRegion->GetTagData(SID("is-flocking-wander"), false);
			if (isFlockingWander)
			{
				AtomicLockJanitor jj(&m_regionLock, FILE_LINE_FUNC);

				const U64 idx = m_regionMask.FindFirstClearBit();
				GAMEPLAY_ASSERTF(idx != kBitArrayInvalidIndex, ("FlockingMgr: flocking region buffer full: %d\n", kMaxFlockingRegions));

				m_regions[idx].Init(pRegion);
				m_regionMask.SetBit(idx);
			}
		}

		virtual void OnRegionLogout(const Region* pRegion) override
		{
			GAMEPLAY_ASSERT(m_inited);
			GAMEPLAY_ASSERT(pRegion);

			const bool isFlockingWander = pRegion->GetTagData(SID("is-flocking-wander"), false);
			if (isFlockingWander)
			{
				AtomicLockJanitor jj(&m_regionLock, FILE_LINE_FUNC);

				U64 idx = kBitArrayInvalidIndex;
				for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
				{
					if (pRegion == m_regions[i].m_pRegion)
					{
						idx = i;
						break;
					}
				}

				GAMEPLAY_ASSERT(idx != kBitArrayInvalidIndex);
				m_regionMask.ClearBit(idx);
			}
		}

	private:
		void AssignWanderRegions(const Point_arg playerPos)
		{
			const bool isPlayerPosValid = !AllComponentsEqual(playerPos, kInvalidPoint);
			
			Sphere playerThreatSphere;
			if (isPlayerPosValid)
			{
				float playerThreatRadius = 0.0f;
				if (m_agentMask.AreAllBitsClear())
				{
					playerThreatRadius = FlockingAgent::kPlayerThreatRadius;
				}
				else
				{
					const U64 i = m_agentMask.FindFirstSetBit();
					FlockingAgent *const pAgent = &m_agents[i];
					playerThreatRadius = pAgent->GetParams()->m_playerThreatRadius;
				}

				playerThreatSphere = Sphere(playerPos, playerThreatRadius);
				if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation &&
										g_flockingConfig.m_debugRegions))
				{
					g_prim.Draw(DebugSphere(playerThreatSphere, kColorRedTrans), kPrimDuration1FramePauseable);
				}
			}

			for (U64 i = m_regionMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_regionMask.FindNextSetBit(i))
			{
				WanderRegion& wanderRegion = m_regions[i];
				wanderRegion.m_population = 0;
				
				if (isPlayerPosValid)
				{	// Player / threat blocking region
					Aabb regionAabb;
					wanderRegion.m_pRegion->DetermineAabbWs(regionAabb);

					wanderRegion.m_isBlocked = IntersectSphereAabb(playerThreatSphere, regionAabb);
				}
				else
				{
					wanderRegion.m_isBlocked = false;
				}
			}

			WanderRegionDesc descs[kMaxFlockingRegions];
			U64 numDescs;
			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];
			
				const Region* pFoundRegion = nullptr;

				if (pAgent->IsEnabled() && 
					pAgent->GetState() == Flocking::kNatural)
				{
					const StringId64 assignedWanderRegionId = pAgent->GetAssignedWanderRegion();
					if (assignedWanderRegionId != INVALID_STRING_ID_64)
					{	// See if the assigned region is available
						for (U64 j = m_regionMask.FindFirstSetBit(); j != kBitArrayInvalidIndex; j = m_regionMask.FindNextSetBit(j))
						{
							WanderRegion& wanderRegion = m_regions[j];
							if (wanderRegion.m_pRegion->GetNameId() == assignedWanderRegionId && 
								wanderRegion.m_isEnabled &&
								(pAgent->GetIgnorePlayerRegionBlocking() || !wanderRegion.m_isBlocked))
							{
								pFoundRegion = wanderRegion.m_pRegion;
								break;
							}
						}
					}

					if (!pFoundRegion)
					{	// Try to find the closest wander region available
						numDescs = 0;
						for (U64 j = m_regionMask.FindFirstSetBit(); j != kBitArrayInvalidIndex; j = m_regionMask.FindNextSetBit(j))
						{
							WanderRegion *const pWanderRegion = &m_regions[j];
							if (pWanderRegion->m_isEnabled &&
								(pAgent->GetIgnorePlayerRegionBlocking() || !pWanderRegion->m_isBlocked))
							{
								descs[numDescs].m_pAgent = pAgent;
								descs[numDescs].m_pWanderRegion = pWanderRegion;
								numDescs++;
							}
						}
						QuickSort(descs, numDescs, CompareWanderRegions());

						for (U64 k = 0; k < numDescs; k++)
						{
							WanderRegion *const pWanderRegion = descs[k].m_pWanderRegion;
							if (pWanderRegion->m_population < pWanderRegion->m_maxPopulation)
							{
								pFoundRegion = pWanderRegion->m_pRegion;
								pWanderRegion->m_population++;
								break;
							}
						}
					}
				}

				pAgent->SetWanderRegion(pFoundRegion);
			}
		}

		void ResolveQueueMoveTo()
		{
			float maxY = FLT_MIN;
			
			U32F numAgents = 0;
			Flocking::FlockingAgent* ppAgents[kMaxFlockingAgents];
			for (U64 i = m_agentMask.FindFirstSetBit(); i != kBitArrayInvalidIndex; i = m_agentMask.FindNextSetBit(i))
			{
				FlockingAgent *const pAgent = &m_agents[i];

				if (!pAgent->IsEnabled() ||
					pAgent->GetState() != kQueueMoveTo || 
					(pAgent->GetQueueMoveToBehavior() == kLoose && !pAgent->GetQueueMoveToSpline()))
				{
					continue;
				}

				const Point posWs = pAgent->GetPosition();
				maxY = Max(maxY, posWs.Y());
				ppAgents[numAgents++] = pAgent;
			}

			if (!numAgents)
			{
				return;
			}

			const float kDrawOffsetY = 1.1f;
			maxY += kDrawOffsetY;

			ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

			const U32F numBits = numAgents * numAgents;
			const U32F numBlocks = ExternalBitArray::DetermineNumBlocks(numBits);
			U64 *const pBlocks = NDI_NEW U64[numBlocks];

			ExternalBitArray adjMatrix;
			adjMatrix.Init(numBits, pBlocks);

			{	// Build adjacency matrix
				adjMatrix.ClearAllBits();

				for (U32F i = 0; i < numAgents; i++)
				{
					const FlockingAgent *const pAgent = ppAgents[i];

					const float combinedR = pAgent->GetVisionParams()->m_groupNeighborRadius + pAgent->GetVisionParams()->m_groupNeighborRadius;
					const float combinedRr = combinedR * combinedR;

					const Point pos = pAgent->GetPosition();
					const Point posXz = Point(pos.X(), 0.0f, pos.Z());

					for (U32F j = 0; j < numAgents; j++)
					{
						const U32F matrixIdx = i * numAgents + j;
						if (i == j || 
							adjMatrix.IsBitSet(matrixIdx))
						{
							continue;
						}

						const FlockingAgent *const pOtherAgent = ppAgents[j];

						const Point otherPos = pOtherAgent->GetPosition();
						const Point otherPosXz = Point(otherPos.X(), 0.0f, otherPos.Z());

						const float distXzSqr = DistSqr(posXz, otherPosXz);
						if (distXzSqr < combinedRr)
						{
							adjMatrix.SetBit(matrixIdx);

							const U32F reverseMatrixIdx = j * numAgents + i;
							adjMatrix.SetBit(reverseMatrixIdx);
						}
					}
				}
			}

			{	// Build and process groups
				// Bfs visit flags
				int *const pBfsDist = NDI_NEW int[numAgents];
				for (U32F i = 0; i < numAgents; i++)
				{
					pBfsDist[i] = -1;
				}

				// Simple queues implemented as array
				const size_t queueArraySize = numAgents + 1; // The last entry doesnt store anything.
				U32F *const idxQueue = NDI_NEW U32F [queueArraySize];
				int queueFront;
				int queueEnd;

				U32F numGroups = 0;
				for (U32F i = 0; i < numAgents; i++)
				{
					if (pBfsDist[i] >= 0)	// Visited
					{
						continue;
					}

					FlockingAgent* agentGroup[kMaxFlockingAgents];
					U32F numGroupAgents = 0;
					Vector avgGroupAgentMoveDir = kZero;

					const Color groupDrawColor = AI::IndexToColor(numGroups, 0.33f);
					{	// Build group
						queueFront = 0;	// Reset the queue
						queueEnd = 0;

						// Enqueue src
						GAMEPLAY_ASSERT(QueueIncrement(queueEnd, queueArraySize) != queueFront); // Queue not full
						idxQueue[queueEnd] = i;
						queueEnd = QueueIncrement(queueEnd, queueArraySize);

						pBfsDist[i] = 0;	// Visit this node

						while (queueFront != queueEnd) // Queue not empty
						{
							const U32F idx0 = idxQueue[queueFront];	// Dequeue
							queueFront = QueueIncrement(queueFront, queueArraySize);

							FlockingAgent *const pFromAgent = ppAgents[idx0];
							const Point fromPos = pFromAgent->GetPosition();
							if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
							{
								g_prim.Draw(DebugCircle(Point(fromPos.X(), maxY, fromPos.Z()), kUnitYAxis, pFromAgent->GetVisionParams()->m_groupNeighborRadius, groupDrawColor), kPrimDuration1FramePauseable);
							}

							agentGroup[numGroupAgents++] = pFromAgent;
							avgGroupAgentMoveDir += pFromAgent->GetQueueMoveToMoveDir();

							for (U32F idx1 = 0; idx1 < numAgents; idx1++)
							{
								const U32F matrixIdx = idx0 * numAgents + idx1;

								if (adjMatrix.IsBitSet(matrixIdx) &&	// Valid neighbor
									pBfsDist[idx1] == -1)				// Not yet visited
								{
									// Enqueue the adjacent agent
									GAMEPLAY_ASSERT(QueueIncrement(queueEnd, queueArraySize) != queueFront); // Queue not full
									idxQueue[queueEnd] = idx1;
									queueEnd = QueueIncrement(queueEnd, queueArraySize);

									pBfsDist[idx1] = pBfsDist[idx0] + 1;	// Visit this node

									if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
									{
										const FlockingAgent *const pToAgent = ppAgents[idx1];
										const Point toPos = pToAgent->GetPosition();

										if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
										{
											g_prim.Draw(DebugLine(Point(fromPos.X(), maxY, fromPos.Z()), Point(toPos.X(), maxY, toPos.Z()), groupDrawColor), kPrimDuration1FramePauseable);
											const Point midPos = AveragePos(fromPos, toPos);
											g_prim.Draw(DebugString(Point(midPos.X(), maxY, midPos.Z()),
																	StringBuilder<32>("%d : %d", numGroups, pBfsDist[idx1]).c_str(),
																	groupDrawColor,
																	0.5f),
																	kPrimDuration1Frame);
										}
									}
								}
							}
						}
					}

					{	// Process group
						avgGroupAgentMoveDir = SafeNormalize(avgGroupAgentMoveDir / numGroupAgents, kZero);

						AgentDesc descs[kMaxFlockingAgents];
						for (U32F iAgent = 0; iAgent < numGroupAgents; iAgent++)
						{
							FlockingAgent *const pAgent = agentGroup[iAgent];
							
							const Point pos = pAgent->GetPosition();
							const float dist = Dot(pos - kOrigin, avgGroupAgentMoveDir);
							descs[iAgent] = { pAgent, dist};
						}

						QuickSort(descs, numGroupAgents, CompareAgents());

						if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
						{
							for (U32F iDesc = 0; iDesc < numGroupAgents; iDesc++)
							{
								FlockingAgent *const pAgent = descs[iDesc].m_pAgent;
								const Point pos = pAgent->GetPosition();
								const float dist = descs[iDesc].m_dist;
								
								const Point fromPos = Point(pos.X(), maxY, pos.Z());
								const Point toPos = fromPos + avgGroupAgentMoveDir * pAgent->GetVisionParams()->m_groupNeighborRadius;

								g_prim.Draw(DebugCross(fromPos, 0.05f, groupDrawColor, PrimAttrib(0), StringBuilder<32>("%d: %d: %3.3f", numGroups, iDesc, dist).c_str(), 0.5f), kPrimDuration1FramePauseable);
								g_prim.Draw(DebugArrow(fromPos, toPos, groupDrawColor), kPrimDuration1FramePauseable);
							}
						}

						for (U32F iDesc = 0; iDesc < numGroupAgents; iDesc++)
						{
							if (iDesc + 1 == numGroupAgents)
							{
								descs[iDesc].m_pAgent->SetQueueMoveToMotion(kFastImmediate);
							}
							else
							{
								descs[iDesc].m_pAgent->SetQueueMoveToMotion(kStop);
							}
						}
					}

					numGroups++; // This is the source node of a new group, bfs to process
				}
			}
		}

	private:
		bool m_inited;

		NdAtomicLock m_agentLock;
		NdAtomicLock m_regionLock;
		NdAtomicLock m_obstacleLock;
	};

	FlockManager2 g_flockMgr2;

	static void OnNavMeshLogin(const NavMesh* pConstMesh, Level*)
	{
		if (!pConstMesh ||
			!pConstMesh->GetTagData(SID("flocking-generate-rvo-obstacles"), false))
		{
			return;
		}

		const U32F numNavPolys = pConstMesh->GetPolyCount();
		for (U32F iPoly = 0; iPoly < numNavPolys; iPoly++)
		{
			const NavPoly *const pPoly = &pConstMesh->GetPoly(iPoly);
			if (pPoly->IsLink())
			{
				continue;
			}

			const U32F edgeCount = pPoly->GetVertexCount();
			for (U32F iEdge = 0; iEdge < edgeCount; iEdge++)
			{
				const U32F adjPolyId = pPoly->GetAdjacentId(iEdge);
				if (adjPolyId == NavPoly::kNoAdjacent)
				{
					const U32F iV0 = iEdge;
					const U32F iV1 = (iEdge + 1) % edgeCount;

					const Point& v0Ls = pPoly->GetVertex(iEdge);
					const Point& v1Ls = pPoly->GetNextVertex(iEdge);

					const Point& v0Ws = pConstMesh->LocalToWorld(v0Ls);
					const Point& v1Ws = pConstMesh->LocalToWorld(v1Ls);

					g_flockMgr2.AddFlockingRvoObstacle(v0Ws, v1Ws, pConstMesh);
				}
			}
		}
	}

	static void OnNavMeshLogout(const NavMesh* pConstMesh)
	{
		if (!pConstMesh ||
			!pConstMesh->GetTagData(SID("flocking-generate-rvo-obstacles"), false))
		{
			return;
		}

		g_flockMgr2.RemoveFlockingRvoObstacle(pConstMesh);
	}

	void FlockManager2::Init()
	{
		GAMEPLAY_ASSERT(!m_inited);
		m_inited = true;

		ALWAYS_ASSERT(ExternalBitArray::DetermineNumBlocks(kMaxFlockingAgents) * sizeof(U64) == sizeof(m_agentMaskBuffer));
		m_agentMask.Init(kMaxFlockingAgents, m_agentMaskBuffer);

		ALWAYS_ASSERT(ExternalBitArray::DetermineNumBlocks(kMaxFlockingRegions) * sizeof(U64) == sizeof(m_regionMaskBuffer));
		m_regionMask.Init(kMaxFlockingRegions, m_regionMaskBuffer);

		ALWAYS_ASSERT(ExternalBitArray::DetermineNumBlocks(kMaxFlockingRvoObstacles) * sizeof(U64) == sizeof(m_rvoObstacleMaskBuffer));
		m_rvoObstacleMask.Init(kMaxFlockingRvoObstacles, m_rvoObstacleMaskBuffer);

		g_regionManager.RegisterObserver(this);

		EngineComponents::GetNavMeshMgr()->AddLoginObserver(OnNavMeshLogin);
		EngineComponents::GetNavMeshMgr()->AddLogoutObserver(OnNavMeshLogout);
	}

	FlockingAgent* AddFlockingAgent(const Point_arg initialAgentPos, const Vector_arg initialAgentForward, const StringId64 flockingParamsId)
	{
		return g_flockMgr2.AddFlockingAgent(initialAgentPos, initialAgentForward, flockingParamsId);
	}

	void RemoveFlockingAgent(FlockingAgent *const pAgent)
	{
		g_flockMgr2.RemoveFlockingAgent(pAgent);
	}

	void FindAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
	{
		g_flockMgr2.FindAllFlockingAgents(outAgents, outNumAgents);
	}

	void FindSelectedFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
	{
		g_flockMgr2.FindSelectedFlockingAgents(outAgents, outNumAgents);
	}

	void SelectAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
	{
		g_flockMgr2.SelectAllFlockingAgents(outAgents, outNumAgents);
	}

	void SelectFlockingAgents(Point_arg startPtWs, Point_arg endPtWs, FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents)
	{
		g_flockMgr2.SelectFlockingAgents(startPtWs, endPtWs, outAgents, outNumAgents);
	}

	void SetRegionEnabled(const StringId64 regionNameId, bool isEnabled)
	{
		g_flockMgr2.SetRegionEnabled(regionNameId, isEnabled);
	}

	bool IsRegionBlocked(const StringId64 regionNameId)
	{
		return g_flockMgr2.IsRegionBlocked(regionNameId);
	}

	const WanderRegion* GetRegionByNameId(const StringId64 regionNameId)
	{
		return g_flockMgr2.GetRegionByNameId(regionNameId);
	}

	FlockingRvoObstacle* AddFlockingRvoObstacle(const Point_arg p0, const Point_arg p1)
	{
		return g_flockMgr2.AddFlockingRvoObstacle(p0, p1, nullptr);
	}

	void ClearFlockingRvoObstacles()
	{
		g_flockMgr2.ClearFlockingRvoObstacles();
	}

	void FindAllFlockingRvoObstacles(FlockingRvoObstacle* outRvoObstacles[kMaxFlockingRvoObstacles], U32F& outNumRvoObstacles)
	{
		g_flockMgr2.FindAllFlockingRvoObstacles(outRvoObstacles, outNumRvoObstacles);
	}

	void InitSimulation()
	{
		g_flockMgr2.Init();
	}

	void StepSimulation(const Point_arg playerPos)
	{
		PROFILE(AI, FlockingStepSimulation);
		g_flockMgr2.StepSimulation(playerPos);
	}

	void ShutdownSimulation()
	{
		g_flockMgr2.Shutdown();
	}
}
