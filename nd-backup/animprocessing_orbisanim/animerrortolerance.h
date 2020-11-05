/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

		// Error tolerance control functions for scene pre-processing and data processing

		/// Returns the current error tolerance used for detecting constant or identity scale
		/// values during animation processing.  Defaults to 0.00001f.
		float GetScaleErrorTolerance();
		/// Returns the current error tolerance used for detecting constant or identity quaternion
		/// values during animation processing.  Defaults to 0.00001f.
		float GetRotationErrorTolerance();
		/// Returns the current error tolerance used for detecting constant or identity translation
		/// values during animation processing.  Defaults to 0.00001f.
		float GetTranslationErrorTolerance();
		/// Returns the current error tolerance used for detecting constant or identity float channel
		/// values during animation processing.  Defaults to 0.0f.
		float GetFloatChannelErrorTolerance();

		/// Sets the error tolerance used for detecting constant or identity scale values.
		void SetScaleErrorTolerance( float epsilon );
		/// Sets the error tolerance used for detecting constant or identity quaternion values.
		void SetRotationErrorTolerance( float epsilon );
		/// Sets the error tolerance used for detecting constant or identity translation values.
		void SetTranslationErrorTolerance( float epsilon );
		/// Sets the error tolerance used for detecting constant or identity float channel values.
		void SetFloatChannelErrorTolerance( float epsilon );

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim

