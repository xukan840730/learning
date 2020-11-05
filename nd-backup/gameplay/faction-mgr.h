/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _NDLIB_FACTION_MGR_H_
#define _NDLIB_FACTION_MGR_H_

class EntityDB;

/// --------------------------------------------------------------------------------------------------------------- ///
class DoutBase;
namespace DMENU { class Item; }

/// --------------------------------------------------------------------------------------------------------------- ///
class FactionId
{
public:
	static const U32F kInvalid = 0xff;
	
	FactionId() : m_id(kInvalid) {}
	FactionId(U8 factionId) : m_id(factionId) {}

	bool IsValid() const; 
	const char* GetName() const;
	StringId64	GetNameId() const;
	U32F        GetRawFactionIndex() const {return m_id;}   // try to avoid using this when possible
	
	bool operator == (const FactionId& rhs) const	{ return m_id == rhs.m_id; }
	bool operator != (const FactionId& rhs) const	{ return m_id != rhs.m_id; }

private:
	U8   m_id;
};

FactionId FactionIdInvalid();

/// --------------------------------------------------------------------------------------------------------------- ///
///
///
/// FactionMgr class
///
///
/// NOTE: faction relations are commutative, i.e.
///
///     IsFriend(a, b) implies  IsFriend(b, a)
///  and
///     IsEnemy(a, b)  implies  IsEnemy(b, a)
///
class FactionMgr
{
public:
	enum Status
	{
		kStatusNeutral,
		kStatusFriend,
		kStatusEnemy
	};

	FactionMgr();
	void Init(U32 factionCount, const char** factionNames, const char** factDictionaryNames = nullptr);
	void Shutdown();

	const FactionId LookupFactionByNameId(StringId64 nameId) const;
	const char* GetFactionName( FactionId fac ) const;
	StringId64 GetFactionNameId( FactionId fac ) const;
	StringId64 GetFactionFactDictionaryId( FactionId fac ) const;

	bool IsValid(FactionId faction) const;
	bool IsFriend(FactionId lhs, FactionId rhs) const;
	bool IsEnemy(FactionId lhs, FactionId rhs) const;

	U32F GetFactionCount() const {return m_factionCount;}
	Status GetFactionStatus(FactionId lhs, FactionId rhs) const;
	// set up relations between factions
	void SetFactionStatus(FactionId lhs, FactionId rhs, Status status);
	// write out a textual representation of the faction matrix
	void DumpFactionMatrixToStream( DoutBase* pStream ) const;

	static DMENU::Item* CreateSelectAllByFactionMenuItem(const char* itemName, const char* menuTitle);

private:
	void SetBit(U8* bits, U32F index) const;
	void ClearBit(U8* bits, U32F index) const;
	bool IsBitSet(const U8* bits, U32F index) const;

private:
	struct FactionDef
	{
		const char* strName;
		StringId64    nameId;
		StringId64	factDictId; // or INVALID_STRING_ID_64 if this faction doesn't require a fact dictionary
	};
	
	U8   m_factionCount;
	U8*  m_pEnemyTable;
	U8*  m_pFriendTable;
	FactionDef* m_factionDefs;
	
	friend class FactionId;
};

extern FactionMgr g_factionMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool IsFriend(FactionId lhs, FactionId rhs)
{
	return g_factionMgr.IsFriend(lhs, rhs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool IsNotFriend(FactionId lhs, FactionId rhs)
{
	return !g_factionMgr.IsFriend(lhs, rhs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool IsEnemy(FactionId lhs, FactionId rhs)
{
	return g_factionMgr.IsEnemy(lhs, rhs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool IsNotEnemy(FactionId lhs, FactionId rhs)
{
	return !g_factionMgr.IsEnemy(lhs, rhs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 BuildFactionMask(FactionId faction);
U32 BuildFactionMask(const EntityDB* pDb);


#endif
