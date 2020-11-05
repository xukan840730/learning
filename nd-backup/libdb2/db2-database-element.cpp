//#define _CRTDBG_MAP_ALLOC

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>

#include <string>
#include <iostream>
#include <fstream>
#include <memory>
#include <vector>

#include "db2-database.h"
#include "db2-database-element.h"
#include "db2-database-element-impl.h"
#include "perforce-checker.h"
#include "common/imsg/msg.h"

namespace libdb2
{
	static bool findDataCompareFunc(WIN32_FIND_DATA a, WIN32_FIND_DATA b)
	{
		return strcmp(a.cFileName, b.cFileName) <= 0;
	}

	// disable: 'this' : used in mase member initializer list
	#pragma warning(disable:4355)    
	static std::vector<WIN32_FIND_DATA> SearchFiles(const std::string path, const std::string pattern)
	{
		std::string searchPattern(path);
		searchPattern.append(pattern);
		searchPattern = ToWinPath(searchPattern);
		std::vector<WIN32_FIND_DATA> files;
		HANDLE hFind;
		WIN32_FIND_DATA FindFileData;
		hFind = FindFirstFile(searchPattern.c_str(), &FindFileData);
		if (hFind == INVALID_HANDLE_VALUE) 
		{
			//		printf ("Invalid File Handle. GetLastError reports %d\n", GetLastError ());
			return files;
		} 
		files.push_back(FindFileData);
		while (FindNextFile(hFind, &FindFileData) != 0) 
			files.push_back(FindFileData);
		DWORD  dwError = GetLastError();
		FindClose(hFind);

		// Sort the files alphabetically to ensure consistent processing
		std::sort(files.begin(), files.end(), findDataCompareFunc);

		return files;
	}





	static xmlDocPtr CreateXmlDocForBundle(const std::string& databasePath, const  std::string& name, const  std::string &supposedPath)
	{
		xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");

		xmlNodePtr rootElem =  xmlNewChild((xmlNodePtr)doc, 0, BAD_CAST "bundle", 0);
		xmlNodePtr animElem =  xmlNewChild(rootElem, 0, BAD_CAST "animations", 0);
		xmlNodePtr bundleElem = xmlNewChild(rootElem, 0, BAD_CAST "bundles", 0);
		xmlAttrPtr nameAttribute = xmlNewProp(rootElem, BAD_CAST "name", BAD_CAST name.c_str());
		xmlNodePtr animlinkElem =  xmlNewChild(rootElem, 0, BAD_CAST "linkedAnimations", 0);


		std::string directory = supposedPath.substr(0, supposedPath.find(".xml"));
		directory.append("/");
		std::vector<WIN32_FIND_DATA> anims = SearchFiles(directory, "*.anim.xml");
		std::vector<WIN32_FIND_DATA>::iterator it;
		for (it = anims.begin(); it!= anims.end(); ++it)
		{
			std::string animFileName(supposedPath);
			animFileName = animFileName.substr(databasePath.length()+1);
			animFileName = animFileName.substr(0,animFileName.rfind(".xml"));
			animFileName.append("/");
			animFileName.append((*it).cFileName);
			std::string animName = animFileName.substr(0, animFileName.find(".anim.xml"));

			xmlNodePtr refElem = xmlNewChild(animElem, 0, BAD_CAST "ref", 0);
			xmlAttrPtr refAttribute = xmlNewProp(refElem, BAD_CAST "path", BAD_CAST animName.c_str());
		}

		std::vector<WIN32_FIND_DATA> linked_anims = SearchFiles(directory, "*.anim.xml.link");
		for (it = linked_anims.begin(); it!= linked_anims.end(); ++it)
		{
			std::string animFileName(supposedPath);
			animFileName = animFileName.substr(databasePath.length()+1);
			animFileName = animFileName.substr(0,animFileName.rfind(".xml"));
			animFileName.append("/");
			animFileName.append((*it).cFileName);
			std::string animName = animFileName.substr(0, animFileName.find(".anim.xml"));

			xmlNodePtr refElem = xmlNewChild(animlinkElem, 0, BAD_CAST "ref", 0);
			xmlAttrPtr refAttribute = xmlNewProp(refElem, BAD_CAST "path", BAD_CAST animName.c_str());
		}



		std::vector<WIN32_FIND_DATA> linkedBundles = SearchFiles(directory, "*.bundle.link");
		for (it = linkedBundles.begin(); it!= linkedBundles.end(); ++it)
		{
			std::string linkFileName(supposedPath);
			linkFileName = linkFileName.substr(0,linkFileName.rfind(".xml"));
			linkFileName.append("/");
			linkFileName.append((*it).cFileName);
			std::ifstream in;
			std::string bundleName;
			in.open(linkFileName.c_str());
			std::getline(in, bundleName);
			in.close();
			bundleName = bundleName.substr(0,bundleName.rfind(".bundle"));

			xmlNodePtr refElem = xmlNewChild(bundleElem, 0, BAD_CAST "ref", 0);
			xmlAttrPtr refAttribute = xmlNewProp(refElem, BAD_CAST "path", BAD_CAST bundleName.c_str());
		}
		return doc;
	}



