/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/libs/libdb2/db2-facade.h"

namespace libdb2
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimFlags
	{
	public:
		AnimFlags(const QueryElement& queryElement);

		bool m_Looping;
		bool m_Streaming;
		bool m_EmbedLastChunk;
		bool m_CenterOfMass;
		bool m_Disabled;
		bool m_noBoExport;
		bool m_exportLightAnim;
		bool m_exportCameraCutAnim;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimOverride
	{
	public:
		bool m_enabled;
		float m_value;

		AnimOverride(const QueryElement& queryElement);
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class Anim : public ElementFacade
	{
	public:
		std::string m_animationSceneFile;

	private:
		std::string m_animId; // does not really exist
	public:
		AnimOverride m_animationStartFrame;
		AnimOverride m_animationEndFrame;
		AnimOverride m_animationTimeScale;
		AnimOverride m_animationSampleRate;
		AnimOverride m_animationPlaybackRate;
		AnimFlags m_flags;
		std::string m_sets;
		std::string m_actorRef;
		std::string m_jointCompression;
		std::string m_channelCompression;
		std::string m_exportNamespace;
		AttributeFacade m_refAnimField;
		std::string m_refAnimDbPath;

		Anim(const QueryElement& queryElement);
		Anim(const Anim& model);

		const Anim* RefAnim() const;
		const std::string GetLightAnimName(const libdb2::Actor* pDbActor) const;
		const std::string GetLightSkelSceneFileName(const libdb2::Actor* pDbActor) const;

	protected:
		virtual std::string Prefix() const { return "Anim." + ElementFacade::Prefix(); }
	private:
		~Anim() {}
	};

} // namespace libdb2
