#ifndef BUILDBIG2DATABASEELEMENT_INCLUDE
#define BUILDBIG2DATABASEELEMENT_INCLUDE
#include <string>
#include <stdint.h>


namespace libdb2
{
	class Database;
	class DatabaseElementImp;

	
class DatabaseElement
{
	friend class DatabaseElementImp;
	friend class QueryElementImp;

	std::string mPath;
	std::string mFilename;
	std::string mName;
	std::string mType;
	bool	m_isLink;

	const libdb2::Database *mOwningDatabase;
	int64_t		m_lastWriteTime;
	void *m_document;		// this needs to come before the pimple because the pimple will fill it in.
	DatabaseElementImp *pimpl_;
protected:
	const void *GetDocument() const { return m_document; }
public:
	DatabaseElement(const std::string& path, const std::string& filename, const libdb2::Database *owner, int64_t lastWrireTime); //I wished owner was a ref, but it was to late when I decided to implement it to get it right quicly
	DatabaseElement(const DatabaseElement& other);
	~DatabaseElement();

	const libdb2::Database *Database() const
	{
		return mOwningDatabase;
	}

	const std::string&  Filename() const
	{
		return mFilename;
	}
	const std::string&  Name() const
	{
		return mName;
	}
	std::string DiskPath() const;
	std::string DbPath() const;
	std::string TypedDbPath() const;

	const std::string&  Type() const 
	{
		return mType;
	}
	bool HasDoc() const;

	void Read() const;
	int64_t LastWriteTime() const { return m_lastWriteTime; };
};
}
#endif