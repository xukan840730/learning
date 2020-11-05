/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class TraversalActionPack;

/// --------------------------------------------------------------------------------------------------------------- ///
class INdAiTraversalController : public ActionPackController
{
public:
	virtual void PostRootLocatorUpdate() override = 0;
	virtual void Reset() override = 0;
	virtual void OnActionPacksReloaded() = 0;

	virtual bool IsNormalMovementSuppressed() const = 0;
	virtual float GetGroundAdjustBlendFactor() const = 0;
	virtual float GetNavMeshAdjustBlendFactor() const = 0;

	virtual bool SafeToBindToDestSpace() const = 0;

	virtual void SetDebugTapEnterTableIndex(I32F val)	{}
	virtual void SetDebugTapLoopTableIndex(I32F val)	{}
	virtual void SetDebugTapExitTableIndex(I32F val)	{}
	virtual void SetDebugTapDemeanor(Demeanor val)		{}

	virtual I32F GetTapEnterTableAnimCount(const TraversalActionPack* pTap) const	{ return 0; }
	virtual I32F GetTapLoopTableAnimCount(const TraversalActionPack* pTap) const	{ return 0; }
	virtual I32F GetTapExitTableAnimCount(const TraversalActionPack* pTap) const	{ return 0; }

	virtual void RequestMeleeAbort()			{}
	virtual bool IsMeleeAbortSupported() const	{ return false; }

	virtual Point GetExitPathOriginPosPs() const = 0;

	virtual bool CanProcessCommands() const { return false; }
	virtual bool IsReadyToExit() const override { return CanProcessCommands(); }
};
