/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_SUBSYSTEM_HANDLE_H
#define ND_SUBSYSTEM_HANDLE_H

#ifndef ND_SUBSYSTEM_H
#error Do not include this file directly. Instead, include "gamelib/gameplay/nd-subsystem.h"
#endif

class NdSubsystem;
class RelocatableHeapRecord;

struct SubsystemHandleData
{
protected:
	RelocatableHeapRecord* m_pHeapRec = nullptr;
	U32 m_offset = 0;
	U32 m_id = 0;
};

template <class T>
class TypedSubsystemHandle : public SubsystemHandleData
{
public:
	TypedSubsystemHandle();
	TypedSubsystemHandle(T& sys);
	TypedSubsystemHandle(T* pSys);

	// conversion operator for converting between TypedSubsystemHandles
	template <class T2> operator TypedSubsystemHandle<T2>() const;
	template <class NewHandleType> NewHandleType CastHandleType() const;

	T* ToSubsystem() const;
	template <class T2> T2* ToSubsystem(StringId64 t2Sid) const;

	U32 GetSubsystemId() const;

	bool HandleValid() const;
	bool Valid() const;
	bool Assigned() const;

	static T* NullPtr() { return nullptr; }

private:
	NdSubsystem* ToSubsystemInternal() const;
	
	template <class T1, class T2> friend bool operator == (const TypedSubsystemHandle<T1>& h1, const TypedSubsystemHandle<T2>& h2);
	template <class T1, class T2> friend bool operator != (const TypedSubsystemHandle<T1>& h1, const TypedSubsystemHandle<T2>& h2);

};



typedef TypedSubsystemHandle<NdSubsystem>			NdSubsystemHandle;



#endif