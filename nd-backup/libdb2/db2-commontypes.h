#pragma once
#include "db2-facade.h"

namespace libdb2
{

	//--------------------------------------------------------------------------------------------------------------------//
	class SoundBankFile : public ElementFacade
	{
		public:
			std::string m_path;
			std::string m_filename;

			SoundBankFile(const QueryElement& queryElement)
				: ElementFacade(queryElement),
				m_path(queryElement.Value("path").second)
			{
				size_t pos = m_path.find_last_of("/");
				if (pos==std::string::npos)
					pos = 0;
				else 
					pos+=1;
				m_filename = m_path.substr(pos);
				m_filename = m_filename.substr(0, m_filename.find_last_of("."));
			
			}

			SoundBankFile(const SoundBankFile &model) :	
			ElementFacade(model),
			m_path(model.m_path),
			m_filename(model.m_filename)
			{
			}

		protected:
			virtual std::string Prefix() const{return  "SoundBankFile." + ElementFacade::Prefix();}
	};
	typedef ListFacade<SoundBankFile> SoundBankFileList;


	//--------------------------------------------------------------------------------------------------------------------//
	class IRPakFile : public ElementFacade
	{
		public:
			std::string m_path;
			std::string m_filename;

			IRPakFile(const QueryElement& queryElement) :
				ElementFacade(queryElement),
				m_path(queryElement.Value("path").second)
			{
				size_t pos = m_path.find_last_of("/");
				if (pos == std::string::npos)
					pos = 0;
				else
					pos += 1;
				m_filename = m_path.substr(pos);
				m_filename = m_filename.substr(0, m_filename.find_last_of("."));

			}

			IRPakFile(const IRPakFile &model) :
				ElementFacade(model),
				m_path(model.m_path),
				m_filename(model.m_filename)
			{
			}

		protected:
			virtual std::string Prefix() const{ return  "IRPakFile." + ElementFacade::Prefix(); }

	};
	typedef ListFacade<IRPakFile> IRPakFileList;


	//--------------------------------------------------------------------------------------------------------------------//
	class Material : public ElementFacade
	{
		public:
			mutable std::string m_name;
			mutable bool m_keepTexturesInPakFile;
			mutable bool m_keepMaterialInPakFile;

			Material(const QueryElement& queryElement):
			ElementFacade(queryElement),
			m_name(queryElement.Value("name").second),
			m_keepMaterialInPakFile( ( queryElement.Value("keepMaterialInPakFile").first == true ) ? ( queryElement.Value("keepMaterialInPakFile").second == std::string("true") ) : true ),
			m_keepTexturesInPakFile( ( queryElement.Value("keepTexturesInPakFile").first == true ) ? ( queryElement.Value("keepTexturesInPakFile").second == std::string("true") ) : true )
			{
			}

		protected:
			virtual std::string Prefix() const{return  "Material." + ElementFacade::Prefix();}
	};
	typedef ListFacade<Material>  MaterialList;


	//--------------------------------------------------------------------------------------------------------------------//
	class MaterialRemap : public ElementFacade
	{
		public:
			std::string m_materialToRemap;
			std::string m_replacingMaterial;
			bool m_useOriginalTextureSet;
			bool m_excludeTree;
			bool m_recurse;

			MaterialRemap(const QueryElement& queryElement) :
				ElementFacade(queryElement),
				m_materialToRemap(queryElement.Value("materialToRemap").second),
				m_replacingMaterial(queryElement.Value("replacingMaterial").second),
				m_useOriginalTextureSet(queryElement.Value("useOriginalTextureSet").first == true && queryElement.Value("useOriginalTextureSet").second == std::string("true")),
				m_excludeTree(queryElement.Value("excludeTree").first == true && queryElement.Value("excludeTree").second == std::string("true")),
				m_recurse(queryElement.Value("subdirectories").first == false || (queryElement.Value("subdirectories").first == true && queryElement.Value("subdirectories").second == std::string("true")))
			{
			}

		protected:
			virtual std::string Prefix() const{ return  "MaterialRemap." + ElementFacade::Prefix(); }
	};
	typedef ListFacade<MaterialRemap>  MaterialRemapList;


	//--------------------------------------------------------------------------------------------------------------------//
	class AlternateResource
	{
	public:
		std::string m_path;
		bool m_exclude;
		bool m_recurse;

		AlternateResource(const QueryElement& queryElement) 
			: m_path(queryElement.Value("path").second)
			, m_exclude(queryElement.Value("exclude").first && queryElement.Value("exclude").second == "true")
			, m_recurse(queryElement.Value("recurse").first && queryElement.Value("recurse").second == "true")
		{
		}
	};
	typedef ListFacade<AlternateResource>  AlternateResourceList;


	//--------------------------------------------------------------------------------------------------------------------//
	class AlternateResources
	{
	public:
		AlternateResourceList m_geometry;
		AlternateResourceList m_materials;
		AlternateResourceList m_textures;

		AlternateResources(const QueryElement& queryElement)
			: m_geometry(queryElement.Element("geometry"), "resource")
			, m_materials(queryElement.Element("materials"), "resource")
			, m_textures(queryElement.Element("textures"), "resource")
		{
		}
	};


	//--------------------------------------------------------------------------------------------------------------------//
	class LightmapOverrideSource
	{
	public:
		std::string m_name;

		LightmapOverrideSource(const QueryElement& queryElement)
			: m_name(queryElement.Value("name").second)
		{
		}

	};
	typedef ListFacade<LightmapOverrideSource>  LightmapOverrideSourceList;


	//--------------------------------------------------------------------------------------------------------------------//
	class LightmapsOverride
	{
		std::string m_xml;

	public:
		bool m_enabled;
		LightmapOverrideSourceList m_sources;

		LightmapsOverride(const QueryElement& queryElement)
			: m_xml(queryElement.Xml())
			, m_sources(queryElement.Element("sources"), "source")
			, m_enabled(queryElement.Value("enabled").first && queryElement.Value("enabled").second == "true")
		{
		}

		const std::string &Xml() const { return m_xml; }
	};


	//--------------------------------------------------------------------------------------------------------------------//
	class NameString
	{
	public:
		std::string m_name;

		NameString(const QueryElement& queryElement)
			: m_name(queryElement.Value("name").second)
		{
		}
	};

	struct CanonicPath 
	{
		CanonicPath(const std::string& base)
			: m_string(CanonicalizePath(base))
		{
		}
		CanonicPath() {}
		operator const std::string& () const { return m_string; }
		size_t size() const { return m_string.size(); }
		const char *c_str() const { return m_string.c_str(); }
		bool  empty() const { return m_string.empty(); }


		bool  operator==(CanonicPath&rhs) const { return m_string == rhs.m_string; }
		bool  operator==(const std::string& rhs) const { return m_string == rhs; }
		friend bool  operator==(const std::string& lhs, CanonicPath&rhs) { return lhs == rhs.m_string; }


		inline bool  operator!=(CanonicPath&rhs) const { return !(m_string == rhs);}
		inline bool  operator!=(const std::string& rhs) const { return !(m_string == rhs); }
		inline friend bool  operator!=(const std::string& lhs, CanonicPath&rhs)  { return !(lhs == rhs.m_string); }

		std::string operator+(const std::string&rhs) const { return m_string + rhs; }
		friend std::string operator+(const char* lhs, const CanonicPath&rhs) { return std::string(lhs) + rhs.m_string; }
		friend std::string operator+(const std::string& lhs, const CanonicPath&rhs) { return lhs + rhs.m_string; }

		std::string m_string;
	};
}
