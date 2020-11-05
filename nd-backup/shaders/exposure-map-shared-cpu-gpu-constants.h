/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef EXPOSURE_MAP_SHARED_CPU_GPU_CONSTANTS_H
#define EXPOSURE_MAP_SHARED_CPU_GPU_CONSTANTS_H

#define kExposureMapThreadsPerGroup						(64)

// Currently cannot be higher than 32, as during part of the computation process,
// each map gets an output bit in a U32 texture. You'll need to change that
// to a U64 to go higher.
#define kExposureMapMaxHeightMaps						(32)

// Treat as unitless tuning parameters for the way the exposure mapping algorithm
// "cheats" by representing a crouching target lower to the ground when near the
// observer so that an observer standing right up against low cover cannot see a
// crouching target on the opposite side of the cover, even though in reality
// they're totally visible
#define kExposureMapTuningMinCrouchHeight				(1.1f)
#define kExposureMapTuningMaxCrouchHeight				(6.7f)
#define kExposureMapTuningAddToSquaredDistance			(2.0f)
#define kExposureMapTuningDivideSquaredDistance			(13.0f)

#endif