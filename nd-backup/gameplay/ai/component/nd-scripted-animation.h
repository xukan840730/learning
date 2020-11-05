/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

class NavCharacter;

/// --------------------------------------------------------------------------------------------------------------- ///
class INdScriptedAnimation
{
public:
	virtual bool IsActiveRequested() const = 0;

	virtual bool IsActive(const NavCharacter* pNpc) const = 0;
	virtual void Enter(NavCharacter* pNavCharacter)		  = 0;
	virtual bool Update(NavCharacter* pNavCharacter)	  = 0;
	virtual void Exit(NavCharacter* pNavCharacter)		  = 0;
	virtual void RequestStop(NavCharacter* pNavCharacter, float fadeOutTime) = 0;
};
