/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/debug/debug-height-mesh-object.h"

#include "ndlib/render/draw-control.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/render-globals.h"

#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/ndphys/rigid-body-grid-hash.h"

#include <Common/Base/hkBase.h>
#include <Physics/Physics/Collide/Shape/Composite/Mesh/Compressed/hknpCompressedMeshShape.h>
#include <Physics/Physics/Collide/Shape/Composite/Mesh/Compressed/hknpCompressedMeshShapeCinfo.h>


PROCESS_REGISTER(DebugHeightMeshObject, NdSimpleObject);

Err DebugHeightMeshObject::Init(const ProcessSpawnInfo& spawn)
{	
	Err result = ParentClass::Init(spawn);
	if (result.Failed())
		return result;

	SetStopWhenPaused(false);

	CompositeBody* pComp = GetCompositeBody();
	if (pComp)
	{
		int numBodies = pComp->GetNumBodies();
		if (numBodies > 0)
		{
			RigidBody* pRigidBody = pComp->GetBody(0);
			pRigidBody->SetNoBroadphase(true);
		}
	}

	m_allowDisableUpdates = false;

	return Err::kOK;
}

void DebugHeightMeshObject::OnKillProcess()
{
	ParentClass::OnKillProcess();

	if (m_pVerts)
	{
		NDI_DELETE [] m_pVerts;
		m_pVerts = nullptr;
		m_numVerts = 0;
	}

	if (m_pGeo)
	{
		m_pGeo->removeReference();
		m_pGeo = nullptr;
	}
}

void DebugHeightMeshObject::PostJointUpdate_Async()
{
	ParentClass::PostJointUpdate_Async();
}

void DebugHeightMeshObject::UpdateHeightMeshSinWave(float height, float widthScale)
{
	CompositeBody* pComp = GetCompositeBody();
	if (pComp)
	{
		int numBodies = pComp->GetNumBodies();
		if (numBodies > 0)
		{
			RigidBody* pRigidBody = pComp->GetBody(0);
			const hknpShape* pShape = pRigidBody->GetHavokShape();

			pRigidBody->SetNoBroadphase(true);

			if (pShape)
			{
				const int kSubdivisions = 60;
				const float kWidth = 20.0f;
				const float kVertDist = kWidth/kSubdivisions;
				const int kZeroOutVerts = 4;

				if (m_pGeo)
					m_pGeo->removeReference();

				m_pGeo = NDI_NEW hkGeometry;
				m_pGeo->m_vertices.setSize((kSubdivisions+1)*(kSubdivisions+1));

				for (int iRow=0; iRow<=kSubdivisions; iRow++)
				{
					float zeroScaleRow = 1.0f;
					if (iRow < kZeroOutVerts)
						zeroScaleRow = (float)iRow/kZeroOutVerts;
					else if (kSubdivisions-iRow < kZeroOutVerts)
						zeroScaleRow = (float)(kSubdivisions-iRow)/kZeroOutVerts;

					for (int iCol=0; iCol<=kSubdivisions; iCol++)
					{
						float zeroScaleCol = 1.0f;
						if (iCol < kZeroOutVerts)
							zeroScaleCol = (float)iCol/kZeroOutVerts;
						else if (kSubdivisions-iCol < kZeroOutVerts)
							zeroScaleCol = (float)(kSubdivisions-iCol)/kZeroOutVerts;

						float zeroScale = zeroScaleRow*zeroScaleCol;

						int index = iRow*(kSubdivisions+1) + iCol;

						float x = kVertDist*(-kSubdivisions/2 + iRow);
						float z = kVertDist*(-kSubdivisions/2 + iCol);
						float dist = Sqrt(x*x + z*z);
						float scaledDist = dist/widthScale;
						float heightPct = 0.5f*(1.0f + Cos(scaledDist*PI));

						hkVector4 pos = hkVector4(x, zeroScale*height*heightPct, z);
						m_pGeo->m_vertices[index] = pos;
					}
				}

				m_pGeo->m_triangles.setSize(kSubdivisions*kSubdivisions*2);
				for (int iRow=0; iRow<kSubdivisions; iRow++)
				{
					for (int iCol=0; iCol<kSubdivisions; iCol++)
					{
						int index = 2*(iRow*kSubdivisions + iCol);
						int vertUL = iRow*(kSubdivisions+1) + iCol;
						int vertUR = iRow*(kSubdivisions+1) + iCol + 1;
						int vertBL = (iRow+1)*(kSubdivisions+1) + iCol;
						int vertBR = (iRow+1)*(kSubdivisions+1) + iCol + 1;

						m_pGeo->m_triangles[index].set(vertUL, vertUR, vertBL);
						m_pGeo->m_triangles[index+1].set(vertUR, vertBR, vertBL);
					}
				}
						

				hknpDefaultCompressedMeshShapeCinfo meshInfo(m_pGeo);
				hknpCompressedMeshShape* pNewShape = NDI_NEW hknpCompressedMeshShape( meshInfo );
				pNewShape->m_userData = Pat(0).m_bits;

				HavokProtoBody* pProto = const_cast<HavokProtoBody*>(pRigidBody->GetProtoBody());
				if (pProto && pProto->m_pMeshDrawData)
				{
					pProto->m_pMeshDrawData->Release();
					pProto->m_pMeshDrawData = nullptr;
				}

				pRigidBody->SetHavokShape(pNewShape);

				pNewShape->removeReference();
			}
		}
	}
}

