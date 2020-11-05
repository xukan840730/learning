/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

float g_fEpsilonScale = 0.00001f;
float g_fEpsilonRotation = 0.00001f;
float g_fEpsilonTranslation = 0.00001f;
float g_fEpsilonFloat = 0.0f;

float GetScaleErrorTolerance() { 
	return g_fEpsilonScale; 
}
float GetRotationErrorTolerance() { 
	return g_fEpsilonRotation; 
}
float GetTranslationErrorTolerance() { 
	return g_fEpsilonTranslation; 
}
float GetFloatChannelErrorTolerance() { 
	return g_fEpsilonFloat; 
}
void SetScaleErrorTolerance( float epsilon ) { 
	g_fEpsilonScale = epsilon > 0.0f ? epsilon : 0.0f; 
}
void SetRotationErrorTolerance( float epsilon ) { 
	g_fEpsilonRotation = epsilon > 0.0f ? epsilon : 0.0f; 
}
void SetTranslationErrorTolerance( float epsilon ) { 
	g_fEpsilonTranslation = epsilon > 0.0f ? epsilon : 0.0f; 
}
void SetFloatChannelErrorTolerance( float epsilon ) { 
	g_fEpsilonFloat = epsilon > 0.0f ? epsilon : 0.0f; 
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
