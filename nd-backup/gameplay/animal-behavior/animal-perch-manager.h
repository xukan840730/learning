/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef _ANIMAL_PERCH_MANAGER_
#define _ANIMAL_PERCH_MANAGER_

#include "gamelib/gameplay/animal-behavior/animal-gameplay-event.h"

//----------------------------------------------------------------------------
// a global class to communicate between animal-perch-process and outside world.
//----------------------------------------------------------------------------
class AnimalEventManager
{
public:
	AnimalEventManager();

	static AnimalEventManager& Get() { return m_singleton; }

	void RegisterListener(MutableIAnimalEventListenerHandle handle);
	void UnregisterListener(IAnimalEventListenerHandle handle);

	void DebugDraw() const;

	void BroadcastEvent(const AnimalGameplayEvent& event);

private:

	I32 GetListenerIndex(IAnimalEventListenerHandle handle) const;

	static AnimalEventManager m_singleton;

	static const I32 kMaxNumListeners = 32;

	NdAtomicLock m_listenerLock;
	MutableIAnimalEventListenerHandle m_listeners[kMaxNumListeners];
	I32 m_numListeners;
};

#endif