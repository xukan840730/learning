#pragma once
#include <sstream>
#include <stdint.h>
#include <stdexcept>

#include "shared/math/vector.h"
#include "db2-query-element.h"

namespace libdb2
{

	//--------------------------------------------------------------------------------------------------------------------//
	class ElementFacade
	{
	protected:

		std::string m_fileName;
		std::string m_name;
		std::string m_diskPath;
		std::string m_fullName;
		std::string m_uniqueName;
		std::string m_typedDbPath;

		bool m_loaded;

		// Hack
		std::string m_xml;

	public:
		
		ElementFacade(const QueryElement& queryElement);

		ElementFacade(const ElementFacade& elem) : 
			m_fileName(elem.m_fileName),
			m_name(elem.m_name),
			m_diskPath(elem.m_diskPath),
			m_fullName(elem.m_fullName),
			m_uniqueName(elem.m_uniqueName),
			m_typedDbPath(elem.m_typedDbPath),
			m_loaded(elem.m_loaded),
			m_xml(elem.m_xml)
		{
		}
	
		const std::string&  Filename()	const {return m_fileName;}

		//eg: hero
		virtual std::string  Name()		const {return m_name;}

		//eg: C:\temp\export\Output\characters\hero\actors\hero\hero.actor.xml
		const std::string&			DiskPath()	const {return m_diskPath;}
		
		//eg: characters\hero\actors\hero\hero
		virtual std::string	FullName()	const {return m_fullName;}

		//eg: characters\hero\actors\hero\hero.actor.xml
		const std::string& TypedDbPath() const {return m_typedDbPath;}

		virtual bool  Loaded() const { return m_loaded; }

		const std::string &Xml() const { return m_xml; }

		//eg: Actor.hero.characters\hero\actors\hero\hero
		const std::string UniqueName() const {return Prefix() + FullName();}

	protected:
		virtual std::string Prefix() const {return Name()+".";}
	};


	//--------------------------------------------------------------------------------------------------------------------//
	class AttributeFacade : public ElementFacade
	{
		 std::string m_attributeName;

	protected:
		mutable bool m_present;
		mutable std::string m_attributeValue;
		virtual std::string Prefix() const{return  "attribute." + m_attributeName + "." ;}

	public:
		const std::string& Value() const {return m_attributeValue;}
		AttributeFacade(const QueryElement& elem, const std::string& attributeName);
	};


	//--------------------------------------------------------------------------------------------------------------------//
	template <typename T> class ListFacade : public ElementFacade
	{
	protected:
		std::vector<T> m_elements;
	public:
		typedef typename std::vector<T>::iterator   iterator;  
		typedef typename std::vector<T>::const_iterator const_iterator;

		ListFacade(const QueryElement& queryElement)
			: ElementFacade(queryElement)
		{
		}

		ListFacade(const QueryElement& queryElement, const std::string& elementsName)
			: ElementFacade(queryElement)
		{
			const std::vector<QueryElement>& elementQueries = queryElement.Elements(elementsName);

			m_elements.reserve(elementQueries.size());
			for (std::vector<QueryElement>::const_iterator it = elementQueries.begin(); it != elementQueries.end(); ++it)
			{
				m_elements.push_back( T((*it)) );
			}
		}
		const_iterator begin() const
		{
			return m_elements.begin();
		}
		const_iterator end() const
		{
			return m_elements.end();
		}
		size_t size() const
		{
			return m_elements.size();
		}
		size_t empty() const
		{
			return m_elements.empty();
		}
		const T& operator[](size_t index) const { return m_elements[index]; }
	protected:
		virtual std::string Prefix() const{return  "List." + ElementFacade::Prefix() ;}
	};

	void SkipSpaces(std::istringstream& strm);
	bool Consume(std::istringstream& strm, const std::string& stringToConsume);
	bool Consume(std::istringstream& strm, char characterToConsume);

	//--------------------------------------------------------------------------------------------------------------------//
	class ParsedFacade : public AttributeFacade
	{
		
	protected:
		bool m_fullyParsed;

		ParsedFacade(const QueryElement& queryElement, const std::string& attributeName);

		virtual void Parse() = 0;
		virtual std::string Prefix() const{return  "Parsed." + AttributeFacade::Prefix();}
	public:
		bool FullyParsed() const {return m_fullyParsed;}
	};


	//--------------------------------------------------------------------------------------------------------------------//
	template <typename T> class Parsed
	{
		T	 m_value;
		bool m_parsed;
	public:
		Parsed():
		  m_parsed(false),
			m_value(T())
		  {

		  }
		  Parsed(std::istringstream &strm)
		  {
			  Parse(strm);
		  }
		  bool Parse(std::istringstream &strm)
		  {
		  	strm>>m_value;
		  	m_parsed = !strm.fail();
		  	return m_parsed;
		  }
		  operator const T& () const{return m_value;}
		  bool IsParsed() const{return m_parsed;}

	};

