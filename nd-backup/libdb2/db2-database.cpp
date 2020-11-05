#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <io.h>
#include <sys/stat.h>

#include "tools/libs/libdb2/db2-database.h"
#include "tools/libs/libdb2/db2-database-element.h"
#include "tools/libs/thread/threadpool.h"

// #pragma optimize("", off) // uncomment when debugging in release mode 


namespace libdb2
{

static const std::string dbSeparator("{{{{====}}}}");
static const std::string dbTypeDelimiter("*type*");

static CritcalSection gs_elementsLock;   // to be used when accessing elements in a muti-threaded way.


//--------------------------------------------------------------------------------------------------------------------//
Database::Database(const std::string &basePath)
	: m_basePath(ToUPath(basePath))
{
}


//--------------------------------------------------------------------------------------------------------------------//
void Database::ReadEverything()
{
	RecurseRead(m_basePath);
}


//--------------------------------------------------------------------------------------------------------------------//
void Database::AddElement(const DatabaseElement* pElem)
{
	TypedElementMap::iterator namedMap(m_elements.find(pElem->Type()));
	if (namedMap == m_elements.end())
	{
		//no map associated with this type yet!
		//add a map for the type and get iterator
		namedMap = m_elements.insert(std::pair<std::string, TypedElements>(pElem->Type(), TypedElements())).first;
	}

	std::pair<std::string, const DatabaseElement*> namedElemPair(pElem->Name(), pElem);
	std::pair<std::string, const DatabaseElement*> dbPathedElemPair(pElem->DbPath(), pElem);
	(*namedMap).second.m_namedElements.insert(namedElemPair);
	(*namedMap).second.m_dbPathedElements.insert(dbPathedElemPair);
}


//--------------------------------------------------------------------------------------------------------------------//
void Database::RecurseReadThread(void *data)
{
    std::pair<Database *, std::string> *pdata = (std::pair<Database *, std::string>*)data;
    pdata->first->RecurseRead(pdata->second);
}


//--------------------------------------------------------------------------------------------------------------------//
void Database::RecurseRead(const std::string& searchPath)
{
	std::string winPath(ToWinPath(searchPath));
	std::string searchPattern(winPath);
	searchPattern.append("\\*");
	std::vector<_finddata_t> files;

	intptr_t hFind;
	_finddata_t FindFileData;

	hFind = _findfirst(searchPattern.c_str(), &FindFileData);
	
	if (hFind == -1) 
	{
//		printf ("Invalid File Handle. GetLastError reports %d\n", GetLastError ());
		return ;
	} 
	files.push_back(FindFileData);
	while (_findnext(hFind, &FindFileData) == 0)
	{
		files.push_back(FindFileData);
	}
	DWORD  dwError = GetLastError();
	_findclose(hFind);

	std::vector<_finddata_t>::iterator filesEnd = files.end();
	gs_elementsLock.Enter();
	for (std::vector<_finddata_t>::iterator it = files.begin(); it != filesEnd; ++it)
	{
		if (((*it).attrib & _A_SUBDIR) == 0)
		{
			// not a directory
			struct stat stats;
			stat((winPath + "\\" + (*it).name).c_str(), &stats);
			int64_t lastWriteTime = stats.st_mtime;
			;

			std::string filename((*it).name);

			const DatabaseElement* pElem = new DatabaseElement(searchPath, filename, this, lastWriteTime);
			AddElement(pElem);
		}
	}

	gs_elementsLock.Leave();

	//recurse for each directory
	std::string dot(".");
	std::string dotdot("..");
	std::string bundledir(".bundle");
	std::string trashdir("trash");
    std::vector<WorkItemHandle> workHandles;
	for (std::vector<_finddata_t>::iterator it=files.begin(); it!= filesEnd; ++it)
	{
		if ( ((*it).attrib & _A_SUBDIR) != 0)
		{
			std::string subdir((*it).name);
			if (subdir.compare(dot) != 0 && subdir.compare(dotdot) != 0 && subdir.compare(trashdir) != 0)
			{
				std::string subpath(searchPath);
				subpath.append("/");
				subpath.append((*it).name);
				std::pair<Database *, std::string> *data = new std::pair<Database *, std::string>(this, subpath);
				workHandles.push_back(NdThreadPool::QueueWorkItem(RecurseReadThread, data));
			}
			size_t bundledirpos = subdir.rfind(bundledir);
			if (bundledirpos != std::string::npos)
			{
				// in case of a bundle dir,
				// pretend there is an xml file with the same name
				// and create a database Element for it
				std::string filename((*it).name);
				filename.append(".xml");

				struct stat stats;
				stat((winPath + "\\" + (*it).name).c_str(), &stats);
				int64_t lastWriteTime = stats.st_mtime;

				const DatabaseElement* pElem = new DatabaseElement(searchPath, filename, this, lastWriteTime);
				gs_elementsLock.Enter();
				AddElement(pElem);
				gs_elementsLock.Leave();
			}
		}
	}

    //wait for all subworks to complete
    for (size_t i = 0; i < workHandles.size(); ++i)
    {
        NdThreadPool::WaitWorkItem(workHandles[i]);
        delete (std::pair<Database *, std::string> *)(workHandles[i].Token());
    }
}


//--------------------------------------------------------------------------------------------------------------------//
const DatabaseElement* Database::FindElement(const std::string& type, const std::string& nameOrPath) const
{
	TypedElementMap::const_iterator namedMap(m_elements.find(type));
	if (namedMap != m_elements.end())
	{
		// Check the name first
		NamedElementMap::const_iterator namedElement((*namedMap).second.m_namedElements.find(nameOrPath));
		if (namedElement != (*namedMap).second.m_namedElements.end())
		{
			return (*namedElement).second;
		}

		std::string pathToCheck = nameOrPath;
		std::size_t typePos = pathToCheck.rfind(type + ".xml");
		if (typePos != std::string::npos)
			pathToCheck = pathToCheck.substr(0, typePos - 1);

		// Now check the full path
		NamedElementMap::const_iterator pathedElement((*namedMap).second.m_dbPathedElements.find(pathToCheck));
		if (pathedElement != (*namedMap).second.m_dbPathedElements.end())
		{
			return (*pathedElement).second;
		}
	}

	return 0;
}


//--------------------------------------------------------------------------------------------------------------------//
const DatabaseElement* Database::FindOrCreateElement(const std::string& type, const std::string& name, bool asALink) const
{
	const DatabaseElement* pElem = FindElement(type, name);
	if (pElem)
	{
		if (pElem->HasDoc())
			return pElem;

		pElem->Read();
		return pElem->HasDoc() ? pElem : NULL;
	}
	else
	{
		Database* pMe = const_cast<Database *> (this);
		pMe->CreateElement(type, name, asALink);

		pElem = FindElement(type, name);
		if (!pElem)
			return NULL;

		pElem->Read();
		return pElem->HasDoc() ? pElem : NULL;
	}
}


//--------------------------------------------------------------------------------------------------------------------//
const NamedElementMap* Database::GetAllElementsOfType(const std::string& typeName) const
{
	auto iter = m_elements.find(typeName);
	if (iter != m_elements.end())
	{
		return &iter->second.m_namedElements;
	}

	return nullptr;
}


//--------------------------------------------------------------------------------------------------------------------//
void Database::CreateElement(const std::string& type, const std::string& name, bool link) 
{
	if (name.empty())
		return;

	std::string searchPath    = m_basePath + "/" + name.substr(0, name.rfind('/'));
	std::string filename      = name.substr(name.rfind('/')+1) + "." + type + ".xml" + (link ? ".link" : "");
	std::string searchPattern = searchPath + "/" + filename;

	searchPattern = ToWinPath(searchPattern);
	int64_t lastWriteTime = 0UL;
	if (type != "bundle")
	{
		struct stat stats;
		stat(searchPattern.c_str(), &stats);
		lastWriteTime = stats.st_mtime;
	}

	const DatabaseElement* pElem = new DatabaseElement(searchPath, filename, this, lastWriteTime);
	AddElement(pElem);
}

} // namespace libdb2