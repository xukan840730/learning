#include <sstream>
#include <algorithm>
#include <cctype>

#include "db2-query-element.h"
#include "db2-facade.h"
#include "common/hashes/ingamehash.h"
namespace libdb2
{
	ElementFacade::ElementFacade(const QueryElement& queryElement) :
		m_fileName(queryElement.Source()->Filename()),
		m_name(queryElement.Source()->Name()),
		m_diskPath(queryElement.Source()->DiskPath()),
		m_fullName(queryElement.Source()->DbPath()),
		m_typedDbPath(queryElement.Source()->TypedDbPath()),
		m_loaded(&queryElement != (QueryElement *)&(QueryElement::null)),
		m_xml(queryElement.Xml())
	{
	}


	AttributeFacade::AttributeFacade(const QueryElement& queryElement, const std::string& attributeName) :
		ElementFacade(queryElement),
		m_present(queryElement.Value(attributeName).first),
		m_attributeValue(queryElement.Value(attributeName).second),
		m_attributeName(attributeName)
	{
	}



	ParsedFacade::ParsedFacade(const QueryElement& elem, const std::string& attributeName) :
		AttributeFacade(elem, attributeName),
		m_fullyParsed(false)
	{
	}


	void SkipSpaces(std::istringstream& strm)
	{
		while (strm.peek() == ' ' || strm.peek() == '\t')
			strm.ignore(1);
	}
	bool Consume(std::istringstream& strm, const std::string& stringToConsume)
	{

		for (unsigned i = 0; i < stringToConsume.length(); i++)
		{
			if (strm.peek() == stringToConsume[i])
				strm.ignore(1);
			else
				return false;
		}
		return true;
	}
	bool Consume(std::istringstream& strm, char characterToConsume)
	{
		if (strm.peek() == characterToConsume)
		{
			strm.ignore(1);
			return true;
		}
		return false;
	}

	template <typename T, int kNbElems> int ParseGenericVector(T *pDst, const std::string &value)
	{
		std::istringstream strm(value);
		int i = 0;
		SkipSpaces(strm);
		if (kNbElems > 1)
			Consume(strm, '(');
		for (i = 0; i < kNbElems; i++)
		{
			///skip any number of spaces before parsing the element
			SkipSpaces(strm);
			if (!strm.eof())
			{
				if (!ParseScalar(pDst + i, strm))
					return i;
			}
			if (kNbElems > 1)
			{
				//skip any number of spaces
				SkipSpaces(strm);
				Consume(strm, ',');   //try to consume plausible separators
				Consume(strm, ';');   //
				Consume(strm, ':');   //
				Consume(strm, '|');   //
			}
		}
		if (kNbElems > 1)
			Consume(strm, ')');

		return i;
	}

	SMath::Vector ParseVector(const QueryElement &queryElement, const std::string &name, SMath::Vector def)
	{
		float v[3] = { 0.0, 0.0, 0.0 };
		int c = ParseGenericVector<float, 3>(v, queryElement.Value(name).second);
		if (c == 3)
			return SMath::Vector(v[0], v[1], v[2]);
		return def;
	}

	SMath::Vector ParseVector4(const QueryElement &queryElement, const std::string &name, SMath::Vector def)
	{
		float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		int c = ParseGenericVector<float, 4>(v, queryElement.Value(name).second);
		if (c == 4)
			return SMath::Vector(v);
		return def;
	}

	std::string CanonicalizePath(const std::string& base)
	{
		std::string ret(base);
		std::transform(ret.begin(), ret.end(), ret.begin(), tolower);
		std::replace(ret.begin(), ret.end(), '\\', '/');
		auto newEnd = std::unique(ret.begin(), ret.end(), [](char aa, char bb)
		{
			return (aa == '/') && (aa == bb);
		});
		ret.resize(newEnd - ret.begin());
		return ret;
	}

}