	DatabaseElementImp::DatabaseElementImp(DatabaseElement &owner):
	m_owner(owner),
	m_owningDatabase(*(owner.mOwningDatabase)),
	m_document(0)
	{
	}
	DatabaseElementImp::~DatabaseElementImp()
	{
		if (m_document)
		{
			m_owner.m_document = NULL;
			xmlFreeDoc(m_document);

		}
	}
	DatabaseElementImp::DatabaseElementImp(DatabaseElement &owner, const DatabaseElementImp& other) :
		m_owner(owner),
		m_owningDatabase(*(owner.mOwningDatabase)),
		m_document(0)
	{
		if (other.m_document)
		{
			m_document = xmlCopyDoc(other.m_document, 1);
			m_owner.m_document = m_document;
		}
	}


	std::string GetErrorString(unsigned int errorCode)
	{
		LPTSTR pszMessage;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			errorCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&pszMessage,
			0, NULL );
		
		std::string message = std::string(pszMessage);
		LocalFree(pszMessage);
		
		message = message.substr(0, message.length()-2);
		return message;
	}

	void DatabaseElementImp::Read() const
	{
		IASSERT(!m_document);

		if (m_owner.mType == "bundle")
		{
			std::string diskPath(m_owner.DiskPath());
			std::string dbPath(m_owner.mOwningDatabase->BasePath().begin(),  m_owner.mOwningDatabase->BasePath().end());
			m_document = CreateXmlDocForBundle(dbPath, m_owner.mName, diskPath);
			m_owner.m_document = m_document;
		}
		else
		{
			// load the database file
			std::string diskPath(m_owner.DiskPath());
			HANDLE handle;
			int errcount = 1000;
			while ((handle = CreateFile(diskPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0)) == INVALID_HANDLE_VALUE && errcount)
			{
				unsigned int errorCode = GetLastError();
				if (errorCode == ERROR_FILE_NOT_FOUND || errorCode == ERROR_PATH_NOT_FOUND) break;
				fprintf(stderr, "ERROR: Can't read file [%s]:\n%s (0x%x)\n", diskPath.c_str(), GetErrorString(errorCode).c_str(), errorCode);
				Sleep(500 + (rand() % 1000));
				errcount--;
			}
			if (handle == INVALID_HANDLE_VALUE) 
			{
				unsigned int errorCode = GetLastError();
				fprintf(stderr, "ERROR: Unable to access database file [%s]: \n%s (0x%x)\n", diskPath.c_str(), GetErrorString(GetLastError()).c_str(), errorCode);
				m_document = 0;
				return;
			}

			DWORD length = GetFileSize(handle, NULL);

			char *buffer = 0;
			buffer = new char [length + 2];
			DWORD bytesRead;
			ReadFile(handle, buffer, length, &bytesRead, NULL);
			buffer[length] = 0;
			buffer[length+1] = 0;
			CloseHandle(handle);

			// in case we're reading a link, then read the target of the link.... (this only works for depht of 1 linking)
			if (m_owner.m_isLink)
			{
				std::string diskPath(m_owner.mOwningDatabase->BasePath());
				diskPath += '/';
				diskPath += buffer;
				delete [] buffer;
				HANDLE handle;
				int errcount = 1000;
				while ((handle = CreateFile(diskPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0)) == INVALID_HANDLE_VALUE && errcount)
				{
					unsigned int errorCode = GetLastError();
					if (errorCode == ERROR_FILE_NOT_FOUND || errorCode == ERROR_PATH_NOT_FOUND) break;
					fprintf(stderr, "ERROR: Can't read file [%s]: %s (0x%x) (target of the link %s)\n", diskPath.c_str(), GetErrorString(errorCode).c_str(), errorCode, m_owner.DiskPath().c_str());
					Sleep(500 + (rand() % 1000));
					errcount--;
				}
				if (handle == INVALID_HANDLE_VALUE) 
				{
					unsigned int errorCode = GetLastError();
					fprintf(stderr, "ERROR: Unable to access database file [%s]: %s (0x%x) (target of the link %s)\n", diskPath.c_str(), GetErrorString(GetLastError()).c_str(), errorCode, m_owner.DiskPath().c_str());
					m_document = 0;
					return;
				}

				length = GetFileSize(handle, NULL);

				buffer = new char [length + 2];
				DWORD bytesRead;
				ReadFile(handle, buffer, length, &bytesRead, NULL);
				buffer[length] = 0;
				buffer[length+1] = 0;
				CloseHandle(handle);
			}			


			if (buffer) 
			{
				m_document = xmlParseMemory(buffer, length);
				m_owner.m_document = m_document;
				delete [] buffer;
			}
			if (m_document == NULL) 
			{
				fprintf(stderr,"Document not parsed successfully. \n");
				return;
			}
		}
	}


	DatabaseElement::
		DatabaseElement(const std::string& path, 
		const std::string& filename, 
		const libdb2::Database *owner, int64_t lastWriteTime): //I wished owner was a ref, but it was to late when I decided to implement it to get it right quickly
	mPath(path),
	mFilename(filename),
