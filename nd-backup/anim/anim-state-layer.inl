/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

/*		++
	 ========
	 |      |
	 |      |
	 |      |
	 |      |
	  \:\/:/
	  /""""\
	 |______|
*/

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
DATA_TYPE AnimStateLayer::InstanceBlender<DATA_TYPE>::BlendForward(const AnimStateLayer* pStateLayer,
																   DATA_TYPE initialData)
{
	if (!pStateLayer)
		return initialData;

	const U32F numTracks = pStateLayer->m_numTracks;
	DATA_TYPE prevData	 = initialData;

	for (U32F iTrack = 0; iTrack < numTracks; ++iTrack)
	{
		const AnimStateInstanceTrack* pTrack = pStateLayer->m_ppTrackList[numTracks - iTrack - 1];
		if (!pTrack)
			continue;

		DATA_TYPE newData	   = GetDataForTrackForward(pTrack, prevData);
		const bool oldestTrack = (iTrack == 0);
		const float masterFade = oldestTrack ? 1.0f : pTrack->MasterFade();
		const float animFade   = oldestTrack ? 1.0f : pTrack->AnimFade();
		const float motionFade = oldestTrack ? 1.0f : pTrack->MotionFade();

		prevData = BlendData(prevData, newData, masterFade, animFade, motionFade);
	}

	return prevData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
DATA_TYPE AnimStateLayer::InstanceBlender<DATA_TYPE>::GetDataForTrackForward(const AnimStateInstanceTrack* pTrack,
																			 DATA_TYPE initialData)
{
	const U32F numInstances	  = pTrack->GetNumInstances();
	DATA_TYPE prevData		  = initialData;
	const U32F oldestInstance = numInstances - 1;

	for (U32F iInstance = 0; iInstance < numInstances; ++iInstance)
	{
		const U32F instanceIndex = numInstances - iInstance - 1;
		const AnimStateInstance* pInstance = pTrack->GetInstance(instanceIndex);
		if (!pInstance)
			continue;

		DATA_TYPE newData;
		if (!GetDataForInstance(pInstance, &newData))
			continue;

		OnHasDataForInstance(pInstance, newData);

		const bool isOldestInstance = (instanceIndex == oldestInstance);
		const float masterFade		= isOldestInstance ? 1.0f : pInstance->MasterFade();
		const float animFade		= isOldestInstance ? 1.0f : pInstance->AnimFade();
		const float motionFade		= isOldestInstance ? 1.0f : pInstance->MotionFade();

		prevData = BlendData(prevData, newData, masterFade, animFade, motionFade);
	}

	return prevData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
DATA_TYPE AnimStateLayer::InstanceBlender<DATA_TYPE>::BlendBackward(const AnimStateLayer* pStateLayer,
																	DATA_TYPE initialData)
{
	if (!pStateLayer)
		return initialData;

	const U32F numTracks = pStateLayer->m_numTracks;
	DATA_TYPE prevData	 = initialData;

	for (U32F iTrack = 0; iTrack < numTracks; ++iTrack)
	{
		const AnimStateInstanceTrack* pTrack = pStateLayer->m_ppTrackList[iTrack];
		if (!pTrack)
			continue;

		DATA_TYPE newData	   = GetDataForTrackBackward(pTrack, prevData);
		const float masterFade = pTrack->MasterFade();
		const float animFade   = pTrack->AnimFade();
		const float motionFade = pTrack->MotionFade();

		prevData = BlendData(prevData, newData, masterFade, animFade, motionFade);
	}

	return prevData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
DATA_TYPE AnimStateLayer::InstanceBlender<DATA_TYPE>::GetDataForTrackBackward(const AnimStateInstanceTrack* pTrack,
																			  DATA_TYPE initialData)
{
	const U32F numInstances	  = pTrack->GetNumInstances();
	DATA_TYPE prevData		  = initialData;
	const U32F oldestInstance = numInstances - 1;

	for (U32F iInstance = 0; iInstance < numInstances; ++iInstance)
	{
		const AnimStateInstance* pInstance = pTrack->GetInstance(iInstance);
		if (!pInstance)
			continue;

		DATA_TYPE newData;
		if (!GetDataForInstance(pInstance, &newData))
			continue;

		OnHasDataForInstance(pInstance, newData);

		const bool isOldestInstance = (iInstance == oldestInstance);
		const float masterFade		= isOldestInstance ? 1.0f : pInstance->MasterFade();
		const float animFade		= isOldestInstance ? 1.0f : pInstance->AnimFade();
		const float motionFade		= isOldestInstance ? 1.0f : pInstance->MotionFade();

		prevData = BlendData(prevData, newData, masterFade, animFade, motionFade);
	}

	return prevData;
}
