/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/libs/libdb2/db2-anim.h"

#include "tools/libs/libdb2/db-query-facades.h"
#include <iostream>

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::Anim::Anim(const QueryElement& queryElement)
	: ElementFacade(queryElement)
	, m_animationSceneFile(queryElement.Value("animationSceneFile").second)
	, m_animId(queryElement.Value("animId").second)
	, m_animationStartFrame(queryElement.Element("overrides").Element("animationStartFrame"))
	, m_animationEndFrame(queryElement.Element("overrides").Element("animationEndFrame"))
	, m_animationTimeScale(queryElement.Element("overrides").Element("animationTimeScale"))
	, m_animationSampleRate(queryElement.Element("overrides").Element("animationSampleRate"))
	, m_animationPlaybackRate(queryElement.Element("overrides").Element("animationPlaybackRate"))
	, m_flags(queryElement.Element("flags"))
	, m_sets(queryElement.Element("sets").Value("content").second)
	, m_actorRef(queryElement.Value("actorRef").second)
	, m_jointCompression(queryElement.Value("compression").first ? queryElement.Value("compression").second : "normal")
	, m_channelCompression(queryElement.Value("channelCompression").first
							   ? queryElement.Value("channelCompression").second
							   : "default")
	, m_exportNamespace(queryElement.Value("prefix").second)
	, m_refAnimField(queryElement, "refAnim")
	, m_refAnimDbPath(queryElement.Value("refAnim").second)
{

	std::replace(m_actorRef.begin(), m_actorRef.end(), '\\', '/'); // normalize to forward slashes
	// force these to enabled, (default value is 0 if not present)
	m_animationStartFrame.m_enabled = true;
	m_animationEndFrame.m_enabled	= true;
	m_animationSampleRate.m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::Anim::Anim(const Anim& model)
	: ElementFacade(model)
	, m_animationSceneFile(model.m_animationSceneFile)
	, m_animId(model.m_animId)
	, m_animationStartFrame(model.m_animationStartFrame)
	, m_animationEndFrame(model.m_animationEndFrame)
	, m_animationTimeScale(model.m_animationTimeScale)
	, m_animationSampleRate(model.m_animationSampleRate)
	, m_animationPlaybackRate(model.m_animationPlaybackRate)
	, m_flags(model.m_flags)
	, m_sets(model.m_sets)
	, m_actorRef(model.m_actorRef)
	, m_jointCompression(model.m_jointCompression)
	, m_channelCompression(model.m_channelCompression)
	, m_exportNamespace(model.m_exportNamespace)
	, m_refAnimField(model.m_refAnimField)
	, m_refAnimDbPath(model.m_refAnimDbPath)
{
	m_animationStartFrame.m_enabled = true;
	m_animationEndFrame.m_enabled	= true;
	m_animationSampleRate.m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const libdb2::Anim* libdb2::Anim::RefAnim() const
{
	std::string refAnimDbPath	   = m_refAnimDbPath.substr(0, m_refAnimDbPath.rfind(".anim.xml"));
	const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("anim", refAnimDbPath);
	if (!pDbElem)
		return NULL;

	QueryElement queryElement(pDbElem);
	Anim* pRefAnim = new Anim(queryElement.Element("anim"));
	return pRefAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::string libdb2::Anim::GetLightAnimName(const libdb2::Actor* pDbActor) const
{
	if (pDbActor->m_sequence.Loaded())	// cinematic animations have more strict naming conventions
	{
		const std::string animNameExcludingActor = Name().substr(Name().find('='));
		const std::string skelSuffix = animNameExcludingActor.substr(animNameExcludingActor.find_last_of('=') + 1);
		return "light-skel" + skelSuffix + animNameExcludingActor;
	}
	else	// igc
	{
		return Name() + "--light-skel";
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::string libdb2::Anim::GetLightSkelSceneFileName(const libdb2::Actor* pDbActor) const
{
	// cinematics have more restrictive naming conventions
	if (pDbActor->m_sequence.Loaded())
		return pDbActor->Name() + '/' + m_animationSceneFile;
	else
		return m_animationSceneFile;
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::AnimOverride::AnimOverride(const QueryElement& queryElement)
	: m_enabled(queryElement.Value("enabled").second == std::string("true"))
	, m_value(ParseScalar<float>(queryElement, "value", 0.0f))
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
libdb2::AnimFlags::AnimFlags(const QueryElement& queryElement)
	: m_Looping(queryElement.Value("looping").second == std::string("true"))
	, m_Streaming(queryElement.Value("streaming").second == std::string("true"))
	, m_EmbedLastChunk(queryElement.Value("embedLastChunk").second == std::string("true"))
	, m_CenterOfMass(queryElement.Value("centerOfMass").second == std::string("true"))
	, m_Disabled(queryElement.Value("disabled").second == std::string("true"))
	, m_noBoExport(false)
	, m_exportLightAnim(queryElement.Value("exportLightAnim").second == std::string("true"))
	, m_exportCameraCutAnim(queryElement.Value("exportCameraCutAnim").second == std::string("true"))
{
}
