/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef SIMPLE_GRID_HASH_MGR_H
#define SIMPLE_GRID_HASH_MGR_H

#include "corelib/system/read-write-atomic-lock.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/simple-grid-hash.h"

class NdGameObject;

// ------------------------------------------------------------------------------------------------------------------------
// SimpleGridHashManager
// ------------------------------------------------------------------------------------------------------------------------

class SimpleGridHashManager
{
	typedef SimpleGridHash::MemberBits MemberBits;

public:
	enum
	{
		kMaxRegisteredCount = SimpleGridHash::kMaxMembers
	};

	struct RegisteredObjectInfo
	{
		Point					m_posWs;
		Vector					m_velWs;
		MutableNdGameObjectHandle m_hGo;
		StringId64				m_userId;
		F32						m_radiusSoft;
		F32						m_radiusHard;
	};

	struct RegisteredObject
	{
		SimpleGridHash::Coord	m_gridHashCoord;
		FactionId				m_factionId;

		RegisteredObjectInfo	m_info;
	};

	class Iterator
	{
		typedef MemberBits::Iterator ParentClass;

	public:
		Iterator(const SimpleGridHashManager& mgr, const char* file, int line, const char* func)
			: m_lockJanitor(&mgr.s_lock, file, line, func)
			, m_mgr(mgr)
			, m_iter(mgr.m_members)
		{
		}

		NdGameObjectHandle First()
		{
			const I32 index = m_iter.First();
			if (index == m_iter.End())
				return nullptr;
			return m_mgr.GetHandle(index);
		}

		NdGameObjectHandle Advance()
		{
			const I32 index = m_iter.Advance();
			if (index == m_iter.End())
				return nullptr;
			return m_mgr.GetHandle(index);
		}

		NdGameObjectHandle End()
		{
			return nullptr;
		}

	private:
		MemberBits::Iterator			m_iter;
		const SimpleGridHashManager&	m_mgr;
		AtomicLockJanitorRead			m_lockJanitor;
	};

	friend class Iterator;

	Iterator GetIterator(const char* file, int line, const char* func) const { return Iterator(*this, file, line, func); }

	static SimpleGridHashManager s_singleton;
	static SimpleGridHashManager& Get() { return s_singleton; };

	SimpleGridHashManager();

	void Init();
	void Reset();

	void Lock();
	void Unlock();

	SimpleGridHashId Register(NdGameObject& go, F32 radiusSoft, F32 radiusHard, FactionId factionIdOverride = FactionId::kInvalid);
	bool Unregister(NdGameObject& go, SimpleGridHashId gridHashId);

	static I32F GetMaxCount() { return kMaxRegisteredCount; }
	MutableNdGameObjectHandle GetHandle(U32F index) const;
	MutableNdGameObjectHandle GetHandleNoLock(U32F index) const;
	NdGameObject* Get(U32F index) const;
	NdGameObject* GetNoLock(U32F index) const;
	NdGameObject* Get(SimpleGridHashId hashId) const;
	//I32F FindIndexOf(NdGameObject& go) const;

	const RegisteredObject* GetRecord(U32F index) const;
	const RegisteredObject* GetRecordNoLock(U32F index) const;
	const RegisteredObject* GetRecord(SimpleGridHashId hashId) const;
	const RegisteredObject* GetRecordNoLock(SimpleGridHashId hashId) const;

	bool UpdateGridHash(NdGameObject& self, SimpleGridHashId gridHashId, F32 radiusSoft, F32 radiusHard);

	SimpleGridHash*	GetGridHash() { return m_pGridHash; }

	bool GetGridHashCoord(SimpleGridHashId gridHashId, SimpleGridHash::Coord* pGridHashCoord);

	static NdRwAtomicLock64	s_lock;

protected:
	MemberBits			m_members;

private:
	void ResetNoLock();

	RegisteredObject	m_aRegisteredObject[kMaxRegisteredCount];
	SimpleGridHash*		m_pGridHash;
};

#endif // SIMPLE_GRID_HASH_MGR_H
