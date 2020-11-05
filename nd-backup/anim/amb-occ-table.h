/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef AMBOCCTABLE_H
#define AMBOCCTABLE_H

#include "corelib/containers/linkednode.h"
#include "ndlib/render/ndgi/ndgi.h"

class AmbientShadowsOccludersInfo
{
public:
	struct AttachInfo
	{
		Vec4	m_offset;
		Quat	m_rotOffset;
		I32		m_jointIndex;
		U32		m_padding[3];
	};

	struct CubemapOccluderData
	{
		Vec4			m_cubemapDataScale;
		Vec4			m_cubemapDataBias;
		Vec4			m_cubemapDirDataScale;
		Vec4			m_cubemapDirDataBias;

		float			m_influenceRadius;
		unsigned char	*m_data;
		U32				m_padding[2];
	};

	struct VolumeOccluderData
	{
		Vec4					m_volumeBoxSizes;

		unsigned char*			m_data;
		U32						m_volumeSizeX;
		U32						m_volumeSizeY;
		U32						m_volumeSizeZ;		
	};

	StringId64			m_meshNameId;
	U32					m_numCubemapOccluders;
	U32					m_numVolumeOccluders;

	AttachInfo			*m_cubemapOccludersAttachInfo;
	AttachInfo			*m_volumeOccludersAttachInfo;

	CubemapOccluderData	*m_cubemapOccluders;
	VolumeOccluderData	*m_volumeOccluders;
};

struct AmbientShadowsVolumeOccluderTextures
{
	ndgi::Texture        m_hTex3d;
};

class AmbientOccludersTableEntry
{
friend class AmbientOccludersTable;

public:
	void								IncrementRefCount(void)			{m_refCount++;}
	const AmbientShadowsOccludersInfo*	GetOccluderInfo(void) const			{return m_pAmbientOccludersInfo;}
	const ndgi::Texture 				GetOccluderTexture(U32 index) const	{return m_pAmbientVolumeOccluderTextures[index].m_hTex3d;}

protected:
	const AmbientShadowsOccludersInfo* m_pAmbientOccludersInfo;		
	AmbientShadowsVolumeOccluderTextures* m_pAmbientVolumeOccluderTextures;
	const char* m_pPackageName;
	StringId64 m_packageNameId;
	StringId64 m_meshNameId;
	I32 m_refCount;
};

class AmbientOccludersTable
{
public:
	typedef AmbientOccludersTableEntry Entry;

	static const I32F kMaxNumEntries = 1024;
	static Entry m_table[kMaxNumEntries];
	static I32F m_numEntries;

	static const AmbientShadowsOccludersInfo* LookupAmbientOccluders(StringId64 meshNameId);

	static void IncrementRefCount(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo);
	static void DecrementRefCount(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo);

	static U32F GetReferenceCountForPackage(StringId64 packageNameId);

	static void LoginAmbientShadowsOccluder(const AmbientShadowsOccludersInfo* pOccluderInfo, const char* pPackageName);
	static void LogoutAmbientShadowsOccluder(const AmbientShadowsOccludersInfo* pOccluderInfo, const char* pPackageName);

	static void DumpToTty();

	// An observer whose callbacks will be called when interesting operations occur on objects
	struct Observer : public LinkedNode<Observer>
	{
		virtual void OnLogin(const Entry& entry)	{}
		virtual void OnLogout(const Entry& entry)		{}
	};

	static void InitializeObservers();
	static void RegisterObserver(Observer * pObserver);
	static void UnregisterObserver(Observer * pObserver);

	static Entry* LookupEntry(StringId64 meshNameId);

	static I32F GetCurrentMemUsage() { return m_currentMemUsage; }
	static I32F GetMaxMemUsage() { return m_maxMemUsage; }

private:
	static I32F m_currentMemUsage;
	static I32F m_maxMemUsage;

	static Entry* LookupEntry(const AmbientShadowsOccludersInfo* pAmbientOccludersInfo);
};

#endif // AMBOCCTABLE_H


