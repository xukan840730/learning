#pragma once
#include "db2-facade.h"
namespace libdb2
{
	class CutScene
	{
	public:
		const std::string	m_sceneFile;
		float				m_startFrame;
		float				m_endFrame;
		float				m_clipFrames;

		CutScene(const QueryElement queryElement)
			: m_sceneFile(queryElement.Value("sceneFile").second)
			, m_startFrame(ParseScalar<float>(queryElement, "frameStart", 0.0f))
			, m_endFrame(ParseScalar<float>(queryElement, "frameEnd", 0.0f))
			, m_clipFrames(ParseScalar<float>(queryElement, "clipFrames", 0.0f))
		{
		}
	private:
		~CutScene() {};
	};
}

