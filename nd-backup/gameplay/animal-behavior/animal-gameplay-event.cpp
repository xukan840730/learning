/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/animal-behavior/animal-gameplay-event.h"

PROCESS_REGISTER_ABSTRACT(IAnimalEventListener, NdLocatableObject);

void IAnimalEventListener::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("on-animal-event"):
		{
			const AnimalGameplayEvent& animalEvent = *event.Get(0).GetConstPtr<AnimalGameplayEvent*>();
			OnAnimalEvent(animalEvent);
			break;
		}
	}

	ParentClass::EventHandler(event);
}