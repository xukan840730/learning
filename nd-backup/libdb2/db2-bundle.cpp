/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/libs/libdb2/db2-bundle.h"

#include "tools/libs/libdb2/db-query-facades.h"

#include <iostream>

namespace libdb2
{
	const Anim* GetAnim(const std::string& nameOrPath);
	const Bundle* GetBundle (const std::string& nameOrPath);

	AnimList::AnimList(const QueryElement& queryElement,
					   const BundleRefList& bundleRefs,
					   const AnimRefList& animRefs,
					   const AnimRefList& linkedAnimRefs)
		: ListFacade<const Anim*>(queryElement)
	{
		std::vector<AnimRef>::const_iterator animIt	   = animRefs.begin();
		std::vector<AnimRef>::const_iterator animEndIt = animRefs.end();
		for (; animIt != animEndIt; ++animIt)
		{
			const AnimRef& ref(*animIt);
			const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("anim", ref.m_path);
			if (pDbElem)
			{
				//QueryElement animQueryElement(pDbElem);
				//m_elements.push_back(Anim(animQueryElement.Element("anim")));
				m_elements.push_back(GetAnim(ref.m_path));
			}
			else
			{
				std::cerr << "animation " << ref.m_path << " not found\n";
			}
		}

		animIt	  = linkedAnimRefs.begin();
		animEndIt = linkedAnimRefs.end();
		for (; animIt != animEndIt; ++animIt)
		{
			const AnimRef& ref(*animIt);
			const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("anim", ref.m_path, true);
			if (pDbElem)
			{
				//QueryElement animQueryElement(pDbElem);
				//m_elements.push_back(Anim(animQueryElement.Element("anim")));
				m_elements.push_back(GetAnim(ref.m_path));
			}
			else
			{
				std::cerr << "animation " << ref.m_path << " not found [1]\n";
			}
		}

		std::vector<BundleRef>::const_iterator bundleIt	   = bundleRefs.begin();
		std::vector<BundleRef>::const_iterator bundleEndIt = bundleRefs.end();
		for (; bundleIt != bundleEndIt; ++bundleIt)
		{
			const BundleRef& ref(*bundleIt);
			std::string bundlepath(ref.m_path);
			size_t pos;
			if ((pos = bundlepath.rfind(".bundle/")) != std::string::npos)
				bundlepath = bundlepath.substr(0, pos);

			//const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("bundle", bundlepath);
			//if (pDbElem)
			//{
			//	QueryElement bundleQueryElement(pDbElem);
			//	Bundle bundle(bundleQueryElement.Element("bundle"));
				
			const Bundle* pBundle = GetBundle(bundlepath);
			std::vector<const Anim*>::const_iterator animIt	= pBundle->m_allAnimations.begin();
			std::vector<const Anim*>::const_iterator animEndIt = pBundle->m_allAnimations.end();
			for (; animIt != animEndIt; ++animIt)
			{
				m_elements.push_back(*animIt);
			}
			//}
			//else
			//{
			//	//			IWARN("%s not found", bundlepath);
			//	std::cerr << bundlepath << " not found\n";
			//}
		}
	}

	void AnimList::AddAnim(const Anim* pAnim) { m_elements.push_back(pAnim); }

} // namespace libdb2
