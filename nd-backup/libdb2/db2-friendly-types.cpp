#include <mutex>
//#pragma optimize("", off) // uncomment when debugging in release mode 
#include "db-query-facades.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "common/util/fileio.h"
#include "common/util/msg.h"



namespace libdb2
{
	static Database* s_pDb = nullptr;
	static bool s_readEverything = false;

	bool IsInitialized()
	{
		return s_pDb != nullptr;
	}
	
	void InitDB(const std::string& dbPath)
	{
		if (s_pDb)
			return;

		s_pDb = new Database(dbPath.c_str());
	}


	void ReadEverything()
	{
		assert(s_pDb);

		s_pDb->ReadEverything();

		s_readEverything = true;
	}

	Database* GetDB()
	{
		return s_pDb;
	}


	static const DatabaseElement* CreateFromDBAndFileName(const std::string& filePath)
	{
		assert(s_pDb);
		if (!s_pDb)
			return NULL;

		std::string ufilePath(ToUPath(filePath));
		std::size_t xmlPos   = ufilePath.rfind(".xml");
		std::size_t typePos  = ufilePath.rfind(".", xmlPos-1);
		std::string typeName = ufilePath.substr(typePos+1, xmlPos - typePos -1);
		std::string dbPath	 = ufilePath.substr(0, typePos);
// the path should be relative to the root of the db
//		dbPath = dbPath.substr(db->BasePath().length());

		bool asLink = filePath.rfind(".link") == (filePath.length() - strlen(".link"));
		const DatabaseElement* pDbElement = s_pDb->FindOrCreateElement(typeName, dbPath, asLink);
		return pDbElement;
	}


	//--------------------------------------------------------------------------------------------------------------------//
	static const DatabaseElement* GetDbElement(const std::string& typeName, const std::string nameOrPath)
	{
		const DatabaseElement* pElement = s_pDb->FindElement(typeName, nameOrPath);
		if (pElement)
		{
			if (!pElement->HasDoc())
				pElement->Read();
		}
		return pElement;
	}


	//--------------------------------------------------------------------------------------------------------------------//
	static bool ResolveFullDbPath(const std::string& assetType, const std::string& assetName, std::string& fullDbName)
	{
		if (!libdb2::IsInitialized())
			return false;

		// Check if we already know where this actor is located
		std::string assetFullName;
		if (SimpleDB::Get(assetName + "." + assetType + ".filename", assetFullName))
		{
			std::string path = libdb2::GetDB()->BasePath() + FileIO::separator + assetFullName;
			if (!FileIO::fileExists(path.c_str()))
			{
				assetFullName = "";
			}
		}

		if (assetFullName.empty())
		{
			INOTE_VERBOSE("Reading the ENTIRE database...");
			ReadEverything();

			const DatabaseElement* pDbElement = GetDbElement(assetType, assetName);
			if (pDbElement)
			{
				// Remember where we found the actor
				assetFullName = pDbElement->TypedDbPath(); 
				SimpleDB::Set(assetName + "." + assetType + ".filename", assetFullName);
			}
		}

		fullDbName = assetFullName;

		return !assetFullName.empty();
	}


	std::recursive_mutex gs_cacheLock;

	typedef  std::map < std::tuple<const DatabaseElement*, bool>, Actor*> actorCacheT;
	static actorCacheT actorCache;
	actorCacheT::iterator Insert(const DatabaseElement * pDbElement, bool loadAnimation, Actor* pActor)
	{	
		auto iter = actorCache.insert(std::make_pair(std::make_tuple(pDbElement, loadAnimation), pActor));
		return iter.first;
	}

	static const DatabaseElement *pPlaceHolderElement = reinterpret_cast<const DatabaseElement*>(0x98764321);	// hopefully a recognizable marker in case someone were to use that 'pointer'
	
