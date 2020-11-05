/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

class ArtItemAnim;

void AnimStreamNotifyUsage(const ArtItemAnim* pArtItem, StringId64 animNameId, float animPhase);
const ArtItemAnim* AnimStreamGetArtItem(const ArtItemAnim* pArtItem, StringId64 animNameId, float phase);
const ArtItemAnim* AnimStreamGetArtItemFirstChunk(const ArtItemAnim* pArtItem, StringId64 animNameId);
float AnimStreamGetStreamPhase(const ArtItemAnim* pArtItem, StringId64 animNameId);
void AnimStreamUpdateAll();
float AnimStreamGetChunkPhase(const ArtItemAnim* pArtItem, StringId64 animNameId, float animPhase);
float AnimStreamHasLoaded(const ArtItemAnim* pArtItem, StringId64 animNameId, float animPhase);
const ArtItemAnim* AnimStreamLookupRealArtItem(const ArtItemAnim* pChunkArtItem);
void AnimStreamLoginStreamDef(void* pData);
void AnimStreamLogoutStreamDef(void* pData);
void AnimStreamReset(const ArtItemAnim* pArtItem, StringId64 animNameId);
void AnimStreamResetAll();
bool AnimStreamIsBusy();
