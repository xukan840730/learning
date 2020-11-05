/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef _NDLIB_BYTEMAP_H_
#define _NDLIB_BYTEMAP_H_

//////////////////////////////////////////////////////////////////////////
///
///  this is shared PPU/SPU code
///
//////////////////////////////////////////////////////////////////////////

class Bitmap128;

class ALIGNED(16) Bytemap128
{
public:
	U8 GetValue( U32F i ) const { ASSERT( i < kCellCount ); return m_data[0][i]; }
	U8 GetValue( U32F iX, U32F iZ ) const { ASSERT( iX < kDim && iZ < kDim ); return m_data[iZ][iX]; }

	void SetValue( U32F i, U8 newValue ) { ASSERT( i < kCellCount ); m_data[0][i] = newValue; }
	U32F GetCellIndex( I32F iX, I32F iZ ) const {ASSERT( U32F(iX) < kDim && U32F(iZ) < kDim ); return iX + iZ * kDim;}
	void Fill( U8 fillValue );
	void SetFromBitmap( const Bitmap128& bitmap, U8 oneValue );
	void SetFromBitmap( const Bitmap128& bitmap, U8 zeroValue, U8 oneValue );
	void SetFromBitmap( const Bitmap128& bitmap, U8 zeroValue, U8 oneValue, I32 shiftX, I32 shiftZ);
	void Merge(const Bitmap128& selectMap, const Bytemap128& if0, const Bytemap128& if1);
	void AddCircleLs( Point_arg centerLs, Scalar_arg radiusLs, U8 value );
	Scalar IntegrateLineLs( Point_arg pt0Ls, Point_arg pt1Ls, Scalar_arg outOfBoundsValue ) const;
	void Shift( I32F iDx, I32F iDz, U8 fillValue );
	void ScaleInPlace2x();
	void DebugDraw( const Transform& tmLsToWs, Scalar_arg upScale ) const;

	static const U32F kDim = 128;
	static const U32F kCellCount = kDim * kDim;
	U8 m_data[kDim][kDim]; // 16K
};

#endif
