/*
* Copyright (c) 2007 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "gamelib/gameplay/nav/bytemap128.h"

#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/bitmap-2d.h"

#define USE_PS3_INTRINSICS (NDI_ARCH_PPU || NDI_ARCH_SPU)

//////////////////////////////////////////////////////////////////////////
///
///
/// class Bytemap128 (PPU + SPU)
///
///
//////////////////////////////////////////////////////////////////////////

void Bytemap128::Fill( U8 fillValue )
{
#if USE_PS3_INTRINSICS
	// vectorized version
	VU8* pDest = (VU8*) &m_data[0][0];
	VU8 vFillValue = (VU8){ fillValue, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	vFillValue = vec_splat( vFillValue, 0 );
		
	I32F quadCount = kCellCount / 16;
	for (I32F iQuad = 0; iQuad < quadCount; ++iQuad)
	{
		*pDest++ = vFillValue;
	}
#else
	// non-vectorized version
	U8* pOut = &m_data[0][0];
	for (U32F iCell = 0; iCell < kCellCount; ++iCell)
	{
		pOut[ iCell ] = fillValue;
	}
#endif
}

///
/// Turns a 128 x 128 bitmap into a 128 x 128 byte map
///
///  each byte of the output corresponds to a single bit in the bitmap.
///    if the bit is zero, the byte is also zero
///    if the bit is one, the byte is set to the value of oneValue
///
void Bytemap128::SetFromBitmap( const Bitmap128& bitmap, U8 oneValue )
{
#if USE_PS3_INTRINSICS
	// vectorized version
	VU8* pDest = (VU8*) &m_data[0][0];
	const VU8 vShift2Bytes = (VU8)(16);
	const VU8 vPermMask = (VU8){0x0,0x0,0x0,0x0, 0x0,0x0,0x0,0x0, 0x1,0x1,0x1,0x1, 0x1,0x1,0x1,0x1};
	const VU8 vShiftLeftAmt = (VU8){0,1,2,3, 4,5,6,7, 0,1,2,3, 4,5,6,7};
	VU8 vFillValue = (VU8){ oneValue, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	vFillValue = vec_splat( vFillValue, 0 );
		
	const VU8* pSrc = PunPtr<const VU8*>(&bitmap);
	I32F srcQuadCount = kCellCount / 128;
	for (I32F iQuad = 0; iQuad < srcQuadCount; ++iQuad)
	{
		VU8 vSrcData = *pSrc++;
		const I32F hwordCount = 128 / 16;
		for (I32F i = 0; i < hwordCount; ++i)
		{
			// for each halfword of the src data (16 bits) we want to write out 16 bytes to the dest,
			//  each 0 bit should output a 0 byte and each 1 bit a 0xff byte
			//
			// first step:
			//   given most significant halfword of vSrcData is nibbles [abcd]
			//   we are going to shuffle these so that we get a quadword [abababab abababab cdcdcdcd cdcdcdcd]
			//   so that each nibble is in a different byte
			VU8 vTmp = vec_perm( vSrcData, vSrcData, vPermMask ); // vTmp = [abababab abababab cdcdcdcd cdcdcdcd]
			// second step:
			//   shift a different bit from each of the nibbles into the msb of each byte
			vTmp = vec_sl( vTmp, vShiftLeftAmt );
			vTmp = vec_sra( vTmp, (VU8)(7) );  // replicate msb of each byte to lower order bits
			vTmp = vec_and( vTmp, vFillValue );  // apply the fillValue
			// output a quadword to dest
			*pDest++ = vTmp;
			// advance to the next halfword
			vSrcData = vec_slo( vSrcData, vShift2Bytes );
		}
	}
#else
	// non-vectorized version
	U8* pOut = &m_data[0][0];
	for (U32F iCell = 0; iCell < kCellCount; ++iCell)
	{
		if (bitmap.IsBitSet( iCell ))
		{
			pOut[ iCell ] = oneValue;
		}
		else
		{
			pOut[ iCell ] = 0;
		}
	}
#endif

#if 0
	for (U32F iCell = 0; iCell < kCellCount; ++iCell)
	{
		if (bitmap.IsBitSet( iCell ))
		{
			NAV_ASSERT( pOutBytemap[iCell] == oneValue );
		}
		else
		{
			NAV_ASSERT( pOutBytemap[iCell] == 0 );
		}
	}
#endif
}

void Bytemap128::SetFromBitmap( const Bitmap128& bitmap, U8 zeroValue, U8 oneValue )
{
#if USE_PS3_INTRINSICS
	// vectorized version
	VU8* pDest = (VU8*) &m_data[0][0];
	const VU8 vShift2Bytes = (VU8)(16);
	const VU8 vPermMask = (VU8){0x0,0x0,0x0,0x0, 0x0,0x0,0x0,0x0, 0x1,0x1,0x1,0x1, 0x1,0x1,0x1,0x1};
	const VU8 vShiftLeftAmt = (VU8){0,1,2,3, 4,5,6,7, 0,1,2,3, 4,5,6,7};
	VU8 vZeroValue = (VU8){ zeroValue, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	vZeroValue = vec_splat( vZeroValue, 0 );
	VU8 vOneValue = (VU8){ oneValue, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	vOneValue = vec_splat( vOneValue, 0 );

	const VU8* pSrc = PunPtr<const VU8*>(&bitmap);
	I32F srcQuadCount = kCellCount / 128;
	for (I32F iQuad = 0; iQuad < srcQuadCount; ++iQuad)
	{
		VU8 vSrcData = *pSrc++;
		const I32F hwordCount = 128 / 16;
		for (I32F i = 0; i < hwordCount; ++i)
		{
			// for each halfword of the src data (16 bits) we want to write out 16 bytes to the dest,
			//  each 0 bit should output a 0 byte and each 1 bit a 0xff byte
			//
			// first step:
			//   given most significant halfword of vSrcData is nibbles [abcd]
			//   we are going to shuffle these so that we get a quadword [abababab abababab cdcdcdcd cdcdcdcd]
			//   so that each nibble is in a different byte
			VU8 vTmp = vec_perm( vSrcData, vSrcData, vPermMask ); // vTmp = [abababab abababab cdcdcdcd cdcdcdcd]
			// second step:
			//   shift a different bit from each of the nibbles into the msb of each byte
			vTmp = vec_sl( vTmp, vShiftLeftAmt );
			vTmp = vec_sra( vTmp, (VU8)(7) );  // replicate msb of each byte to lower order bits
			vTmp = vec_sel(vZeroValue, vOneValue, vTmp);//( vTmp, vFillValue );  // apply the fillValue
			// output a quadword to dest
			*pDest++ = vTmp;
			// advance to the next halfword
			vSrcData = vec_slo( vSrcData, vShift2Bytes );
		}
	}
#else
	// non-vectorized version
	U8* pOut = &m_data[0][0];
	for (U32F iCell = 0; iCell < kCellCount; ++iCell)
	{
		if (bitmap.IsBitSet( iCell ))
		{
			pOut[ iCell ] = oneValue;
		}
		else
		{
			pOut[ iCell ] = zeroValue;
		}
	}
#endif
}

void Bytemap128::SetFromBitmap( const Bitmap128& bitmap, U8 zeroValue, U8 oneValue, I32 shiftX, I32 shiftZ )
{
	for (U32 iX = 0; iX < kDim; ++iX)
	{
		for (U32 iZ = 0; iZ < kDim; ++iZ)
		{
			I32 iXShifted = iX - shiftX;
			I32 iZShifted = iZ - shiftZ;
			I32 srcX = iXShifted >> 1;
			I32 srcZ = iZShifted >> 1;
			I32 neighborX = MinMax(srcX + ((iXShifted & 1) ? 1 : -1), 0, (I32)kDim - 1);
			I32 neighborZ = MinMax(srcZ + ((iZShifted & 1) ? 1 : -1), 0, (I32)kDim - 1);
			srcX = MinMax(srcX, 0, (I32)kDim - 1);
			srcZ = MinMax(srcZ, 0, (I32)kDim - 1);
			I32 result = (bitmap.IsBitSet(bitmap.GetCellIndex(srcX, srcZ)) ? oneValue : zeroValue) * 9;
			result += (bitmap.IsBitSet(bitmap.GetCellIndex(neighborX, srcZ)) ? oneValue : zeroValue) * 3;
			result += (bitmap.IsBitSet(bitmap.GetCellIndex(srcX, neighborZ)) ? oneValue : zeroValue) * 3;
			result += (bitmap.IsBitSet(bitmap.GetCellIndex(neighborX, neighborZ)) ? oneValue : zeroValue);
			m_data[iZ][iX] = result >> 4;
		}
	}
}


//
//   Bytemap128::Merge - create a Bytemap by merging two Bytemap according to the selectMap
//
// each bit in the selectMap selects which of the source Bytemaps should provide the
// output value at the corresponding index.
//
// This function allows the output Bytemap to be the same as one or both of the
// inputs.
//
void Bytemap128::Merge( const Bitmap128& selectMap, const Bytemap128& if0, const Bytemap128& if1 )
{
#if USE_PS3_INTRINSICS
	VU8* pDest = (VU8*) &m_data[0][0];
	const VU8* pSrc0 = (const VU8*) &if0.m_data[0][0];
	const VU8* pSrc1 = (const VU8*) &if1.m_data[0][0];
	const VU8* pSelectMap = PunPtr<const VU8*>(&selectMap);
	const VU8 vShift2Bytes = (VU8)(16);
	const VU8 vPermMask = (VU8){0x0,0x0,0x0,0x0, 0x0,0x0,0x0,0x0, 0x1,0x1,0x1,0x1, 0x1,0x1,0x1,0x1};
	const VU8 vShiftLeftAmt = (VU8){0,1,2,3, 4,5,6,7, 0,1,2,3, 4,5,6,7};

	I32F srcQuadCount = kCellCount / 128;
	for (I32F iQuad = 0; iQuad < srcQuadCount; ++iQuad)
	{
		VU8 vSelMap = *pSelectMap++;
		const I32F hwordCount = 128 / 16;
		for (I32F i = 0; i < hwordCount; ++i)
		{
			// for each halfword of the src data (16 bits) we want to write out 16 bytes to the dest,
			//  each 0 bit should output a 0 byte and each 1 bit a 0xff byte
			//
			// first step:
			//   given most significant halfword of vSelMap is nibbles [abcd]
			//   we are going to shuffle these so that we get a quadword [abababab abababab cdcdcdcd cdcdcdcd]
			//   so that each nibble is in a different byte
			VU8 vSel = vec_perm( vSelMap, vSelMap, vPermMask ); // vSel = [abababab abababab cdcdcdcd cdcdcdcd]
			// second step:
			//   shift a different bit from each of the nibbles into the msb of each byte
			vSel = vec_sl( vSel, vShiftLeftAmt );
			vSel = vec_sra( vSel, (VU8)(7) );  // replicate msb of each byte to lower order bits

			VU8 vSrc0 = *pSrc0++;
			VU8 vSrc1 = *pSrc1++;
			VU8 vTmp = vec_sel( vSrc0, vSrc1, vSel );
			// output a quadword to dest
			*pDest++ = vTmp;
			// advance to the next halfword
			vSelMap = vec_slo( vSelMap, vShift2Bytes );
		}
	}
#else
	// non-vectorized version
	U8* pOut = &m_data[0][0];
	const U8* pSrcIf0 = &if0.m_data[0][0];
	const U8* pSrcIf1 = &if1.m_data[0][0];
	for (U32F iCell = 0; iCell < kCellCount; ++iCell)
	{
		if (selectMap.IsBitSet( iCell ))
		{
			pOut[ iCell ] = pSrcIf1[ iCell ];
		}
		else
		{
			pOut[ iCell ] = pSrcIf0[ iCell ];
		}
	}
#endif
}

Scalar Bytemap128::IntegrateLineLs( Point_arg pt0Ls, Point_arg pt1Ls, Scalar_arg outOfBoundsValue ) const
{
	PROFILE(AI, Bitmap128_Probe);

	I32 x0 = (I32)pt0Ls.X();
	I32 z0 = (I32)pt0Ls.Z();
	I32 x1 = (I32)pt1Ls.X();
	I32 z1 = (I32)pt1Ls.Z();

	I32 dX = Abs(x1 - x0);
	I32 dZ = Abs(z1 - z0);
	I32 sX = (x0 < x1) ? 1 : -1;
	I32 sZ = (z0 < z1) ? 1 : -1;

	I32 err = dX - dZ;

	I32 totalCost = GetValue(MinMax(x0, 0, (I32)kDim - 1), MinMax(z0, 0, (I32)kDim - 1));

	while(x0 != x1 || z0 != z1)
	{
		I32 e2 = err << 1;
		if (e2 > -dZ)
		{
			err -= dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		totalCost += GetValue(MinMax(x0, 0, (I32)kDim - 1), MinMax(z0, 0, (I32)kDim - 1));
	}

	return (F32)totalCost * Dist(pt0Ls, pt1Ls) / (F32)(Max(dX, dZ) + 1);


	//Vector moveLs  = VectorXz(pt1Ls - pt0Ls);
	//const Scalar moveLen = Length( moveLs );
	//Scalar pathSum = Scalar( kZero );

	//if (moveLen < SCALAR_LC(0.000001f))
	//{
	//	// no movement
	//	return pathSum;
	//}
	//Vector moveDirLs = Normalize( moveLs );
	//Vector invMoveDirLs = Recip(moveDirLs);
	//Vector nextT0 = - (pt0Ls - kOrigin) * invMoveDirLs;  // weird vector mul
	////
	//// parametric form of this line is:
	////
	////    lineXZ(tt) = pt0Ls + moveDirLs * tt,  where tt ranges from 0 to moveLen
	////
	//// split into x and z components we have:
	////
	////    line.x = pt0Ls.x + moveDirLs.x * tt,
	////    line.z = pt0Ls.z + moveDirLs.z * tt
	////
	//// to find the values of tt where the line crosses certain x or z boundaries, we solve for
	////   tt in terms of line.x and line.z:
	////
	////    ttX = ( line.x - pt0Ls.x ) / moveDirLs.x,
	////    ttZ = ( line.z - pt0Ls.z ) / moveDirLs.z
	////
	////  refactoring into vector form:
	////
	////    ttXZ = nextT0 + lineXZ * T(invMoveDirLs)
	////
	////    (note the multiply here is not a dot or cross product, it is a component-by-component
	////     multiply, hence the 'T' transpose operation to make it into a matrix multiply)
	////

	//// if move vector is zero,
	//if ( Abs(moveLs.X()) < kSmallFloat )
	//{
	//	// set this equation to always return a value large enough to be discarded (not QNaN)
	//	nextT0.SetX(2.0f*moveLen);
	//	invMoveDirLs.SetX( kZero );
	//}
	//if ( Abs(moveLs.Z()) < kSmallFloat )
	//{
	//	// set this equation to always return a value large enough to be discarded (not QNaN)
	//	nextT0.SetZ(2.0f*moveLen);
	//	invMoveDirLs.SetZ( kZero );
	//}
	//const Scalar zero = SCALAR_LC(0.0f);
	//// parametric endpoints of the line segment
	//Scalar tt0 = zero;
	//Scalar tt1 = moveLen;
	//{
	//	// clip to map boundary
	//	Vector bound0Ls = VECTOR_LC( 0.01f, 0.0f, 0.01f );
	//	Vector bound1Ls = VECTOR_LC( kDim-0.01f, 0, kDim-0.01f );
	//	// solve for T at the boundaries
	//	Vector bound0T = nextT0 + bound0Ls * invMoveDirLs;
	//	Vector bound1T = nextT0 + bound1Ls * invMoveDirLs;
	//	Vector minT = Min(bound0T, bound1T);
	//	Vector maxT = Max(bound0T, bound1T);
	//	// if x move is zero and x coord is within bounds,
	//	if ( Abs(moveLs.X()) < kSmallFloat && pt0Ls.X() >= bound0Ls.X() && pt0Ls.X() <= bound1Ls.X() )
	//	{
	//		minT.SetX( zero );
	//	}
	//	// if z move is zero and z coord is within bounds,
	//	if ( Abs(moveLs.Z()) < kSmallFloat && pt0Ls.Z() >= bound0Ls.Z() && pt0Ls.Z() <= bound1Ls.Z() )
	//	{
	//		minT.SetZ( zero );
	//	}
	//	tt0 = Max( tt0, Max(minT.X(), minT.Z()) );
	//	tt1 = Min( tt1, Min(maxT.X(), maxT.Z()) );
	//	if (tt0 > tt1)
	//	{
	//		// the line falls completely outside the mep
	//		return moveLen * outOfBoundsValue;
	//	}
	//	Scalar clippedAmount = tt0 + (moveLen - tt1);
	//	ASSERT( clippedAmount >= 0.0f ); // never negative
	//	pathSum += clippedAmount * outOfBoundsValue;
	//}
	//const Point startLs = pt0Ls + moveDirLs * tt0;
	//I32F iStartX = I32F( startLs.X() );
	//I32F iStartZ = I32F( startLs.Z() );
	//ASSERT( iStartX >= 0 && iStartX < kDim );
	//ASSERT( iStartZ >= 0 && iStartZ < kDim );
	//iStartX = MinMax(iStartX, I32F(0), I32F(kDim - 1));
	//iStartZ = MinMax(iStartZ, I32F(0), I32F(kDim - 1));
	//Vector nextLs(iStartX, 0, iStartZ);

	//Vector stepX(kUnitXAxis);
	//Vector stepZ(kUnitZAxis);
	//I32F iXStep = 1;
	//I32F iZStep = 1;
	//if ( moveLs.X() < zero )
	//{
	//	stepX = -stepX;
	//	iXStep = -1;
	//}
	//else
	//{
	//	nextLs += stepX;
	//}
	//if ( moveLs.Z() < zero )
	//{
	//	stepZ = -stepZ;
	//	iZStep = -1;
	//}
	//else
	//{
	//	nextLs += stepZ;
	//}

	//// tt ranges from tt0 to tt1
	//Scalar tt = tt0;
	//I32F iX = iStartX;
	//I32F iZ = iStartZ;
	//while ( true )
	//{
	//	// nextLs.X is the grid X coordinate of the next probe intersection
	//	// nextLs.Z is the grid Z coordinate of the next probe intersection
	//	// startLs is the start point transformed into grid coordinates
	//	// invMoveDirLs.X is 1.0 / moveDirLs.X
	//	// invMoveDirLs.Z is 1.0 / moveDirLs.Z
	//	// nextT.X is the parametric coordinate of the intersection with a cell boundary in X
	//	// nextT.Z is the parametric coordinate of the intersection with a cell boundary in Z
	//	// nextT is solving 2 different equations for the parametric t simultaneously
	//	Vector nextT = nextT0 + nextLs * invMoveDirLs;

	//	ASSERT( iX >= 0 && iX < kDim );
	//	ASSERT( iZ >= 0 && iZ < kDim );
	//	I32F iPrevX = iX;
	//	I32F iPrevZ = iZ;
	//	Scalar prevT = tt;
	//	// if X is smaller,
	//	if (nextT.X() < nextT.Z())
	//	{
	//		// step in X
	//		tt = nextT.X();
	//		iX += iXStep;
	//		nextLs += stepX;
	//	}
	//	else
	//	{
	//		// step in Z
	//		tt = nextT.Z();
	//		iZ += iZStep;
	//		nextLs += stepZ;
	//	}
	//	Scalar stepDist = (Min(tt, tt1) - prevT);
	//	ASSERT( stepDist >= 0.0f );
	//	pathSum += stepDist * float(GetValue( iPrevX, iPrevZ ));
	//	if (tt > tt1)
	//	{
	//		break;
	//	}
	//}
	//return pathSum;
}

void BytemapAddFromRasterTable( Bytemap128* pOut,
								const Bitmap128::RasterTable& rasterTable,
								U8 value
								)
{
	PROFILE(AI, BytemapAddFromRasterTable);
	const I32F iXMax = Bytemap128::kDim - 1;
	const I32F iZMin = rasterTable.m_iZMin;
	const I32F iZMax = rasterTable.m_iZMax;

	const I16* rowMin = rasterTable.m_rowMin;
	const I16* rowMax = rasterTable.m_rowMax;

	// this can be easily vectorized
	for (I32F iZ = iZMin; iZ <= iZMax; ++iZ)
	{
		I32F iX0 = rowMin[ iZ ];
		I32F iX1 = rowMax[ iZ ] - 1;
		if ( iX0 < Bytemap128::kDim && iX1 >= 0 )
		{
			U8* pRow = pOut->m_data[ iZ ];
			iX0 = Max( iX0, I32F(0) );
			iX1 = Min( iX1, iXMax );
			for (I32F iX = iX0; iX <= iX1; ++iX)
			{
				// add with saturation
				U32F iVal = Min( U32F(255), U32F(pRow[ iX ]) + value );
				pRow[ iX ] = iVal;
			}
		}
	}
}

void Bytemap128::AddCircleLs( Point_arg centerLs, Scalar_arg radiusLs, U8 value )
{
	Bitmap128::RasterTable rasterTable;
	rasterTable.MakeCircleLs( centerLs, radiusLs );
	BytemapAddFromRasterTable( this, rasterTable, value );
}

void Bytemap128::Shift( I32F iDx, I32F iDz, U8 fillValue )
{
	{
		// shift in X
		I32F iXStart = 0;
		I32F iXStep = 0;
		if (iDx > 0)
		{
			iXStart = kDim - 1;
			iXStep = -1;
		}
		else if (iDx < 0)
		{
			iXStart = 0;
			iXStep = 1;
		}
		if (iXStep != 0)
		{
			I32F iXCount = kDim - Abs(iDx);
			for (I32F iZ = 0; iZ < kDim; ++iZ)
			{
				I32F iX = iXStart;
				for (I32F count = iXCount; count > 0; --count)
				{
					m_data[ iZ ][ iX ] = m_data[ iZ ][ iX - iDx ];
					iX += iXStep;
				}
				for (I32F count = Min(I32F(kDim), Abs(iDx)); count > 0; --count)
				{
					m_data[ iZ ][ iX ] = fillValue;
					iX += iXStep;
				}
			}
		}
	}
	{
		// shift in Z
		I32F iZStart = 0;
		I32F iZStep = 0;
		if (iDz > 0)
		{
			iZStart = kDim - 1;
			iZStep = -1;
		}
		else if (iDz < 0)
		{
			iZStart = 0;
			iZStep = 1;
		}
		if (iZStep != 0)
		{
			I32F iZCount = kDim - Abs(iDz);
			for (I32F iX = 0; iX < kDim; ++iX)
			{
				I32F iZ = iZStart;
				for (I32F count = iZCount; count > 0; --count)
				{
					m_data[ iZ ][ iX ] = m_data[ iZ - iDz ][ iX ];
					iZ += iZStep;
				}
				for (I32F count = Min(I32F(kDim), Abs(iDz)); count > 0; --count)
				{
					m_data[ iZ ][ iX ] = fillValue;
					iZ += iZStep;
				}
			}
		}
	}
}

void Bytemap128::ScaleInPlace2x()
{
	for (I32F iZ = kDim - 1; iZ >= 0; --iZ)
	{
		for (I32F iX = kDim - 1; iX >= 0; --iX)
		{
			m_data[ iZ ][ iX ] = m_data[ iZ >> 1 ][ iX >> 1 ];
		}
	}
}

#if ! NDI_ARCH_SPU

void Bytemap128::DebugDraw( const Transform& tmLsToWs, Scalar_arg upScale ) const
{
	STRIP_IN_FINAL_BUILD;
	PROFILE( AI, NavCon_DebugDrawCost );

	const Transform& tm = tmLsToWs;

	Vector baseUp = (Vector(kUnitYAxis) * upScale) * tm;
	const float lineWidth = 3.0f;
	const U32F kXDim = Bytemap128::kDim;
	const U32F kZDim = Bytemap128::kDim;
	for (I32F iZ = 0; iZ < kZDim; ++iZ)
	{
		U8 iPrevY = 0;
		Point ptPrevWs( kOrigin );
		bool prevValid = false;
		Color prevColor = kColorBlack;
		for (I32F iX = 0; iX < kXDim; ++iX)
		{
			U8 iY = GetValue(iX, iZ);
			if (iY > 1)
			{
				Color color = Lerp( kColorGreen, kColorRed, Min(1.0f, iY/64.0f));
				Point ptLs(iX+0.5f, 0.0f, iZ+0.5f);
				Point ptWs = ptLs * tm;
				if (prevValid)
				{
					g_prim.Draw( DebugLine( ptPrevWs + baseUp * float(iPrevY),
											ptWs + baseUp * float(iY),
											prevColor,
											color,
											lineWidth
											)
								 );
				}
				else
				{
					g_prim.Draw( DebugLine( ptWs, baseUp * float(iY), kColorBlack, color, lineWidth ) );
				}
				ptPrevWs = ptWs;
				iPrevY = iY;
				prevValid = true;
				prevColor = color;
			}
			else
			{
				if (prevValid)
				{
					g_prim.Draw( DebugLine( ptPrevWs, baseUp * float(iPrevY), kColorBlack, prevColor, lineWidth ));
				}
				prevValid = false;
				prevColor = kColorBlack;
			}
		}
	}
}

#endif



