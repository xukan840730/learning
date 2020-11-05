/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/libs/libdb2/db2-anim.h"
#include "tools/libs/libdb2/db2-facade.h"

namespace libdb2
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	class BundleRef : public ElementFacade
	{
	public:
		std::string m_path;
		std::string m_name; // path stripped from all prefix and extension

		BundleRef(const QueryElement& queryElement)
			: ElementFacade(queryElement)
			, // we are directly handled a ref no need to search for it
			m_path(queryElement.Value("path").second)
			, m_name(m_path.substr(m_path.rfind("\\") + 1, m_path.rfind(".bundle.xml") - m_path.rfind("\\") - 1))
		{
		}
		BundleRef(const BundleRef& model) : ElementFacade(model), m_path(model.m_path), m_name(model.m_name) {}
	};
	typedef ListFacade<BundleRef> BundleRefList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimRef : public ElementFacade
	{
	public:
		std::string m_path;
		std::string m_name; // path stripped from all prefix and extension

		AnimRef(const QueryElement& queryElement)
			: ElementFacade(queryElement)
			, // we are directly handled a ref no need to search for it
			m_path(queryElement.Value("path").second)
			, m_name(m_path.substr(0, m_path.rfind(".anim.xml")))
		{
		}
		AnimRef(const AnimRef& model) : ElementFacade(model), m_path(model.m_path), m_name(model.m_name) {}
	};
	typedef ListFacade<AnimRef> AnimRefList;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class AnimList : public ListFacade<const Anim*>
	{
	public:
		AnimList(const QueryElement& elem,
				 const BundleRefList& bundleRefs,
				 const AnimRefList& animRefs,
				 const AnimRefList& linkedAnimRefs);

		void AddAnim(const Anim* pAnim);
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class Bundle : public ElementFacade
	{
	public:
		BundleRefList m_bundles;
		AnimRefList m_animations;
		AnimRefList m_linkedAnimations;
		AnimList m_allAnimations;

		Bundle(const QueryElement& queryElement)
			: ElementFacade(queryElement)
			, m_bundles(queryElement.Element("bundles"), "ref")
			, m_animations(queryElement.Element("animations"), "ref")
			, m_linkedAnimations(queryElement.Element("linkedAnimations"), "ref")
			, m_allAnimations(queryElement, m_bundles, m_animations, m_linkedAnimations)
		{
		}

	protected:
		virtual std::string Prefix() const { return "Bundle." + ElementFacade::Prefix(); }
	private:
		~Bundle() {}
	};
} // namespace libdb2
