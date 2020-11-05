/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef JOINT_SFX_HELPER_H
#define JOINT_SFX_HELPER_H


class EffectAnim;
class EffectAnimEntry;
struct FgAnimData;

namespace ndanim
{
}


class JointSfxHelper
{
public:

	static float kHeelJointOffset;
	static float kBallJointOffset;
	static float kTopThreshold;
	static float kBottomThreshold;
	static float kTopSpeedThreshold;
	static float kBottomSpeedThreshold;

	JointSfxHelper(const FgAnimData* pAnimData, const char* destFolderPath);
	~JointSfxHelper();

	void WriteEffects(bool includeExistingEffects, bool includeNewEffects, bool generateFootEffects);

private:

	class Hysteresis
	{
	public:

		void Init(float top, float bottom)
		{
			m_topThreshold = top;
			m_bottomThreshold = bottom;
			m_below = true;
		}

		void Update(float value)
		{
			if (m_below)
			{
				if (value > m_topThreshold)
				{
					m_below = !m_below;
				}
			}
			else
			{
				if (value < m_bottomThreshold)
				{
					m_below = !m_below;
				}
			}
		}

		bool IsBelow() const
		{
			return m_below;
		}

	private:
		float m_topThreshold;
		float m_bottomThreshold;
		bool m_below;
	};

	struct GeneratedFootEffect
	{
		const EffectAnim* m_pEffectAnim;
		StringId64 m_type;
		U32 m_frame;
	};

	static const int kMaxEffectAnims;
	static const int kMaxGeneratedFootEffects;
	const FgAnimData* m_pAnimData;
	const EffectAnim** m_pAllEffectAnims;
	const EffectAnim** m_pExistingEffectAnims;
	EffectAnim* m_pNewEffectAnims;
	GeneratedFootEffect* m_pGeneratedFootEffects;
	U32F m_numTotalEffectAnims;
	U32F m_numExistingEffectAnims;
	U32F m_numNewEffectAnims;
	U32F m_numGeneratedFootEffects;

	static const int kMaxFolderPathLength = 128;
	char m_destFolder[kMaxFolderPathLength];

	Hysteresis m_leftHeelHist;
	Hysteresis m_leftBallHist;
	Hysteresis m_rightHeelHist;
	Hysteresis m_rightBallHist;

	Hysteresis m_leftHeelSpeedHist;
	Hysteresis m_leftBallSpeedHist;
	Hysteresis m_rightHeelSpeedHist;
	Hysteresis m_rightBallSpeedHist;

	Point m_lastLeftHeelPosOs;
	Point m_lastLeftBallPosOs;
	Point m_lastRightHeelPosOs;
	Point m_lastRightBallPosOs;

	Point m_lastLeftHeelPosWs;
	Point m_lastLeftBallPosWs;
	Point m_lastRightHeelPosWs;
	Point m_lastRightBallPosWs;

	enum BufferType
	{
		kBufferSoundAll,
		kBufferSoundNew,
		kBufferSoundExisting,
		kBufferMiscAll,
		kBufferMiscNew,
		kBufferMiscExisting,
		kBufferDecal,

		kNumBufferTypes
	};
	
	static const int kTextBufferSize = 512 * 1024;

	struct WriteBuffer
	{
		char* m_pTextBuffer;
		int m_charsWritten;
		bool m_firstWrite;
	};

	WriteBuffer m_writeBuffers[kNumBufferTypes];
	char* m_pCurrentFileBuffer;
	
	U32F GatherExistingEffects(const SkeletonId skelId);
	void GenerateProceduralFootEffects(const EffectAnim* pEffectAnim, U32F frameIndex, const Transform& objectXform, const Transform* pJointTransforms, U32F numTotalJoints, bool looping = false, bool noWrite = false);
	void GenerateFootEffectsForAnim(const EffectAnim* pEffectAnim);

	bool DidEffectExist(const EffectAnim* pEffectAnim) const;

	void AddEffectAnimToBuffers(const EffectAnim* pEffectAnim, bool didExist);
	void AddEffectToBuffer(U32F bufferType, const EffectAnimEntry* pCurrentEffect);
	void AddAnimHeaderToBuffer(U32F bufferType, const char* pAnimName);
	void AddAnimFooterToBuffer(U32F bufferType);
	void AddGeneratedFootEffectToBuffer(U32F bufferType, const GeneratedFootEffect* pGeneratedFootEffect);
	void AddGeneratedFootDecalEffectToBuffer(U32F bufferType, const GeneratedFootEffect* pGeneratedFootEffect);
	const char* GetAppendName(U32F bufferType);
	void WriteBufferToEffectFile(const char* pArtGroupName, U32F bufferType);
};

#endif // JOINT_SFX_HELPER_H
