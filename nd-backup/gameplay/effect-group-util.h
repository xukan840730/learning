/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NDLIB_EFFECT_GROUP_UTIL_H
#define NDLIB_EFFECT_GROUP_UTIL_H

class EffectAnim;

/// --------------------------------------------------------------------------------------------------------------- ///
class DebugEffectFile
{
public:
	char* m_artGroupName;
	EffectAnim* m_anims;
	StringId64 m_artGroupId;
	U32 m_numAnims;

	const EffectAnim* GetEffectAnim(StringId64 animId) const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern DebugEffectFile* LoadEffectGroupFile(const char* filename, U32 version);
extern const char* GetEffectNameFromId(StringId64 nameId);
extern bool IsSoundEffect(StringId64 effectId);
extern bool IsVoiceEffect(StringId64 effectId);
extern bool ShouldValidateEffect(StringId64 effectId);
extern void ForceLinkEffectGroupUtil();

#endif // NDLIB_EFFECT_GROUP_UTIL_H
