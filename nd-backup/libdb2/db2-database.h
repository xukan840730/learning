#pragma once
#include <string>
#include <unordered_map>
#include <map>
#include <algorithm>


namespace libdb2
{
	inline std::string ToWinPath(const std::string& srce)
	{
		std::string temp(srce);
		std::replace(temp.begin(), temp.end(), '/', '\\');
			return temp;
	}
	inline std::string ToUPath(const std::string& srce)
	{
		std::string temp(srce);
		std::replace(temp.begin(), temp.end(), '\\', '/');
			return temp;
	}

	class DatabaseElement;
	struct NamedElementMap: public std::multimap<std::string, const DatabaseElement*>{};
	struct TypedElements
	{
		NamedElementMap m_namedElements;
		NamedElementMap m_dbPathedElements;
	};
	typedef std::unordered_map<std::string, TypedElements> TypedElementMap;

	class Database
	{
		std::string m_basePath;
		TypedElementMap m_elements;

        static void RecurseReadThread(void *data);
		void RecurseRead(const std::string& searchPath);

		void CreateElement(const std::string& type, const std::string& wintypeName, bool isALink=false);
		void AddElement(const DatabaseElement* pElem);

	public:
		Database(const std::string& basePath);
		void ReadEverything();

		const DatabaseElement* FindElement(const std::string& type, const std::string& nameOrPath) const;
		const DatabaseElement* FindOrCreateElement(const std::string& type, const std::string& name, bool asALink=false) const;
		const NamedElementMap* GetAllElementsOfType(const std::string& typeName) const;

		const std::string& BasePath() const { return m_basePath; }
	};

	class NullDatabase : public Database
	{
	public:
		NullDatabase() : Database("")
		{}
	};

	//// a bunch of helpers functions for those who don't want
	//// to go through the whole query process
	class Actor;
	class Level;
	class Bundle;
	class Anim;

	bool IsInitialized();
	void InitDB(const std::string& dbPath); 	//this reads the whole database but does not check that every accessed file is backed by perforce
	void ReadEverything();
	Database* GetDB();

	const libdb2::Actor* GetActor(const std::string& baseActorName, size_t lod);
	const libdb2::Actor* GetActor(const std::string& nameOrPath, bool loadAnimation = true);
	const libdb2::Level* GetLevel(const std::string& nameOrPath);
	const libdb2::Bundle* GetBundle(const std::string& nameOrPath);
	const libdb2::Anim* GetAnim(const std::string& nameOrPath);

	static std::string NameFromDBPath(const std::string & dbpath)
	{
		std::string ufilePath(ToUPath(dbpath));
		std::size_t xmlPos   = ufilePath.rfind(".xml");
		std::size_t typePos  = ufilePath.rfind(".", xmlPos-1);
		std::string typeName = ufilePath.substr(typePos+1, xmlPos - typePos -1);
		std::string dbPath	 = ufilePath.substr(0, typePos);
		std::size_t namepos  = dbPath.rfind("/");
		if (namepos == std::string::npos || namepos + 1 >= dbPath.size())
			return "";
		return dbPath.substr(namepos+1);
	}
}
