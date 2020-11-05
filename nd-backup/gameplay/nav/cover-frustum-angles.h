/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

/*! \file cover-frustum-angles.h
   \brief CoverFrustumAngles struct
*/

#ifndef _NDLIB_COVER_FRUSTUM_ANGLES_H_
#define _NDLIB_COVER_FRUSTUM_ANGLES_H_


struct CoverFrustumAngles
{
	// angles defining a frustum which represents the area this cover is "good for attacking against"
	float inner;
	float outer;
	float upper;
	float lower;

	CoverFrustumAngles( float _inner=-15.0f, float _outer=45.0f, float _upper=45.0f, float _lower=-15.0f ) :
		   inner(_inner), outer(_outer), upper(_upper), lower(_lower) {}
};

struct ProtectionFrustumAngles
{
	// angles defining a frustum which represents the area this cover is "good for hiding from"
	// inner angle measures the angle of the frustum plane directed inward toward the cover
	// outer angle measures the angle of the frustum plane directed outward
	//
	// outer inner            inner outer
	//   <-- -->                <-- -->
	//   \  |  /                \  |  /
	//    \ | /                  \ | /
	//     \|/ LEFT         RIGHT \|/
	//      +--------      --------+
	//
	// NOTE: Angles are measured in degrees, relative to cover facing direction, using
	// the right-hand rule (about world j axis). As such, in the diagrams above:
	//    LEFT:  outer > 0, inner < 0 
	//    RIGHT: outer < 0, inner > 0

	float inner;
	float outer;
	float upperStand;
	float upperCrouch;
	float lower;

	ProtectionFrustumAngles( float _inner=-55.0f, float _outer=45.0f, float _upperStand=85.0f, float _upperCrouch=45.0f, float _lower=-45.0f ) :
		   inner(_inner), outer(_outer), upperStand(_upperStand), upperCrouch(_upperCrouch), lower(_lower) {}
};

#endif


