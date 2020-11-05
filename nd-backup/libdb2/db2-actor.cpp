/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/libs/libdb2/db2-actor.h"

#include "common/util/fileio.h"
#include "common/util/msg.h"

#include "tools/libs/libdb2/db-query-facades.h"
#include "tools/libs/libdb2/db2-anim.h"
#include "tools/libs/toolsutil/simpledb.h"

#include <iostream>

//#pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
void libdb2::Actor::MergeMotionMatchAnimsToAnimList()
{
	for (const std::shared_ptr<MotionMatchSet>& pMotionSet : m_motionMatchSets)
	{
		for (const Anim* pAnim : pMotionSet->Anims())
		{
			AnimList::const_iterator it = std::find_if(m_animations.begin(), m_animations.end(), [&](const Anim* pa) {
				return pa->Filename() == pAnim->Filename();
			});
			if (it == m_animations.end())
			{
				if (pAnim->m_flags.m_noBoExport)
					m_animations.AddAnim(pAnim);
				else
				{
					Anim* pAnimNoExport = new Anim(*pAnim);		// SNAP WE NEED TO DUPLICATE HERE
					pAnimNoExport->m_flags.m_noBoExport = true;
					m_animations.AddAnim(pAnimNoExport);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::CinematicBinding::CinematicBinding(const CinematicSequence& sequence, const Anim* pAnim)
{
	m_animName = pAnim->Name();

	std::string::size_type iPrefixEnd = pAnim->Name().find_first_of('=');
	if (iPrefixEnd != std::string::npos)
		m_aliasName = pAnim->Name().substr(0, iPrefixEnd);
	else
		m_aliasName = "";

	m_lookName		= ""; // default the look name to the skeleton actor name if possible
	m_audioStem		= "";
	m_parentToAlias = "";
	m_parentJointOrAttach = "";
	m_attachSysId		  = "";
	m_tintRandomizer	  = (U32)-1;

	// first try to find the explicit look name in our binding info table
	int numBindingInfos = sequence.m_bindingInfoList.size();
	for (int i = 0; i < numBindingInfos; ++i)
	{
		const CinematicBindingInfo& bindingInfo = sequence.m_bindingInfoList[i];
		if (bindingInfo.m_animAlias == m_aliasName)
		{
			m_lookName		= bindingInfo.m_lookName;
			m_audioStem		= bindingInfo.m_audioStem;
			m_parentToAlias = bindingInfo.m_parentToAlias;
			m_parentJointOrAttach = bindingInfo.m_parentJointOrAttach;
			m_attachSysId		  = bindingInfo.m_attachSysId;
			m_tintRandomizer	  = bindingInfo.m_tintRandomizer.empty() ? (U32)-1
																	: atoi(bindingInfo.m_tintRandomizer.c_str());
			break;
		}
	}

	if (m_lookName.empty())
	{
		// no explicit look name - try to use the skeleton actor as the look name
		std::string actorRefFilename(FileIO::extractFilename(pAnim->m_actorRef.c_str()));
		std::replace(actorRefFilename.begin(), actorRefFilename.end(), '\\', '/');
		const char* ext = FileIO::extractLongestExtension(actorRefFilename.c_str());
		if (ext != NULL)
		{
			--ext; // back up to include the dot
			ASSERT(*ext == '.');
			size_t iExt = ext - actorRefFilename.c_str();
			m_lookName	= actorRefFilename.substr(0, iExt);
		}
	}
}
