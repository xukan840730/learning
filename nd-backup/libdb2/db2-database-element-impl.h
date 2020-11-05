#pragma once
#define LIBXML_STATIC
#include "tools/libs/libxml2/include/libxml/parser.h"
namespace libdb2
{
	class DatabaseElement;
	class Database;
	class DatabaseElementImp
	{
	public:
		DatabaseElement&  m_owner;
		const Database& 	m_owningDatabase;
		mutable xmlDocPtr m_document;
		DatabaseElementImp(DatabaseElement &owner);
		~DatabaseElementImp();
		DatabaseElementImp(DatabaseElement &owner, const DatabaseElementImp& other);
		void Read() const;
	};
}
