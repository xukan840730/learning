#ifndef BUILDBIG2QUERYELEMENT_INCLUDE
#define BUILDBIG2QUERYELEMENT_INCLUDE

#include <string>
#include <unordered_map>
#include <list>
//#include "tools/libs/xercesc/include/xercesc/dom/DOM.hpp"
#include "db2-database-element.h"
#include "db2-database.h"

namespace libdb2
{
class DatabaseElement;
class Database;
class QueryElementImp;


struct GameFlagsInfo
{
	std::string m_gameFlagsPath;
	std::string m_gameFlags;
	int64_t m_lastWriteTime = 0;
};

class QueryElement
{
	friend class QueryElementImp;
	QueryElementImp* m_pImpl_;
protected:

	std::unordered_multimap<std::string, std::string> m_attributes;
	typedef std::unordered_map<std::string, std::list<QueryElement> > ElementMap;
	ElementMap m_elements;
	std::string m_type;
	static std::string empytString;
	const DatabaseElement * dataSource;
	std::string m_elementSource;
		
protected:
	QueryElement();
public:
	static class QueryElementFalse null;
	QueryElement(const DatabaseElement *source);
	QueryElement(const QueryElement &model);  //copy constructor
	const libdb2::Database *Database() const {return dataSource->Database();} 
	const libdb2::DatabaseElement *Source() const {return dataSource;} 
	virtual std::string Xml() const { return m_elementSource; }

	virtual const std::pair<const bool, std::string> Value(const std::string& name) const;				// 'Maybe' - 'bool' = IsDefined? and 'string' is the value of the parameter
	virtual const QueryElement& Element(const std::string& name) const;
	virtual const std::vector<QueryElement> Elements(const std::string& name) const;

	static void ReadGameFlags(std::string& gameflags, std::string &gameflagsPath, int64_t& lastWriteTime, const QueryElement&element);	// this is kludgy to avoid code duplication reading the gameflags
																																				// element should only be an actor or a level (not even a subpart of these)
};

class QueryElementFalse : public QueryElement
{
	const static std::vector<QueryElement> emptyArray;
	const static DatabaseElement emptyElement;
	const static NullDatabase gs_nullDatabase;
public:
	QueryElementFalse()
	{
		dataSource = &emptyElement;
	}
	virtual const std::pair<const bool, std::string> Value(const std::string& name) const
	{
		return std::pair<const bool, std::string>(false, empytString);
	}

	virtual  const QueryElement& Element(const std::string& name) const
	{
		return *this;
	}

	virtual  const std::vector<QueryElement> Elements(const std::string& name) const
	{
		return emptyArray;
	}
	virtual std::string Xml() const { return ""; }
};


}
#endif