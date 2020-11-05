#include <iostream>
#include <utility>
#include <unordered_map>
#include <stdio.h>
#include <sys/stat.h>

#include "db2-database-element.h"
#include "db2-database-element-impl.h"
#include "db2-query-element.h"
#define LIBXML_STATIC
#include "tools/libs/libxml2/include/libxml/parser.h"

//#pragma optimize("", off) // uncomment when debugging in release mode 


//XERCES_CPP_NAMESPACE_USE

namespace libdb2
{
	// disable: 'this' : used in mase member initializer list
	#pragma warning(disable:4355)    
static 	std::string ltrim(const std::string& input, const std::string& delimiters)
{
	std::string::const_iterator begin = input.begin();
	std::string::const_iterator end   = input.end();

	for(;begin!=end && delimiters.find(*begin)!=std::string::npos; ++begin)
			;
	return std::string(begin, end);
}

static 	std::string rtrim(const std::string& input, const std::string& delimiters)
{
	std::string::const_iterator begin = input.begin();
	std::string::const_iterator end   = input.end();

	for(;begin!=end && delimiters.find(*(end-1))!=std::string::npos; --end)
			;
	return std::string(begin, end);
}

static	std::string trim(const std::string& input, const std::string& delimiters)
{
	return rtrim(ltrim(input, delimiters), delimiters);
}
static const std::string gs_fieldTrims(" ");


class QueryElementImp
{
	QueryElement& m_owner;

public:
	QueryElementImp(QueryElement& owner):
		m_owner(owner)
	{
	}

	void ImportNode(const xmlNodePtr node)
	{
		if (node)
		{

			if ((const char*)node->name)
			{
				m_owner.m_type = trim(std::string((const char*)node->name), gs_fieldTrims);
				xmlDoc * doc = (xmlDoc *)const_cast<void *>(m_owner.dataSource->GetDocument());
				xmlBufferPtr buffer = xmlBufferCreate();
				xmlNodeDump(buffer, doc, node, 0, 0);
				m_owner.m_elementSource = std::string((const char*)xmlBufferContent(buffer));

			}

			xmlAttributePtr	attribute = ((const xmlElementPtr)(node))->attributes;
			while (attribute!=0)
			{
				xmlChar *propValue = xmlGetProp(node, attribute->name);

				std::string attrname (trim(std::string((char*)attribute->name), gs_fieldTrims));
				std::string propvalue(trim(std::string((char*)propValue),       gs_fieldTrims));

				m_owner.m_attributes.insert(std::pair<std::string, std::string>(attrname, propvalue));
				xmlFree(propValue);
				attribute= (xmlAttributePtr)(attribute->next);
			}

			xmlNode *child = node->children;
			while(child)
			{
				if (child->type == XML_ELEMENT_NODE)
				{
					QueryElement queryChild;
					queryChild.dataSource = m_owner.dataSource;
					queryChild.m_pImpl_->ImportNode(child);
					m_owner.m_elements[queryChild.m_type].push_back(queryChild);
				}
				child = child->next;
			}
		}
	}

	void ImportDoc()
	{
		ImportNode((xmlNodePtr)m_owner.dataSource->pimpl_->m_document);
	}
};





const NullDatabase QueryElementFalse::gs_nullDatabase;
const std::vector<QueryElement> QueryElementFalse::emptyArray;
const DatabaseElement  QueryElementFalse::emptyElement("", "", &QueryElementFalse::gs_nullDatabase, 0);

QueryElementFalse QueryElement::null;
std::string QueryElement::empytString("");


QueryElement::QueryElement():
dataSource(0),
m_pImpl_(new QueryElementImp(*this))
{
}

QueryElement::QueryElement(const DatabaseElement *source):
dataSource(source),
m_pImpl_(new QueryElementImp(*this))
{
	m_pImpl_->ImportDoc();
}


QueryElement::QueryElement(const QueryElement &model):
m_attributes(model.m_attributes),
m_elements(model.m_elements),
m_type(model.m_type),
dataSource(model.dataSource),
m_elementSource(model.m_elementSource),
m_pImpl_(new QueryElementImp(*this))
{

}


const std::pair<const bool, std::string> QueryElement::Value(const std::string& name) const
{
	std::unordered_multimap<std::string, std::string>::const_iterator attributeIt;
	if ((attributeIt = m_attributes.find(name)) != m_attributes.end())
		return std::pair<const bool, std::string>(true, (*attributeIt).second);
	return std::pair<const bool, std::string>(false, empytString);
}

const QueryElement& QueryElement::Element(const std::string& name) const
{
	ElementMap::const_iterator elementIt;
	if ((elementIt = m_elements.find(name)) != m_elements.end())
	{
		return (*elementIt).second.front();
	}
	return (QueryElement&)QueryElement::null;
}

const std::vector<QueryElement> QueryElement::Elements(const std::string& name) const
{
	std::vector<QueryElement>	vec;
	ElementMap::const_iterator elementIt;
	if ((elementIt = m_elements.find(name)) != m_elements.end())
	{
		for (std::list<QueryElement>::const_iterator it = (*elementIt).second.begin(); 
			it!=(*elementIt).second.end(); ++it)
		{
			vec.push_back(*it);
		}
	}
	return vec;
/*
	std::pair<ElementMap::const_iterator, ElementMap::const_iterator> range = mElements.equal_range(name);
	ElementMap::const_iterator it;
//	return std::vector<QueryElement>(range.first, range.second);
	std::vector<QueryElement>	vec;
	for (it = range.first; it!=range.second; ++it)
	{
		vec.push_back((*it).second);

	}
	return vec;*/
}

void QueryElement::ReadGameFlags(std::string& gameflags, std::string &gameflagsPath, int64_t& lastWriteTime, const QueryElement&element)
{
	const DatabaseElement *source =  element.Source();
	if (source)
	{
		const std::string dbBasePath = source->Database()->BasePath();
		const std::string& type = source->Type();
		bool IsActor = type == "actor";
		bool IsLevel = type == "level";
	
		if (IsActor || IsLevel)
		{
			size_t  pos = dbBasePath.rfind("/");		// skip the name of the builderdb
			if (pos != std::string::npos)
			{
				gameflagsPath = dbBasePath.substr(0, pos) + "/gameflagdb/builder/" + type + "s/" + source->Name() + "." + type + ".json";
				struct stat stats;
				stats.st_mtime = 0;
				stat(gameflagsPath.c_str(), &stats);
				lastWriteTime = stats.st_mtime;



				FILE * fin = fopen(gameflagsPath.c_str(), "rb");
				if (fin)
				{
					fseek(fin, 0, SEEK_END);
					gameflags.resize(ftell(fin));
					fseek(fin, 0, SEEK_SET);
					if (gameflags.size())
						fread(&gameflags[0], gameflags.size(), 1, fin);
					fclose(fin);
				}
			} 
		}
	}
}


}
