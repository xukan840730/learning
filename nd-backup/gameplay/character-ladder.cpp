/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-ladder.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsSameLadderEdge(FeatureEdgeReference ladderEdge, FeatureEdgeReference edge)
{
	for (int iDir = 0; iDir < 2; iDir++)
	{
		FeatureEdgeReference currEdge = edge;

		while (currEdge.GetSrcEdge()->GetFlags() & FeatureEdge::kFlagLadder)
		{
			if (currEdge == ladderEdge)
				return true;

			FeatureEdgeReference nextEdge = currEdge.GetLink(iDir);
			if (nextEdge.GetSrcEdge() == nullptr)
				break;

			float newEdgeDot = Dot(currEdge.GetWallNormal(), nextEdge.GetWallNormal());
			if (newEdgeDot < 0.95f)
				break;

			if (nextEdge == edge)
				break;

			currEdge = nextEdge;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EdgeInfo GetLadderEdgeCenter(FeatureEdgeReference edge)
{
	GAMEPLAY_ASSERT(edge.GetFlags() & FeatureEdge::kFlagLadder);

	const int kMaxEdges = 64;

	FeatureEdgeReference edges[kMaxEdges];
	float edgeLen[kMaxEdges];

	FeatureEdgeReference currEdge = edge;
	FeatureEdgeReference nextEdge = currEdge.GetLink(0);
	int loopCount = 0;
	while (nextEdge.GetSrcEdge() && (nextEdge.GetSrcEdge()->GetFlags() & FeatureEdge::kFlagLadder))
	{
		float newEdgeDot = Dot(nextEdge.GetWallNormal(), currEdge.GetWallNormal());
		if (newEdgeDot < 0.95f)
			break;

		currEdge = nextEdge;
		nextEdge = currEdge.GetLink(0);
		loopCount++;
		if (loopCount >= kMaxEdges)
		{
			EdgeInfo ladderEdge;
			ladderEdge = edge;
			ladderEdge.SetClosestEdgePt(Lerp(edge.GetVert0(), edge.GetVert1(), 0.5f));
			return ladderEdge;
		}
	}

	int numEdges = 0;
	float totalLen = 0.0f;
	while (currEdge.GetSrcEdge() && (currEdge.GetSrcEdge()->GetFlags() & FeatureEdge::kFlagLadder))
	{
		if (numEdges > 0)
		{
			FeatureEdgeReference prevEdge = edges[numEdges - 1];
			float newEdgeDot = Dot(prevEdge.GetWallNormal(), currEdge.GetWallNormal());
			if (newEdgeDot < 0.95f)
				break;
		}
		edges[numEdges] = currEdge;

		const float len = Length(currEdge.GetEdge());
		totalLen += len;
		edgeLen[numEdges] = len;
		numEdges++;

		currEdge = currEdge.GetLink(1);
	}

	if (totalLen < 0.2f)
	{
		EdgeInfo ladderEdge;
		ladderEdge = edge;
		ladderEdge.SetClosestEdgePt(Lerp(edge.GetVert0(), edge.GetVert1(), 0.5f));
		return ladderEdge;
	}

	const float midLen = 0.5f * totalLen;
	float currLen = 0.0f;
	for (int i = 0; i < numEdges; i++)
	{
		if (currLen + edgeLen[i] >= midLen)
		{
			float edgePct = LerpScaleClamp(currLen, currLen + edgeLen[i], 0.0f, 1.0f, midLen);
			EdgeInfo ladderEdge;
			ladderEdge = edges[i];
			ladderEdge.SetClosestEdgePt(Lerp(edges[i].GetVert0(), edges[i].GetVert1(), edgePct));
			return ladderEdge;
		}

		currLen += edgeLen[i];
	}

	DEBUG_HALT();
	EdgeInfo ladderEdge;
	ladderEdge = edge;
	ladderEdge.SetClosestEdgePt(Lerp(edge.GetVert0(), edge.GetVert1(), 0.5f));
	return ladderEdge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EdgeInfo GetNextLadderEdge(const ListArray<EdgeInfo>& nearbyEdges, EdgeInfo ladderEdge, int steps)
{
	if (steps == 0)
		return ladderEdge;

	if (!ladderEdge)
		return EdgeInfo();

	const float stepOffset = Sign(steps) * ICharacterLadder::kLadderRungSpacing;
	const float halfStepOffset = 0.5f * stepOffset;

	const Point edgeCurrentPt = ladderEdge.GetClosestEdgePt();
	const Vector edgeCurrentUp = ladderEdge.GetTopNormal();
	const Vector edgeCurrentNormal = ladderEdge.GetWallNormal();

	EdgeInfo bestEdge;
	float bestEdgeRating = kLargestFloat;
	for (int iEdge = 0; iEdge < nearbyEdges.Size(); iEdge++)
	{
		const EdgeInfo& nearbyEdge = nearbyEdges[iEdge];
		if (!(nearbyEdge.GetFlags() & FeatureEdge::kFlagLadder))
			continue;

		float dotWallNormal = Dot(edgeCurrentNormal, nearbyEdge.GetWallNormal());
		if (dotWallNormal < 0.95f)
			continue;

		const EdgeInfo nearbyLadderEdge = GetLadderEdgeCenter(nearbyEdge);
		if (nearbyLadderEdge != nearbyEdge)
			continue;

		const Point nearbyLadderEdgePt = nearbyLadderEdge.GetClosestEdgePt();
		const Vector toNearbyEdge = nearbyLadderEdgePt - edgeCurrentPt;
		const float nearbyEdgeDistSq = LengthSqr(toNearbyEdge);
		const Point targetEdgePt = edgeCurrentPt + stepOffset * edgeCurrentUp;
		const float rating = LengthSqr(targetEdgePt - nearbyLadderEdgePt);

		if (rating < (halfStepOffset*halfStepOffset) && rating < bestEdgeRating)
		{
			bestEdge = nearbyLadderEdge;
			bestEdgeRating = rating;
		}
	}

	if (!bestEdge)
		return EdgeInfo();

	if (steps > 1)
	{
		return GetNextLadderEdge(nearbyEdges, bestEdge, steps - 1);
	}
	else if (steps < -1)
	{
		return GetNextLadderEdge(nearbyEdges, bestEdge, steps + 1);
	}

	return bestEdge;
}
