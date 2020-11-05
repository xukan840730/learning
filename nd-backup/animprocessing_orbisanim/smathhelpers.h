/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "shared/math/mat44.h"
#include "shared/math/transform.h"
#include "shared/math/quat.h"

#include "icelib/geom/cgmath.h"
#include "icelib/geom/cgvec.h"
#include "icelib/geom/cgquat.h"

namespace OrbisAnim {
	namespace Tools {

	// useful functions for converting ITGEOM types to equivalent SMath types
	inline SMath::Vec4 SMathVec4(ITGEOM::Vec4 const& v) { return SMath::Vec4(v.x, v.y, v.z, v.w); }
	inline SMath::Vec4 SMathVec4(ITGEOM::Vec3 const& v, float w) { return SMath::Vec4(v.x, v.y, v.z, w); }
	inline SMath::Point SMathPoint(ITGEOM::Vec3 const& v) { return SMath::Point(v.x, v.y, v.z); }
	inline SMath::Point SMathPoint(ITGEOM::Vec4 const& v) { return SMath::Point(v.x, v.y, v.z); }
	inline SMath::Vector SMathVector(ITGEOM::Vec3 const& v) { return SMath::Vector(v.x, v.y, v.z); }
	inline SMath::Vector SMathVector(ITGEOM::Vec4 const& v) { return SMath::Vector(v.x, v.y, v.z); }
	inline SMath::Quat SMathQuat(ITGEOM::Quat const& q) { return SMath::Quat(q.x, q.y, q.z, q.w); }

	// useful functions for converting SMath types to equivalent ITGEOM types
	inline ITGEOM::Vec3 ITGEOMVec3(SMath::Point const& v) { return ITGEOM::Vec3( v.X(), v.Y(), v.Z() ); }
	inline ITGEOM::Vec3 ITGEOMVec3(SMath::Vector const& v) { return ITGEOM::Vec3( v.X(), v.Y(), v.Z() ); }
	inline ITGEOM::Quat ITGEOMQuat(SMath::Quat const& q) { return ITGEOM::Quat( q.X(), q.Y(), q.Z(), q.W() ); }

	// useful functions for converting between ITGEOM types
	inline ITGEOM::Vec4 ITGEOMVec4(ITGEOM::Vec3 const& v, float w) { return ITGEOM::Vec4( v.X(), v.Y(), v.Z(), w ); }

	// useful functions for SMath types
	inline SMath::Mat44 SMathMat44_BuildAffine(SMath::Vector const& vScale, SMath::Quat const& qRotation, SMath::Point const& vTranslation)
	{
		SMath::Mat44 mPreScale(	SMath::Vec4( vScale.X(), 0.0f, 0.0f, 0.0f ),
								SMath::Vec4( 0.0f, vScale.Y(), 0.0f, 0.0f ),
								SMath::Vec4( 0.0f, 0.0f, vScale.Z(), 0.0f ),
								SMath::Vec4( SMath::kUnitWAxis ) );
		return mPreScale * SMath::BuildTransform( qRotation, vTranslation.GetVec4() );
	}
	void SMathMat44_DecomposeAffine(SMath::Mat44 const& m, SMath::Vector *pvScale, SMath::Quat *pqRotation, SMath::Point *pvTranslation, SMath::Mat44 *pmSkew = NULL);

	/// Since SMath types require memory alignment, AlignedArray<T> 
	/// is useful for allocating arrays of these types.
	template<class T>
	class AlignedArray {
	public:
		AlignedArray() : m_pAlloc(NULL), m_size(0) {}
		AlignedArray(size_t size) { Alloc(size); }
		AlignedArray(size_t size, T const& valueClear)
		{
			Alloc(size);
			for (size_t i = 0; i < size; ++i)
				m_pPtr[i] = valueClear;
		}
		~AlignedArray() { Free(); }

		size_t size() const { return m_size; }
		bool empty() const { return m_size == 0; }

		void clear() { Free(); }
		void resize(size_t size) { Realloc(size); }
		void resize(size_t size, T const& valueClear)
		{
			size_t sizePrev = m_size;
			Realloc(size);
			for (size_t i = sizePrev; i < size; ++i)
				m_pPtr[i] = valueClear;
		}

		T& operator[](size_t i) { assert(i < m_size); return m_pPtr[i]; }
		T operator[](size_t i) const { assert(i < m_size); return m_pPtr[i]; }

	private:
		void Free() {
			delete[] m_pAlloc, m_pAlloc = NULL;
			m_pPtr = NULL;
			m_size = 0;
		}
		void Alloc(size_t size) {
			m_pAlloc = new U8[ sizeof(T)*size + 0x10 ];
			m_pPtr = (T*)(m_pAlloc + (((U8 const*)NULL - m_pAlloc) & 0xF));
			m_size = size;
		}
		void Realloc(size_t size) {
			if (m_size) {
				U8 *pAllocPrev = m_pAlloc;
				T *pPtrPrev = m_pPtr;
				size_t sizePrev = (m_size < size) ? m_size : size;
				Alloc(size);
				memcpy(m_pPtr, pPtrPrev, sizePrev);
				delete[] pAllocPrev;
			} else
				Alloc(size);
		}

		U8 *m_pAlloc;
		T *m_pPtr;
		size_t m_size;
	};

	}	//namespace Tools
}	//namespace OrbisAnim

