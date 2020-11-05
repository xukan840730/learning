/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef MESHTABLE_H
#define MESHTABLE_H


class MeshTable
{
public:

	static I64 s_meshTableModifiedFrame;

	static void SetMeshTableModified();
	static bool WasMeshTableModifiedLastFrame();

};


#endif // MESHTABLE_H


