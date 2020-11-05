/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef OVERLAY_CHECK_H
#define OVERLAY_CHECK_H
#include "corelib/containers/list-array.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"

namespace DC {
struct AnimActor;
struct AnimNode;
struct AnimState;
}  // namespace DC

class DynamicAnimations
{
	const ListArray<StringId64>& m_anims;
public:
	DynamicAnimations(const ListArray<StringId64>& animList)
		: m_anims(animList)
	{
	}
	const ListArray<StringId64>& List() const {return m_anims;}
};

class AnimOverlayUseCheck
{
	class OverlayState
	{
		StringId64 m_id;
		const OverlayState* m_prevState;
		bool	m_isState;
		
	public:
		OverlayState() {}
		OverlayState(StringId64 id, bool isState)
			: m_id(id)
			, m_prevState(nullptr)
			, m_isState(isState)
		{
		}		

		bool operator == (const OverlayState& rhs) const { return (rhs.m_id == m_id) && (rhs.m_prevState == m_prevState) && (rhs.m_isState == m_isState); }

		StringId64 GetId() const { return m_id;}
		bool IsState() const { return m_isState;}
	};

	class OverlayEntryInfo
	{
		const DC::AnimOverlaySetEntry* m_pEntry;
		const DC::AnimOverlaySet* m_pSet;
	public:
		OverlayEntryInfo(const DC::AnimOverlaySetEntry* pEntry,
			const DC::AnimOverlaySet* pSet)
			: m_pEntry(pEntry)
			, m_pSet(pSet)
		{}		

		OverlayEntryInfo() {}

		static int Compare(const OverlayEntryInfo& a, const OverlayEntryInfo& b)
		{
			return strcmp(DevKitOnly_StringIdToString(a.m_pEntry->m_sourceId), DevKitOnly_StringIdToString(b.m_pEntry->m_sourceId));
		}

		bool operator == (const OverlayEntryInfo& rhs) const { return (rhs.m_pEntry == m_pEntry) && (rhs.m_pSet == m_pSet); }

		const DC::AnimOverlaySetEntry*  GetEntry() const { return m_pEntry; }
		const DC::AnimOverlaySet*  GetSet() const { return m_pSet; }
	};

	//An implementation of a set using a list array as storage.  
	//Super slow because it uses linear searching to find elements.
	//It should not be used for production code.  Only be used in debug code
	template<typename T> 
	class SetArray
	{
	public:
		typedef ListArray<T> ArrayType;			

		// SCE style
		typedef typename ArrayType::ValueType		ValueType;
		typedef typename ArrayType::Iterator		Iterator;
		typedef typename ArrayType::ConstIterator	ConstIterator;
		typedef typename ArrayType::Pointer			Pointer;
		typedef typename ArrayType::ConstPointer	ConstPointer;
		typedef typename ArrayType::Reference		Reference;
		typedef typename ArrayType::ConstReference	ConstReference;
		typedef typename ArrayType::SizeType		SizeType;
		
	private:
		ArrayType list;
	public:
		SetArray(SizeType size)
			: list(size)
		{}

		void Insert(const T& item)
		{
			if (!::Contains(list, item))
				list.push_back(item);
		}

		bool Contains(const T& item) const
		{
			return ::Contains(list, item);
		}

		void Remove(const T& item)
		{
			Iterator toRemove = list.End();
			for (Iterator it = list.Begin(); it != list.End(); ++it)
			{
				if ( (*it) == item)
				{
					toRemove = it;
				}
			}
			if (toRemove != list.End())
				list.EraseAndMoveLast(toRemove);
		}

		Pointer& ArrayPointer() { return list.ArrayPointer(); }

		// SCE style
		Iterator       Begin()					{return list.Begin();}
		ConstIterator  Begin() const			{return list.Begin();}
		Iterator       End()					{return list.End();}
		ConstIterator  End() const				{return list.End();}

		T&        operator[](SizeType i)        {return list[i];}
		const T&  operator[](SizeType i) const  {return list[i];}
		SizeType  Size() const					{return list.Size();}

	};

	typedef SetArray<OverlayState> OverlayStateSet;

	typedef SetArray<OverlayEntryInfo> OverlayEntrySet;

public:
	void CheckForUnusedOverlays(const DC::AnimActor* pAnimActor, const DynamicAnimations& dynamicAnims, const ListArray<const DC::AnimOverlaySet*>& overlays);	

private:

	void SearchForUnusedOverlayEnrties(OverlayStateSet& stateSet, OverlayEntrySet& entrySet);

	bool Matches(const OverlayState& state, const OverlayEntryInfo& entry);

	void AddOverlaySetEntries(const ListArray<const DC::AnimOverlaySet*>& overlays, OverlayEntrySet& entrySet);

	void AddDynamicAnims(const DynamicAnimations& dynamicAnims, OverlayStateSet& overlaystateSet);

	void AddOverlayStateFromAnimActors(const DC::AnimActor* pAnimActor, OverlayStateSet& overlaystateSet);

	void AddOverlayStatesFromState(const DC::AnimState* pState, OverlayStateSet& overlaystateSet);

	void AddOverlayStatesFromTree(const DC::AnimNode* pAnimNode, OverlayStateSet& overlaystateSet);	
};

#endif //OVERLAY_CHECK_H
