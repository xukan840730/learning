/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef _ANIMAL_GAMEPLAY_EVENT_H
#define _ANIMAL_GAMEPLAY_EVENT_H

#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/scriptx/h/animal-behavior-defines.h"

//----------------------------------------------------------------------------------//
// Animal Events
//----------------------------------------------------------------------------------//
class AnimalGameplayEvent
{
public:

	AnimalGameplayEvent(DC::AnimalEvent def, Point_arg posWs)
		: m_def(def)
		, m_posWs(posWs)
	{}

	DC::AnimalEvent m_def;
	Point m_posWs;
};

class IAnimalEventListener : public NdLocatableObject
{
	typedef NdLocatableObject ParentClass;

public:
	virtual void OnAnimalEvent(const AnimalGameplayEvent& event) = 0;
	virtual void EventHandler(Event& event) override;

	virtual void DebugDraw() const {}
};

PROCESS_DECLARE(IAnimalEventListener);

FWD_DECL_PROCESS_HANDLE(IAnimalEventListener);

void RegisterAnimalEventListener(MutableIAnimalEventListenerHandle h);
void UnregisterAnimalEventListener(IAnimalEventListenerHandle h);

void BroadcastAnimalEvent(StringId64 id, Point_arg posWs);

#endif