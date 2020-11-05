/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "animprocessingstructs.h"
#include "icelib/icesupport/streamwriter.h"

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

		/// Virtual base class for object root animations
		class AnimationClipObjectRootAnim
		{
		public:
			// make a copy of this AnimationClipObjectRootAnim
			virtual AnimationClipObjectRootAnim* ConstructCopy() const = 0;

			// Destroy this AnimationClipObjectRootAnim
			virtual ~AnimationClipObjectRootAnim() {};

			/// Write this AnimationClipObjectRootAnim to the given streamWriter, returning
			/// the location of the start of the data.
			virtual ICETOOLS::Location Write( ICETOOLS::StreamWriter& streamWriter ) const = 0;

			/// Returns the type of this AnimationClipObjectRootAnim
			virtual AnimationObjectRootAnimType GetType() const = 0;

			/// Returns the number of sourceData frames of data encoded in this animation track
			virtual unsigned GetNumFrames() const = 0;
			/// Returns the sourceData joint animation index encoded in this animation track
			virtual unsigned GetObjectRootAnimIndex() const = 0;
			/// Returns the scale encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			virtual ITGEOM::Vec3 GetScale( unsigned iFrame ) const = 0;
			/// Returns the rotation encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			virtual ITGEOM::Quat GetRotation( unsigned iFrame ) const = 0;
			/// Returns the translation encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			virtual ITGEOM::Vec3 GetTranslation( unsigned iFrame ) const = 0;

			/// Write the values extracted to the debug output stream
			virtual void Dump() const = 0;
		};

		/// Implementation of AnimationClipObjectRootAnim that extracts the linear component of 
		/// the object root joint animation.
		class AnimationClipLinearObjectRootAnim : public AnimationClipObjectRootAnim
		{
			ITGEOM::Vec3	m_rootLinearScale;			//!< extract linear component of root scale animation and store it here
			ITGEOM::Quat	m_rootLinearRotation;		//!< extract linear component of root rotation animation and store it here
			ITGEOM::Vec3	m_rootLinearTranslation;	//!< extract linear component of root translation animation and store it here
			unsigned		m_iObjectRootAnimIndex;		//!< index of object root joint animation
			unsigned		m_iNumFrames;				//!< number of frames encoded
			float			m_fPhasePerFrame;			//!< for speed in Get functions, precalculate 1/(m_iNumFrames-1)

			friend bool ExtractLinearRootAnim( AnimationClipSourceData& sourceData, unsigned iObjectRootAnim );
			/// private constructor called by ExtractLinearRootAnim()
			AnimationClipLinearObjectRootAnim(unsigned iObjectRootAnimIndex, unsigned iNumFrames, float fPhasePerFrame, ITGEOM::Vec3 const& rootLinearScale, ITGEOM::Quat const& rootLinearRotation, ITGEOM::Vec3 const& rootLinearTranslation) :
				m_rootLinearScale(rootLinearScale), m_rootLinearRotation(rootLinearRotation), m_rootLinearTranslation(rootLinearTranslation),
				m_iObjectRootAnimIndex(iObjectRootAnimIndex), m_iNumFrames(iNumFrames), m_fPhasePerFrame(fPhasePerFrame)
			{
			}

		public:
			/// construct an empty AnimationClipLinearObjectRootAnim()
			AnimationClipLinearObjectRootAnim();

			// make a copy of this AnimationClipObjectRootAnim
			AnimationClipObjectRootAnim* ConstructCopy() const { return new AnimationClipLinearObjectRootAnim(*this); }

			/// Write this linear data to the given streamWriter, returning
			/// the location of the start of the data.
			ICETOOLS::Location Write( ICETOOLS::StreamWriter& streamWriter ) const;

			/// Returns the type of this AnimationClipObjectRootAnim
			AnimationObjectRootAnimType GetType() const { return kObjectRootAnimLinear; }

			/// Returns the number of sourceData frames of data encoded in this animation track
			unsigned GetNumFrames() const { return m_iNumFrames; }
			/// Returns the sourceData joint animation index encoded in this animation track
			unsigned GetObjectRootAnimIndex() const { return m_iObjectRootAnimIndex; }
			/// Returns the scale encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			ITGEOM::Vec3 GetScale( unsigned iFrame ) const { 
				return ITGEOM::Lerp( kIdentityScale, m_rootLinearScale, m_fPhasePerFrame*(float)iFrame );
			}
			/// Returns the rotation encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			ITGEOM::Quat GetRotation( unsigned iFrame ) const {
				return ITGEOM::Slerp( kIdentityRotation, m_rootLinearRotation, m_fPhasePerFrame*(float)iFrame );
			}
			/// Returns the translation encoded in this animation track at frame iFrame (assuming a looping clip if not in range 0 ... GetNumFrames()-1)
			ITGEOM::Vec3 GetTranslation( unsigned iFrame ) const { return m_rootLinearTranslation * (m_fPhasePerFrame*(float)iFrame); }

			/// Write the values extracted to the debug output stream
			void Dump() const;
		};

		/// Extract the linear part of sourceData joint animation iObjectRootAnim from sourceData
		/// and store it as a new AnimationClipLinearObjectRootAnim in sourceData.
		/// Returns false if the extraction results in no data (if the sourceData for 
		/// iObjectRootAnim is constant, for instance).
		bool ExtractLinearRootAnim( AnimationClipSourceData& sourceData, unsigned iObjectRootAnim );

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim

