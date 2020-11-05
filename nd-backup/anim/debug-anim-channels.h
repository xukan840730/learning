/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef DEBUG_ANIM_CHANNEL_H
#define DEBUG_ANIM_CHANNEL_H

//Development code only
#if FINAL_BUILD
#	define ENABLE_DEBUG_ANIM_CHANNELS 0
#else
#	ifndef ENABLE_DEBUG_ANIM_CHANNELS
#		define ENABLE_DEBUG_ANIM_CHANNELS 1
#	endif
#endif

#if ENABLE_DEBUG_ANIM_CHANNELS

#include "corelib/containers/hashtable.h"
#include "corelib/util/hashable-pair.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/resource/resource-table.h"

class ArtItemAnim;
struct CompressedChannel;
class ArtItemAnimGroup;
template <typename T> class ListArray;


class DebugAnimChannels : public ResourceObserver
{
public:
	const CompressedChannel* GetChannel(const ArtItemAnim* pAnim, StringId64 channelNameId) const;
	void AddChannel(const ArtItemAnim* pAnim, StringId64 channelNameId, const CompressedChannel* pChannel);
	void RemoveChannelsForAnim(const ArtItemAnim* pAnim);	
	void GetChannelsForAnim(const ArtItemAnim* pAnim, ListArray<StringId64>& outChannelNames) const;
//	virtual void OnAnimGroupLogout(const ArtItemAnimGroup* pAnimGroup) override;
	virtual void PreAnimationLogout(const ArtItemAnim* pAnim) override;

	static void Init();
	static DebugAnimChannels* Get();

	// does not acquire lock for you, just returns a pointer
	NdAtomicLock* GetLock() { return &m_lock; }

private:	
	typedef hashablePair<const ArtItemAnim*, StringId64> Key;
	typedef HashTable< Key, const CompressedChannel*> ChannelTable;

	DebugAnimChannels() {}	

	ChannelTable m_channelTable;

	mutable NdAtomicLock m_lock;

	static DebugAnimChannels* s_pDebugAnimChannels;
};

struct UnalignedQuat
{
	UnalignedQuat() {}

	UnalignedQuat(Quat_arg q)
		: x(q.X())
		, y(q.Y())
		, z(q.Z())
		, w(q.W())
	{}

	operator Quat() const { return Quat(x, y, z, w); }

	float x,y,z,w;	
};

struct AnimChannelFormat
{
	UnalignedQuat m_quat;
	Vec3 m_trans;
	Vec3 m_scale;
};

class IChannelSampleBuilder
{
public:
	virtual void BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const = 0;
};

class DebugChannelBuilder
{
public:
	const CompressedChannel* Build(const ArtItemAnim* pAnim, IChannelSampleBuilder* pBuild);
};

class JointChannelBuild : public IChannelSampleBuilder
{
public:
	JointChannelBuild(I32 jointIndex);
	virtual void BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const override;

private:
	I32			m_jointIndex;
};

class CenterOfMassChannelBuild : public IChannelSampleBuilder
{
public:
	CenterOfMassChannelBuild();
	virtual void BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const override;

private:
};


class RawDataChannelBuild : public IChannelSampleBuilder
{
public:
	RawDataChannelBuild(float *pData, int numData);
	virtual void BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const override;

private:
	float			*m_pData;
	int				m_numData;
};

#endif

#endif //DEBUG_ANIM_CHANNEL_H
