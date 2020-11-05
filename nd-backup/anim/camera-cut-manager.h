/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERA_CUT_MANAGER_H
#define CAMERA_CUT_MANAGER_H

class ArtItemAnim;

struct CameraCutAnimEntry
{
	const ArtItemAnim* m_pArtItemAnim = nullptr;
	StringId64 m_associatedAnimNameId = INVALID_STRING_ID_64;		// Original animation that contained the camera cut
	int m_cameraCutFrameIndex = 0;
};

class CameraCutManager
{
public:
	void Init();

	void RegisterAnim(const ArtItemAnim* pAnim);
	void UnregisterAnim(const ArtItemAnim* pAnim);

	int GetCameraCutsInAnimation(StringId64 animNameId, CameraCutAnimEntry* pEntries, int maxNumEntries);
	int GetNumCameraCutAnims() const;

	const CameraCutAnimEntry* FindAnimEntry(const ArtItemAnim* pAnim) const;
	const CameraCutAnimEntry* FindAnimEntry(StringId64 animId, int frameNum) const;

	void SortCameraCutFrames();

private:
	static const int kNumCameraCutAnimEntries = 128;
	CameraCutAnimEntry m_cameraCutAnimEntries[kNumCameraCutAnimEntries];
	int m_cameraCutNumEntries = 0;

	bool m_needsSort = false;

};

#endif