	Actor* GetActorAsset(const std::string& assetType, const std::string& nameOrPath, bool loadAnimation)
	{
		std::lock_guard<std::recursive_mutex> cacheLock(gs_cacheLock);
		const DatabaseElement* pDbElement = GetDbElement(assetType, nameOrPath);
		auto where = actorCache.find(std::make_tuple(pDbElement, loadAnimation));
		if ( where == actorCache.end())
		{
			if (pDbElement)
			{
				QueryElement queryElem(pDbElement);
				where = Insert(pDbElement, loadAnimation, new Actor(queryElem.Element(assetType), loadAnimation));
			}
			else
			{

				std::string fullDbPath = nameOrPath;
				if (nameOrPath.find("/") == std::string::npos)
				{
					// We have a name, resolve to a path
					ResolveFullDbPath(assetType, nameOrPath, fullDbPath);
				}

				const DatabaseElement* pNewDbElem = CreateFromDBAndFileName(fullDbPath);
				if (pNewDbElem)
				{
					QueryElement queryElem(pNewDbElem);
					where = Insert(pNewDbElem, loadAnimation, new Actor(queryElem.Element(assetType), loadAnimation));

				}
				else
				{
					where = Insert(pPlaceHolderElement, loadAnimation, new Actor(QueryElement::null));		// we use a placeholder, because if we insert a null element
																											// it will be found next time we look up for an asset that has not been loaded yet
																											// and no other new asset will ever be loaded
				}
			}
		}
		return where->second;
	}

	
	//--------------------------------------------------------------------------------------------------------------------//
	template<class T>
	T* GetAsset(const std::string& assetType, const std::string& nameOrPath, const DatabaseElement* pDbElement = nullptr)
	{
		std::lock_guard<std::recursive_mutex> cacheLock(gs_cacheLock);
		typedef  std::map <const DatabaseElement*, T*> assetCacheT;
		static assetCacheT assetCache;
		auto Insert = [&] (const DatabaseElement * pDbElement, T* pAsset) -> assetCacheT::iterator
		{
			auto iter = assetCache.insert(std::make_pair(pDbElement, pAsset));
			return iter.first;
		};

		if (pDbElement == nullptr)
			pDbElement = GetDbElement(assetType, nameOrPath);

		auto where = assetCache.find(pDbElement);
		if (where == assetCache.end())
		{
			if (pDbElement)
			{
				QueryElement queryElem(pDbElement);
				T* element = new T(queryElem.Element(assetType));
				where = Insert(pDbElement, element);
			}
			else
			{

				std::string fullDbPath = nameOrPath;
				if (nameOrPath.find("/") == std::string::npos)
				{
					// We have a name, resolve to a path
					ResolveFullDbPath(assetType, nameOrPath, fullDbPath);
				}

				const DatabaseElement* pNewDbElem = CreateFromDBAndFileName(fullDbPath);
				if (pNewDbElem)
				{
					QueryElement queryElem(pNewDbElem);
					T* element = new T(queryElem.Element(assetType));
					where = Insert(pNewDbElem, element);
				}
				else
				{
					where = Insert(pPlaceHolderElement, new T(QueryElement::null)); // we use a placeholder, because if we insert a null element
																					// it will be found next time we look up for an asset that has not been loaded yet
																					// and no other new asset will ever be loaded

				}
			}
		}
		return where->second;
	}

	typedef  std::map < std::tuple<const Actor*, size_t>, Actor*> actorCache2T;
	static actorCache2T actorCache2;
	actorCache2T::iterator Insert(const Actor*pBaseActor, size_t lodLevel, Actor* pLodActor)
	{
		auto iter = actorCache2.insert(std::make_pair(std::make_tuple(pBaseActor, lodLevel), pLodActor));
		return iter.first;
	}

	const Actor* GetActor(const std::string& baseActorName, size_t lod)
	{
		const Actor *const pBaseActor = GetActor(baseActorName);
		if (lod == 0)
		{
			return pBaseActor;
		}

		auto where = actorCache2.find(std::make_tuple(pBaseActor, lod));
		if (where == actorCache2.end())
		{
			Actor *const pDbLodActor = new Actor((*pBaseActor), lod);
			where = Insert(pBaseActor, lod, pDbLodActor);
		}
		return where->second;
	}


	//--------------------------------------------------------------------------------------------------------------------//
	const Actor* GetActor(const std::string& nameOrPath, bool loadAnimation /*= true*/)
	{
		return GetActorAsset("actor", nameOrPath, loadAnimation);
	}

	//--------------------------------------------------------------------------------------------------------------------//
	const Level* GetLevel(const std::string& nameOrPath)
	{
		return GetAsset<Level>("level", nameOrPath);
	}

	//--------------------------------------------------------------------------------------------------------------------//
	const Bundle* GetBundle(const std::string& nameOrPath)
	{
		const DatabaseElement* pDbElem = libdb2::GetDB()->FindOrCreateElement("bundle", nameOrPath);
		return GetAsset<Bundle>("bundle", nameOrPath, pDbElem);
	}

	//--------------------------------------------------------------------------------------------------------------------//
	const Anim* GetAnim(const std::string& nameOrPath)
	{
		return GetAsset<Anim>("anim", nameOrPath);
	}
}