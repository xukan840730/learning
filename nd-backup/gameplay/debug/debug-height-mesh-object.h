/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */


#ifndef DEBUG_HEIGHT_MESH_OBJECT_H
#define DEBUG_HEIGHT_MESH_OBJECT_H

#include "gamelib/gameplay/nd-simple-object.h"


class DebugHeightMeshObject : public NdSimpleObject
{
	typedef NdSimpleObject ParentClass;

public:
	STATE_DECLARE_OVERRIDE(Active);

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void OnKillProcess() override;

	virtual void PostJointUpdate_Async() override;

	void UpdateHeightMeshSinWave(float height, float widthScale);
	void UpdateVerts();
	void DrawMesh();

private:
	hkGeometry* m_pGeo = nullptr;

	global::VStreamPosColor *m_pVerts = nullptr;
	int m_numVerts = 0;
};

#endif