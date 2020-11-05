/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshPatchInput;

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintNavMeshPatchInput(DoutBase* pOutput, const NavMeshPatchInput& patchInput);
void GenerateNavMeshPatch(const NavMeshPatchInput& patchInput);
void RunGenerateNavMeshPatchJob(ndjob::CounterHandle pDependentCounter, ndjob::CounterHandle pDependentCounter2);
