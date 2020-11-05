/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/animal-behavior/animal-perch-manager.h"
#include "ndlib/script/script-manager.h"

AnimalEventManager AnimalEventManager::m_singleton;

//------------------------------------------------------------------------------//
AnimalEventManager::AnimalEventManager()
	: m_listenerLock()
	, m_numListeners(0)
{}

void AnimalEventManager::BroadcastEvent(const AnimalGameplayEvent& event)
{
	for (I32 ii = 0; ii < m_numListeners; ii++)
	{
		AnimalGameplayEvent* pDuplicatedEvent = NDI_NEW(kAllocSingleGameFrame) AnimalGameplayEvent(event);
		SendEvent(SID("on-animal-event"), m_listeners[ii], BoxedValue(pDuplicatedEvent));
	}
}

void AnimalEventManager::RegisterListener(MutableIAnimalEventListenerHandle handle)
{
	if (!handle.HandleValid())
		return;

	AtomicLockJanitor alj(&m_listenerLock, FILE_LINE_FUNC);

	if (GetListenerIndex(handle) >= 0)
		return;

	if (m_numListeners <= kMaxNumListeners)
		m_listeners[m_numListeners++] = handle;
}

void AnimalEventManager::UnregisterListener(IAnimalEventListenerHandle handle)
{
	AtomicLockJanitor alj(&m_listenerLock, FILE_LINE_FUNC);

	I32 indexToRemove = GetListenerIndex(handle);
	if (indexToRemove >= 0)
	{
		for (I32 ii = indexToRemove; ii < m_numListeners - 1; ii++)
			m_listeners[ii] = m_listeners[ii + 1];
		m_numListeners--;
	}
}

I32 AnimalEventManager::GetListenerIndex(IAnimalEventListenerHandle handle) const
{
	for (I32 ii = 0; ii < m_numListeners; ii++)
		if (m_listeners[ii] == handle)
			return ii;
	return -1;
}

void AnimalEventManager::DebugDraw() const
{
	for (I32 ii = 0; ii < m_numListeners; ii++)
		if (m_listeners[ii].HandleValid())
			m_listeners[ii].ToProcess()->DebugDraw();
}

//------------------------------------------------------------------------------//
void RegisterAnimalEventListener(MutableIAnimalEventListenerHandle h)
{
	AnimalEventManager::Get().RegisterListener(h);
}

void UnregisterAnimalEventListener(IAnimalEventListenerHandle h)
{
	AnimalEventManager::Get().UnregisterListener(h);
}

void BroadcastAnimalEvent(StringId64 id, Point_arg posWs)
{
	const DC::Map* pMap = ScriptManager::LookupInModule<DC::Map>(SID("*animal-events*"), SID("animal-behavior"));
	ASSERT(pMap);
	if (!pMap)
		return;

	const DC::AnimalEvent* pDef = ScriptManager::MapLookup<DC::AnimalEvent>(pMap, id);

	AnimalGameplayEvent event(*pDef, posWs);
	AnimalEventManager::Get().BroadcastEvent(event);
}