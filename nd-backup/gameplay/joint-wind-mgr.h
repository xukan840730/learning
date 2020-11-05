/*
* Copyright (c) 2007 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#ifndef JOINT_WIND_MGR_H
#define JOINT_WIND_MGR_H

#include "ndlib/render/ndgi/ndgi.h"

class LoadedTexture;
class RenderFrameParams;

struct JointWindConst
{
	F32 m_windDirection[3];
	F32 m_windSpeed;
	F32 m_turbulence;
	F32 m_windMultiplier;
	F32 m_time;
	U32F m_numJoints;
};


struct JointWindDataIn
{
	F32 m_pos[3];
	F32 m_stiffness;
	U32F m_flags;
};

struct JointWindDataOut
{
	F32 m_quat[4];
	U32F m_flags;
};


/////////////////////////////////////////////////////////////
//

class JointWindManager
{
public:

	enum {
		kNumCsThreads = 64,
		kMaxWindJoints = 3000
	};

	JointWindManager();

	void Init();
	void Close();

	JointWindDataIn* GetInputBuffer(U32F numJoints, I32F& index);
	JointWindDataOut* GetOutputBuffer(I32F index);

	void PreUpdate();
	void PostUpdate(RenderFrameParams const *pParams);

private:
	ndgi::ComputeContext m_cmpContext;
	static ndgi::ComputeQueue s_cmpQueue;
	I64 m_cmpLastKickFrame;
	I64 m_outGameFrame;

	ndgi::Buffer m_hOutBuffer;
	ndgi::Buffer m_hInBuffer;

	JointWindDataOut* m_pOutData;
	JointWindDataIn* m_pInData;
	U32F m_inIndex;

	NdAtomicLock m_lock;
};

extern JointWindManager g_jointWindMgr;

#endif // JOINT_WIND_MGR_H