	template <> class Parsed<double>  // specialized so that non numeric strings do not parse as 0 and we do not detect 0 as bein an un/parsed value
	{
		double	 m_value;
		bool m_parsed;
	public:
		Parsed():
		  m_parsed(false),
			m_value(0.f)
		  {
		  }
		  Parsed(std::istringstream &strm)
		  {
			  Parse(strm);
		  }
		  bool Parse(std::istringstream &strm)
		  {

			  if (isdigit(strm.peek()))
			  {
				 strm>>m_value;
				 m_parsed = true;
			  }
			  return m_parsed;
		  }
		  operator double () const{return m_value;}
		  bool IsParsed() const{return m_parsed;}

	};

	template <typename T> bool ParseScalar(T *pDst, std::istringstream &strm) { return false; }
	template <> inline bool ParseScalar<double>(double *pDst, std::istringstream &strm)
	{
		if (isdigit(strm.peek()) || strm.peek() == '-' || strm.peek() == '.')
		{
			strm >> *pDst;
			return true;
		}
		return false;
	}

	template <> inline bool ParseScalar<float>(float *pDst, std::istringstream &strm)
	{
		if (isdigit(strm.peek()) || strm.peek() == '-' || strm.peek() == '.')
		{
			strm >> *pDst;
			return true;
		}
		return false;
	}

	template <> inline bool ParseScalar<int>(int *pDst, std::istringstream &strm)
	{
		if (isdigit(strm.peek()) || strm.peek() == '-')
		{
			strm >> *pDst;
			return true;
		}
		return false;
	}

	template <typename T> T ParseScalar(const QueryElement &queryElement, const std::string &valueName, T def)
	{
		std::string value = queryElement.Value(valueName).second;
		std::istringstream strm(value);
		T ret;
		if (ParseScalar<T>(&ret, strm))
			return ret;
		return def;
	}

	SMath::Vector ParseVector(const QueryElement &queryElement, const std::string &name, SMath::Vector def);
	SMath::Vector ParseVector4(const QueryElement &queryElement, const std::string &name, SMath::Vector def);

	template <typename T> std::vector<T> ParseList(const QueryElement &queryElement, const std::string &name)
	{
		const std::vector<QueryElement>& elementQueries = queryElement.Elements(name);

		std::vector<T> elements;
		elements.reserve(elementQueries.size());
		for (std::vector<QueryElement>::const_iterator it = elementQueries.begin(); it != elementQueries.end(); ++it)
		{
			elements.push_back(T((*it)));
		}

		return std::move(elements);
	}

	template <typename T, int kNbElems> class EnumeratedFacade : public ParsedFacade
	{
		Parsed<T> m_data[kNbElems];
		int m_parsedCount;

	protected:
		virtual void Parse()
		{
			std::istringstream strm(m_attributeValue);
			int i = 0;
			SkipSpaces(strm);
			if (kNbElems>1)
				Consume(strm, '(');
			for (i=0; i<kNbElems; i++)
			{
				///skip any number of spaces before parsing the element
				SkipSpaces(strm);
				if (!strm.eof())
				{
					if (!m_data[i].Parse(strm))
						break;
					m_parsedCount++;
				}
				if (kNbElems>1)
				{
					//skip any number of spaces
					SkipSpaces(strm);
					Consume(strm, ',');   //try to consume plausible separators
					Consume(strm, ';');   //
					Consume(strm, ':');   //
					Consume(strm, '|');   //
				}
			}
			if (kNbElems>1)
				Consume(strm, ')');
			m_fullyParsed = i==kNbElems;
		}

		virtual std::string Prefix() const {return  "Enumerated." + ParsedFacade::Prefix();}

	public:
		EnumeratedFacade(const QueryElement& elem, const std::string& attributeName) : 
		  ParsedFacade(elem, attributeName),
			  m_parsedCount(0)
		  {
			  if (m_present)
				  Parse();
		  }

		  int GetCount() const {return kNbElems;}
		  int ParsedCount() const {return m_parsedCount;}
		  const Parsed<T>& operator[](unsigned int index) const
		  {
			  if (index<kNbElems)
				  return m_data[index];
			  throw std::out_of_range("EnumeratedFacade operator [] index out of range");
		  }
		  const T& ParsedValue()const{return m_data[0];}

		  void CopyFrom(const EnumeratedFacade&rhs)
		  {
			  m_parsedCount = rhs.m_parsedCount;
			  for (int i = 0; i < m_parsedCount; i++)
				  m_data[i] = rhs.m_data[i];
		  }

		/*  EnumeratedFacade &operator = (const EnumeratedFacade & rhs)
		  {
			  m_parsedCount = rhs.m_parsedCount;
			  int i;
			  for (i=0; i<m_parsedCount; i++)
				  m_data[i] = rhs.m_data[i];
			  ParsedFacade::operator=((const ParsedFacade&)rhs);
			  return *this;
		  }*/

	};

	std::string CanonicalizePath(const std::string& base);


}