//	mDocument(0),
	mOwningDatabase(owner),
	m_lastWriteTime(lastWriteTime),
	m_document(NULL),
	pimpl_(new DatabaseElementImp(*this))
	{
		std::size_t xmlPos  = mFilename.rfind(".xml");
		std::size_t typePos = mFilename.rfind(".", xmlPos-1);
		mType = mFilename.substr(typePos+1, xmlPos - typePos -1);
		mName = mFilename.substr(0, typePos);
		m_isLink = mFilename.rfind(".link") == mFilename.length() - strlen(".link");
	}

	DatabaseElement::DatabaseElement(const DatabaseElement& other)
	{
		*this = other;
		this->pimpl_ = new DatabaseElementImp(*this, *other.pimpl_);
	}
	DatabaseElement::~DatabaseElement()
	{
//		if (mDocument)
//			mDocument->release();
		m_document = NULL;
		delete pimpl_;
	}


	std::string DatabaseElement::DiskPath() const
	{
		std::string ret(mPath);
		ret.append("/");
		ret.append(mFilename);
		return ret;
	
	}
	
	std::string DatabaseElement::DbPath() const
	{
		const std::string& basePath = mOwningDatabase->BasePath();
		std::string ret;
		if (mPath.size())
			ret = mPath.substr(basePath.length()+1);
		
		ret.append("/");
		ret.append(mName);
		return ret;
	}
	
	std::string DatabaseElement::TypedDbPath() const
	{
		std::string ret;
		if (mPath.size())
			ret = mPath.substr(mOwningDatabase->BasePath().length()+1);
		ret.append("/");
		ret.append(mFilename);
		return ret;
	}
	
	void DatabaseElement::Read() const
	{
		if (HasDoc())
		{
			return;
		}

		pimpl_->Read();
	}

	bool DatabaseElement::HasDoc() const
	{
		return pimpl_->m_document != 0;
	}

}