void DebugHeightMeshObject::UpdateVerts()
{
	if (m_pGeo)
	{
		int numTris = m_pGeo->m_triangles.getSize();

		int numVerts = 3*numTris;
		if (numVerts != m_numVerts)
		{
			if (m_pVerts)
				NDI_DELETE [] m_pVerts;

			m_numVerts = numVerts;
			m_pVerts = NDI_NEW(kAllocDebug) global::VStreamPosColor[m_numVerts];
		}

		global::VStreamPosColor *pVerts = NDI_NEW(kAllocSingleGameFrame) global::VStreamPosColor[3*numTris];

		const RenderCamera& cam = GetRenderCamera(0);
		const Point camPos(cam.m_position);
		const Vector lightDir = Vector(Vec4(kUnitZAxis) * cam.m_mtx.m_viewToWorld);

		float angleThresholdDot = Cos(DEGREES_TO_RADIANS(35.0f));

		for (int iTri=0; iTri<numTris; iTri++)
		{
			int index[3];
			index[0] = m_pGeo->m_triangles[iTri].m_a;
			index[1] = m_pGeo->m_triangles[iTri].m_b;
			index[2] = m_pGeo->m_triangles[iTri].m_c;

			Point vert[3];
			for (int iVertOffset=0; iVertOffset<3; iVertOffset++)
			{
				hkVector4 v = m_pGeo->m_vertices[index[iVertOffset]];

				Point posLs((float)v.getComponent(0), (float)v.getComponent(1), (float)v.getComponent(2));
				vert[iVertOffset] = GetLocator().TransformPoint(posLs);
			}

			const Vector triNorm = Normalize(Cross(vert[1] - vert[0], vert[2] - vert[0]));
			float upAngleDot = Dot(triNorm, kUnitYAxis);
			bool tooSteep = upAngleDot < angleThresholdDot;


			float colorMult = Abs(Dot(lightDir, triNorm));

			for (int iVertOffset=0; iVertOffset<3; iVertOffset++)
			{
				int iVert = iTri*3 + iVertOffset;

				m_pVerts[iVert].m_x = vert[iVertOffset].X();
				m_pVerts[iVert].m_y = vert[iVertOffset].Y();
				m_pVerts[iVert].m_z = vert[iVertOffset].Z();

				int rb = tooSteep ? 128 : 0;
				int gb = tooSteep ? 128 : 160;
				int bb = tooSteep ? 128 : 0;

				int r = MinMax((int)(colorMult*rb), 0, 255);
				int g = MinMax((int)(colorMult*gb), 0, 255);
				int b = MinMax((int)(colorMult*bb), 0, 255);

				m_pVerts[iVert].m_color = Abgr8FromRgba(r, g, b, 255);
			}
		}
	}
}

void DebugHeightMeshObject::DrawMesh()
{
	UpdateVerts();

	int numTris = m_numVerts/3;
	if (numTris > 0)
		g_prim.DrawNoHide( DebugTriangleList(m_pVerts, numTris) );
}



class DebugHeightMeshObject::Active : public NdSimpleObject::Active
{
	typedef NdSimpleObject::Active ParentStateClass;
	BIND_TO_PROCESS(DebugHeightMeshObject);

	virtual void Update() override
	{
		DebugHeightMeshObject& self = Self();

		self.DrawMesh();
	}

	virtual void EventHandler(Event& event) override
	{
		DebugHeightMeshObject& self = Self();

		if (event.GetMessage() == SID("set-bump-height"))
		{
			float height = event.Get(0).GetAsF32();
			float widthScale = event.Get(1).GetAsF32();

			self.UpdateHeightMeshSinWave(height, widthScale);
			self.UpdateVerts();
		}
	}
};

STATE_REGISTER(DebugHeightMeshObject, Active, kPriorityNormal);


void ForceLinkDebugHeightMeshObject()
{}

