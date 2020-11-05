#ifdef MAIN_TEST

#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif						

#include <stdio.h>
#include <tchar.h>

#include <iostream>
#include "database.h"
#include "DBQueryFacades.h"


void TestCache()
{
	libdb2::Database dbcached(".", "y:/tim", false, false);
//	dbcached.ReadCached("y:/tim");
	libdb2::Database db1("C:\\big2\\data\\db\\builderdb", "", true, false);

}


int _tmain(int argc, _TCHAR* argv[])
{

//	TestCache();
//	libdb2::InitDBNoRead("C:\\big2\\data\\db\\builderdb");
	libdb2::InitDB("y:\\big3\\data\\db\\builderdb");
	libdb2::ReadEverything();


	//libdb2::Level sndbank1 = libdb2::GetLevel("war-dummy-const");
	libdb2::Actor sndbank2 = libdb2::GetActor("falling-sign");
	libdb2::Actor actorlod = libdb2::GetActor("actor-lod-test");

	libdb2::Actor  actor = libdb2::GetActor("jerome-actor");

	libdb2::Level gompa = libdb2::GetLevel("gompa");
	libdb2::Level blendtest = libdb2::GetLevel("blend-test");
	libdb2::Level aleveltest = libdb2::GetLevel("aleveltest");

//	libdb2::InitDB("C:\\temp\\buildBigExportX");
//	libdb2::InitDB("C:\\temp\\export\\Output");

//	libdb2::Actor  actor = libdb2::GetActor("gompa-bridge-broken");
//	libdb2::Actor  actor = libdb2::GetActor("afolder");
	libdb2::Actor  actor2 = libdb2::GetActor("afolder-1");
	libdb2::Actor  actor3 = libdb2::GetActor("afolder-1-1");

//	libdb2::Anim jeromeanim = libdb2::GetAnimFromFile("progammers\\jerome\\afolder\\afolder.bundle\\jeromeanim.anim.xml");
	libdb2::Anim jeromeanim = libdb2::GetAnim("in-game-cinematics\\warzone\\caps-highrise.bundle\\inspect-map-a-ent.anim.xml");


	std::cout<<"actor.FullName()   "<<actor.FullName()<<"\n";
	std::cout<<"actor.DiskPath() "<<actor.DiskPath()<<"\n";
	std::cout<<"actor.Name()     "<<actor.Name()<<"\n";
	std::cout<<"actor.Filename() "<<actor.Filename()<<"\n";
	std::cout<<"actor.UniqueName()"<<actor.UniqueName()<<"\n";	
	bool genFeat = actor.m_geometry.m_generateFeaturesOnAllSides;

//	libdb2::Anim anneIm = 	libdb2::GetAnimFromFile("progammers\\scott\\scott.bundle\\breathing-heavy.anim.xml");\
//	std::cout <<anneIm.m_refAnimField.Loaded() <<"\n";

	libdb2::Level lt = libdb2::GetLevel("gompa-lightmap-test");
	libdb2::Level npc_animtest = libdb2::GetLevel("levels\\test\\playtests\\npc-animtest.level.xml");
	std::cout<<npc_animtest.LastWriteTime();


	libdb2::Level lalalevel = libdb2::GetLevel("lala");

/*
//	libdb2::InitDBNoRead("C:\\big2\\data\\db\\builderdb");
	libdb2::InitDBNoRead("C:\\temp\\buildBigExportX");
/*	libdb2::Level  level = libdb2::GetLevelFromFile("C:\\big2\\data\\db\\builderdb\\levels\\amazon\\temple\\temple-0a.level.xml");
	libdb2::Actor  actor2 = libdb2::GetActorFromFile("C:\\big2\\data\\db\\builderdb\\characters\\hero\\actors\\hero\\hero.actor.xml");
*/
	libdb2::Actor  uboat_break = libdb2::GetActor("objects\\amazon\\amazon-uboat-break\\amazon-uboat-break.actor.xml");


	libdb2::Actor  elena = libdb2::GetActor("characters\\elena\\elena\\elena-base.actor.xml");
//	libdb2::Anim anneIm = 	libdb2::GetAnimFromFile("progammers\\scott\\scott.bundle\\breathing-heavy.anim.xml");\
	

//	const libdb2::Anim& refed = anneIm.RefAnim();

	libdb2::Anim elenaanim(*elena.m_animations.begin());
	std::cout<<"elenaanim.FullName()   "<<elenaanim.FullName()<<"\n";
	std::cout<<"elenaanim.DiskPath() "<<elenaanim.DiskPath()<<"\n";
	std::cout<<"elenaanim.Name()     "<<elenaanim.Name()<<"\n";
	std::cout<<"elenaanim.Filename() "<<elenaanim.Filename()<<"\n";
	std::cout<<"elenaanim.UniqueName()"<<elenaanim.UniqueName()<<"\n";	



	libdb2::Anim anneIm2 = 	libdb2::GetAnim("progammers\\scott\\scott.bundle\\ambient-catch-breath_part1.anim.xml");
	const libdb2::Anim& refed2 = anneIm2.RefAnim();
	
	return 0;
}

#endif 