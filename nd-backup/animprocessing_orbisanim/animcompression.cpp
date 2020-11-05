/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <queue>

#include "animcompression.h"
#include "animmetadata.h"
#include "animobjectrootanims.h"
#include "icelib/geom/bdbox.h"
#include "smathhelpers.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace OrbisAnim {
	namespace Tools {

	// returns the number of bits needed to hold value, between 0 to 32
	// for example GetNumberOfBits(0x12) = 5, GetNumberOfBits(0) = 0, GetNumberOfBits(0x80000000) = 32
	extern unsigned GetNumberOfBits(U32 value);

	extern float ComputeError_Vec3(ITGEOM::Vec3 const& v, ITGEOM::Vec3 const& vRef);
	extern float ComputeError_Quat(ITGEOM::Quat const& q, ITGEOM::Quat const& qRef);
	extern float ComputeError_Float(float f, float fRef);

	extern bool DoCompress_Vec3_Range(U64_Array& aCompressed, ITGEOM::Vec3Array& avDecompressed, U64_Array& aContextData, const ITGEOM::Vec3Array& avValues, Vec3BitPackingFormat bitPackingFormat);

	template<typename T> static inline T max(T a, T b) { return (a > b) ? a : b; }
	template<typename T> static inline T min(T a, T b) { return (a > b) ? b : a; }
	template<typename T> static inline T clamp(T a, T lo, T hi) { return max(min(a, hi), lo); }

	typedef std::pair<int, U8*> KeyPair;
	struct KeyPairLess {
		bool operator() (KeyPair& a, KeyPair& b) const { return a.first < b.first; }
	};
	
	namespace AnimProcessing {

//-------------------

bool NormalizeBitFormat(Vec3BitPackingFormat& r_format, unsigned reserveBits)
{
	Vec3BitPackingFormat format = r_format;
	U8 minBitsPerChannel = (reserveBits > 0) ? 1 : 0;
	std::priority_queue<KeyPair, std::vector<KeyPair>, KeyPairLess> q;
	U8 numBitsTotal = (U8)(format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ + reserveBits);
	// if we have more than 64 bits total, we reduce the number of bits per channel proportionately:
	if (numBitsTotal > 64) {
		format.m_numBitsTotal = 64;
		U32 extraBitsTotal = numBitsTotal - 64;
		U32 extraBits = extraBitsTotal;
		if (format.m_numBitsX > minBitsPerChannel) {
			U32 extraBitsX = (format.m_numBitsX * extraBitsTotal) / numBitsTotal;
			int rank = ((format.m_numBitsX * extraBitsTotal) % numBitsTotal) * 8 + 5;
			q.push(KeyPair(rank, &format.m_numBitsX));
			format.m_numBitsX = (U8)(format.m_numBitsX - extraBitsX);
			extraBits -= extraBitsX;
		}
		if (format.m_numBitsY > minBitsPerChannel) {
			U32 extraBitsY = (format.m_numBitsY * extraBitsTotal) / numBitsTotal;
			int rank = ((format.m_numBitsY * extraBitsTotal) % numBitsTotal) * 8 + 6;
			q.push(KeyPair(rank, &format.m_numBitsY));
			format.m_numBitsY = (U8)(format.m_numBitsY - extraBitsY);
			extraBits -= extraBitsY;
		}
		if (format.m_numBitsZ > minBitsPerChannel) {
			U32 extraBitsZ = (format.m_numBitsZ * extraBitsTotal) / numBitsTotal;
			int rank = ((format.m_numBitsZ * extraBitsTotal) % numBitsTotal) * 8 + 7;
			q.push(KeyPair(rank, &format.m_numBitsZ));
			format.m_numBitsZ = (U8)(format.m_numBitsZ - extraBitsZ);
			extraBits -= extraBitsZ;
		}
		while (extraBits-- > 0) {
			KeyPair keypair = q.top();
			(*keypair.second)--;
			q.pop();
			keypair.first = extraBits;
			q.push(keypair);
		}
	}
	// numBitsZ + reserveBits must fit in 32 bits:
	if (format.m_numBitsZ > (U8)(32 - reserveBits))
		format.m_numBitsZ = (U8)(32 - reserveBits);
	// Correct any problems with Y crossing more than 4 bytes
	// reduce number of y bits, as anything > 24 bits is a bit of a waste after conversion to floating point
	if ((format.m_numBitsX & 0x7) + format.m_numBitsY > 32)
		format.m_numBitsY = 32 - (format.m_numBitsX & 0x7);
	// Correct any problems with Z + reserve crossing more than 4 bytes
	// reduce number of z bits, as anything > 24 bits is a bit of a waste after conversion to floating point anyway
	if (((format.m_numBitsX + format.m_numBitsY) & 0x7) + format.m_numBitsZ + reserveBits > 32)
		format.m_numBitsZ = (U8)( 32 - (((format.m_numBitsX + format.m_numBitsY) & 0x7) & 0x7) - reserveBits );
	format.m_numBitsTotal = (U8)( format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ + reserveBits );
	bool bModified =	format.m_numBitsTotal != r_format.m_numBitsTotal 
					||	format.m_numBitsX != r_format.m_numBitsX
					||	format.m_numBitsY != r_format.m_numBitsY
					||	format.m_numBitsZ != r_format.m_numBitsZ;
	r_format = format;
	return bModified;
}

bool NormalizeFloatBitFormat(Vec3BitPackingFormat& r_format)
{
	r_format.m_numBitsY = r_format.m_numBitsZ = 0;
	if (r_format.m_numBitsX > 32) {
		r_format.m_numBitsTotal = r_format.m_numBitsX = 32;
		return true;
	}
	r_format.m_numBitsTotal = r_format.m_numBitsX;
	return false;
}

float ComputeErrorFromBitFormat_Vec3_Range(ITGEOM::Vec3 const& vMin, ITGEOM::Vec3 const& vMax, Vec3BitPackingFormat format, std::vector<U64> const& aContextData)
{
	ITGEOM::Vec3 vBias, vScale;
	GetVec3Range(aContextData, vScale, vBias);
	ITGEOM::Vec3 vError(0.0f, 0.0f, 0.0f);
	if (format.m_numBitsX > 0) {
		float fUpper = vBias.x + vScale.x * (ldexpf(1.0f, format.m_numBitsX) - 1.0f);
		vError.x = max( vScale.x*0.5f, max( vMax.x - fUpper, vBias.x - vMin.x ) );
	} else
		vError.x = max( fabsf(vMax.x - vBias.x), fabsf(vMin.x - vBias.x) );
	if (format.m_numBitsY > 0) {
		float fUpper = vBias.y + vScale.y * (ldexpf(1.0f, format.m_numBitsY) - 1.0f);
		vError.y = max( vScale.y*0.5f, max( vMax.y - fUpper, vBias.y - vMin.y ) );
	} else
		vError.y = max( fabsf(vMax.y - vBias.y), fabsf(vMin.y - vBias.y) );
	if (format.m_numBitsZ > 0) {
		float fUpper = vBias.z + vScale.z * (ldexpf(1.0f, format.m_numBitsZ) - 1.0f);
		vError.z = max( vScale.z*0.5f, max( vMax.z - fUpper, vBias.z - vMin.z ) );
	} else
		vError.z = max( fabsf(vMax.z - vBias.z), fabsf(vMin.z - vBias.z) );
	float fError = sqrtf( Dot3(vError, vError) );
	return fError;
}
float ComputeErrorFromBitFormat_Vec3_Range(ITGEOM::Vec3Array const& avValues, Vec3BitPackingFormat format, std::vector<U64> const& aContextData)
{
	BDVOL::BoundBox bbox(avValues);
	return ComputeErrorFromBitFormat_Vec3_Range(bbox.MinPoint(), bbox.MaxPoint(), format, aContextData);
}

Vec3BitPackingFormat ComputeBitFormatFromRange_Vec3_Range(ITGEOM::Vec3 const& vMin, ITGEOM::Vec3 const& vMax, float fBitErrorTolerance, std::vector<U64>& aContextData, bool bNormalizeFormat = true, float *pfErrorEstimate = NULL)
{
	Vec3BitPackingFormat format( 0, 0, 0, 0 );
	CalculateVec3RangeAndFormat(vMin, vMax, fBitErrorTolerance, format, aContextData);

	U32 numBitsTotal = format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ;
	format.m_numBitsTotal = (U8)numBitsTotal;
	if (bNormalizeFormat && NormalizeBitFormat(format, 0))
		CalculateVec3Range(vMin, vMax, format, aContextData);
	if (pfErrorEstimate)
		*pfErrorEstimate = ComputeErrorFromBitFormat_Vec3_Range(vMin, vMax, format, aContextData);
	return format;
}
Vec3BitPackingFormat ComputeBitFormatFromValues_Vec3_Range(ITGEOM::Vec3Array const& avValues, float fBitErrorTolerance, std::vector<U64>& aContextData, bool bNormalizeFormat = true, float *pfErrorEstimate = NULL)
{
	Vec3BitPackingFormat format( 0, 0, 0, 0 );
	BDVOL::BoundBox bbox(avValues);
	CalculateVec3RangeAndFormat(bbox.MinPoint(), bbox.MaxPoint(), fBitErrorTolerance, format, aContextData);

	U32 numBitsTotal = format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ;
	format.m_numBitsTotal = (U8)numBitsTotal;
	if (bNormalizeFormat && NormalizeBitFormat(format, 0))
		CalculateVec3Range(bbox.MinPoint(), bbox.MaxPoint(), format, aContextData);
	if (pfErrorEstimate)
		*pfErrorEstimate = ComputeErrorFromBitFormat_Vec3_Range(bbox.MinPoint(), bbox.MaxPoint(), format, aContextData);
	return format;
}

unsigned GetLargestComponentIndex(ITGEOM::Quat const& q)
{
	unsigned uLargest = 3;
	float fLargestAbs = fabsf(q.m_v[3]);
	for (unsigned i = 0; i < 3; ++i) {
		float fAbs = fabsf(q.m_v[i]);
		if (fLargestAbs < fAbs)
			uLargest = i, fLargestAbs = fAbs;
	}
	return uLargest;
}
ITGEOM::Vec3 GetSmallest3Vector(ITGEOM::Quat const& q)
{
	unsigned uLargest = GetLargestComponentIndex(q);
	if (q.m_v[uLargest] < 0.0f)
		return ITGEOM::Vec3(-q.m_v[ uLargest == 0 ? 3 : 0 ],-q.m_v[ uLargest == 1 ? 3 : 1 ],-q.m_v[ uLargest == 2 ? 3 : 2 ] );
	else
		return ITGEOM::Vec3( q.m_v[ uLargest == 0 ? 3 : 0 ], q.m_v[ uLargest == 1 ? 3 : 1 ], q.m_v[ uLargest == 2 ? 3 : 2 ] );
}

Vec3BitPackingFormat ComputeBitFormatFromValues_Quat_SmallestThree(ITGEOM::QuatArray const& aqValues, float fErrorToleranceRadians, bool bNormalizeFormat = true, float* pfErrorEstimate = NULL)
{
	static const float kfSqrt_1by2 = 0.70710678f;				// sqrt(1/2)
	static const float kfSqrt_1by3 = 0.57735027f;				// sqrt(1/3)
	static const float kfErrorToleranceFP = 0.5f / 16777216.0f;	// 0.5f/2^24: range -sqrt(0.5f)..sqrt(0.5f) has max fp exponent -1 => fp resolution = 2^-24 over full range => max error = 0.5 / 2^24

	// trivial reject for no samples
	unsigned numSamples = (unsigned)aqValues.size();
	Vec3BitPackingFormat format( 1, 1, 1, 5 );
	if (numSamples == 0) {
		if (pfErrorEstimate) {
			*pfErrorEstimate = 0.0f;
		}
		return format;
	}

	// find min & max smallest3 quats (and min d - the largest component of min quat)
	ITGEOM::Vec3 vMin = GetSmallest3Vector(aqValues[0]);
	ITGEOM::Vec3 vMax = vMin;
	float fDSqrMin = 1.0f - vMin.x*vMin.x - vMin.y*vMin.y - vMin.z*vMin.z;
	for (unsigned iSample = 1; iSample < numSamples; ++iSample) {
		ITGEOM::Vec3 vSm3 = GetSmallest3Vector(aqValues[iSample]);
		float fDSqr = 1.0f - vSm3.x*vSm3.x - vSm3.y*vSm3.y - vSm3.z*vSm3.z;
		if (fDSqrMin > fDSqr) fDSqrMin = fDSqr;
		if (vMin.x > vSm3.x) vMin.x = vSm3.x; else if (vMax.x < vSm3.x) vMax.x = vSm3.x;
		if (vMin.y > vSm3.y) vMin.y = vSm3.y; else if (vMax.y < vSm3.y) vMax.y = vSm3.y;
		if (vMin.z > vSm3.z) vMin.z = vSm3.z; else if (vMax.z < vSm3.z) vMax.z = vSm3.z;
	}
	// worst case fDSqrMin = 0.25 at (0.5,0.5,0.5,0.5);
	float fDMin = sqrtf( fDSqrMin );

	float fErrorTolerancePerChannelAtZero = fErrorToleranceRadians * 0.5f * kfSqrt_1by3;	
	float fErrorTolerancePerChannel = fErrorTolerancePerChannelAtZero * fDMin;
	// no point in error tolerance per channel to below what floats can represent over the full range -sqrt(0.5)..sqrt(0.5)
	if (fErrorTolerancePerChannel < kfErrorToleranceFP)
		fErrorTolerancePerChannel = kfErrorToleranceFP;
	if (fErrorTolerancePerChannelAtZero < kfErrorToleranceFP)
		fErrorTolerancePerChannelAtZero = kfErrorToleranceFP;

	U32 minRangePerChannel = (fErrorTolerancePerChannel * 0xFFFFFFFF > kfSqrt_1by2) ? (U32)(kfSqrt_1by2 / fErrorTolerancePerChannel + 0.5f) : 0xFFFFFFFF;
	U8 minBitsPerChannel = (minRangePerChannel > 0) ? (U8)GetNumberOfBits(minRangePerChannel) : 1;

	float fErrorPerChannel = kfSqrt_1by2 / (2.0f * (float)(1u << (minBitsPerChannel-1) ));
	ITGEOM::Vec3 vError(0.0f, 0.0f, 0.0f);
	if ((vMin.x < -fErrorTolerancePerChannelAtZero) || (vMax.x > fErrorTolerancePerChannelAtZero)) {
		format.m_numBitsX = minBitsPerChannel;
		vError.x = fErrorPerChannel;
	} else {
		vError.x = max( fabsf(vMax.x), fabsf(vMin.x) );
	}
	if ((vMin.y < -fErrorTolerancePerChannelAtZero) || (vMax.y > fErrorTolerancePerChannelAtZero)) {
		format.m_numBitsY = minBitsPerChannel;
		vError.y = fErrorPerChannel;
	} else {
		vError.y = max( fabsf(vMax.y), fabsf(vMin.y) );
	}
	if ((vMin.z < -fErrorTolerancePerChannelAtZero) || (vMax.z > fErrorTolerancePerChannelAtZero)) {
		format.m_numBitsZ = minBitsPerChannel;
		vError.z = fErrorPerChannel;
	} else {
		vError.z = max( fabsf(vMax.z), fabsf(vMin.z) );
	}
	format.m_numBitsTotal = format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ + 2;
	if (!bNormalizeFormat) {
		if (pfErrorEstimate) {
			*pfErrorEstimate = sqrtf( Dot3(vError, vError) );
		}
		return format;
	}
	if (format.m_numBitsTotal > 64)
	{
		if (format.m_numBitsX == 1)	//only possible if numBitsY == numBitsZ > 30
			format.m_numBitsY = 31, format.m_numBitsZ = 30;
		else if (format.m_numBitsY == 1)	//only possible if numBitsX == numBitsZ > 30
			format.m_numBitsX = 31, format.m_numBitsZ = 30;
		else if (format.m_numBitsZ == 1)	//only possible if numBitsX == numBitsY > 30
			format.m_numBitsX = 28, format.m_numBitsY = 28;	//NOTE: 28 is largest Nx==Ny that satisfies (Nx&7)+Ny<32
		else	//only possible if numBitsX == numBitsY == numBitsY > 20
			format.m_numBitsX = 21, format.m_numBitsY = 21, format.m_numBitsZ = 20;
	} else {
		if ((format.m_numBitsX & 0x7) + format.m_numBitsY > 32) {
			format.m_numBitsY = 32 - (format.m_numBitsX & 0x7);
			if (format.m_numBitsZ > 30)
				format.m_numBitsZ = 30;
		} else if (((format.m_numBitsX + format.m_numBitsY) & 0x7) + format.m_numBitsZ > 30) {
			format.m_numBitsZ = 30 - ((format.m_numBitsX + format.m_numBitsY) & 0x7);
		} else {
			// bit format is ok, just return calculated error
			if (pfErrorEstimate) {
				*pfErrorEstimate = sqrtf( Dot3(vError, vError) );
			}
			return format;
		}
	}
	if (format.m_numBitsX != minBitsPerChannel && ((vMin.x < -fErrorTolerancePerChannelAtZero) || (vMax.x > fErrorTolerancePerChannelAtZero)))
		vError.x = kfSqrt_1by2 / (2.0f * (float)(1u << (format.m_numBitsX-1) ));
	if (format.m_numBitsY != minBitsPerChannel && ((vMin.y < -fErrorTolerancePerChannelAtZero) || (vMax.y > fErrorTolerancePerChannelAtZero)))
		vError.y = kfSqrt_1by2 / (2.0f * (float)(1u << (format.m_numBitsY-1) ));
	if (format.m_numBitsZ != minBitsPerChannel && ((vMin.z < -fErrorTolerancePerChannelAtZero) || (vMax.z > fErrorTolerancePerChannelAtZero)))
		vError.z = kfSqrt_1by2 / (2.0f * (float)(1u << (format.m_numBitsZ-1) ));
	format.m_numBitsTotal = format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ + 2;
	if (pfErrorEstimate) {
		*pfErrorEstimate = sqrtf( Dot3(vError, vError) );
	}
	return format;
}

Vec3BitPackingFormat ComputeBitFormatFromValues_Quat_Log(ITGEOM::QuatArray const& aqValues, float fBitErrorTolerance, std::vector<U64>& contextData, bool bNormalizeFormat = true, float* pfErrorEstimate = NULL)
{
	static const float kfPiSqrBy4 = 2.4674011f;
	CalculateQuatLogOrientation( aqValues, contextData );
	ITGEOM::Quat qMean;
	GetQuatLogOrientation( contextData, qMean );
	ITGEOM::Quat qMeanConjugate = qMean.Conjugate();
	BDVOL::BoundBox bbox;
	bbox.Empty();
	size_t numQuats = aqValues.size();

	float fMaxErrorRuntimeExp = 0.0f;
	for (size_t i = 0; i < numQuats; ++i) {
		ITGEOM::Quat q = qMeanConjugate * aqValues[i];
		ITGEOM::Vec3 vLog = QuatLog( q );
		bbox.Add( vLog );
		float fThetaSqr = (vLog.x*vLog.x + vLog.y*vLog.y + vLog.z*vLog.z);
		float fErrorRuntimeExpEst = fabsf(fThetaSqr*fThetaSqr*(fThetaSqr - 1.253f*1.254f)*(kfPiSqrBy4 - fThetaSqr)*0.000024f);
		if (fMaxErrorRuntimeExp < fErrorRuntimeExpEst)
			fMaxErrorRuntimeExp = fErrorRuntimeExpEst;
	}
	// Runtime QuatExp evaluation will reduce our accuracy anyway.  
	// No point in compressing our log values with much more accuracy than this limiting factor:
	float fQuatErrorTolerance = fBitErrorTolerance;
	if (fQuatErrorTolerance < fMaxErrorRuntimeExp)
		fQuatErrorTolerance = fMaxErrorRuntimeExp;

	// As the log of a quaternion has magnitude (angle from qMean)/2, our log must have half the error of our quaternion
	float fLogErrorTolerance = fQuatErrorTolerance * 0.5f;
	Vec3BitPackingFormat format = ComputeBitFormatFromRange_Vec3_Range( bbox.MinPoint(), bbox.MaxPoint(), fLogErrorTolerance, contextData, bNormalizeFormat, pfErrorEstimate );
	if (pfErrorEstimate)
		*pfErrorEstimate *= 2.0f;
	return format;
}

#define TEST_SPLINE_COMPRESSION 0
#if TEST_SPLINE_COMPRESSION

struct SplineFittingData {
	unsigned m_frameLast;
	float m_fError;
	float m_fSlopeFirstOut;
	float m_fSlopeLastIn;
};

static inline ITGEOM::Vec3 GetZeroErrorPoint( ITGEOM::Vec3 const& vE0, ITGEOM::Vec3 const& vE1 )
{
	// We want to find the point S at which Dot3( S, E0 ) == Dot3( S, E1 ) == 0.0f.
	// We can express this as a 2x2 matrix equation:
	//   [ E0.x, E0.y ] * [ S.x ] = [ -E0.z ]
	//   [ E1.x, E1.y ]   [ S.y ]   [ -E1.z ]
	// and solve it by inverting the 2x2 matrix.
	// Due to the nature of E(t), we don't worry about invertibility.
	float fDetInv = 1.0f / (vE0.x*vE1.y - vE1.x*vE0.y);
    //( vE1.y,-vE0.y)*fDetInv dot (-vE0.z,-vE1.z);
    //(-vE1.x, vE0.x)*fDetInv dot (-vE0.z,-vE1.z);
	return ITGEOM::Vec3( (vE0.y*vE1.z - vE1.y*vE0.z)*fDetInv, (vE0.z*vE1.x - vE1.z*vE0.x)*fDetInv, 1.0f );
}

static ITGEOM::Vec3 GetEqualErrorPoint( ITGEOM::Vec3 const& vE0, ITGEOM::Vec3 const& vE1, ITGEOM::Vec3 const& vE2 )
{
	// We want to find the point which equalizes the errors calculated from Dot3( vE0, vS ),
	// Dot3( vE1, vS ), and Dot3( vE2, vS ) with vEqualErrorPoint.z = 1.0f;
	// Flip any error vectors which point outward from the volume enclosed by the three vectors,
	// and construct and invert the error matrix to find vEqualErrorPoint:
	ITGEOM::Vec3 vZero01 = GetZeroErrorPoint(vE0, vE1);
	ITGEOM::Vec3 vZero12 = GetZeroErrorPoint(vE1, vE2);
	ITGEOM::Vec3 vZero20 = GetZeroErrorPoint(vE2, vE0);
	ITGEOM::Matrix3x3 mError(
		((ITGEOM::Dot3(vZero12, vE0) < 0.0f) ? -vE0 : vE0),
		((ITGEOM::Dot3(vZero20, vE1) < 0.0f) ? -vE1 : vE1),
		((ITGEOM::Dot3(vZero01, vE2) < 0.0f) ? -vE2 : vE2) );
	ITGEOM::Matrix3x3 mErrorInv;
	if (!ITGEOM::Inverse(&mErrorInv, mError, 0.00000001f)) {
		// if matrix is not invertible, it means that vE0, vE1, vE2 intersect in a point, so we can return any vZero*
		// for safety, we return the barycenter of what should be an infinitely small triangle
		return (vZero01 + vZero12 + vZero20) * (1.0f/3.0f);
	}
	ITGEOM::Vec3 vEqualErrorPoint = mErrorInv * ITGEOM::Vec3(1.0f, 1.0f, 1.0f);
	ITASSERT(fabsf(vEqualErrorPoint.z) > 0.000001f);	// matrix should never result in 0.0f here
	vEqualErrorPoint /= vEqualErrorPoint.z;
	return vEqualErrorPoint;
}

void TestFitSpline( SplineFittingData& splineFit, ITGEOM::Vec3Array const& avValues, unsigned iC, unsigned frameFirst, unsigned frameLast )
{
	static const float kf1by3 = 1.0f / 3.0f, kf1by6 = 1.0f / 6.0f;
	unsigned numFramesToFit = frameLast - frameFirst + 1;
	splineFit.m_frameLast = frameLast;
	float fYFirst = avValues[frameFirst][iC], fYLast = avValues[frameLast][iC];
	float fDeltaY = fYLast - fYFirst;
	if (numFramesToFit == 4) {
		// 0 error fit for 4 points:
		float fY1 = avValues[frameFirst+1][iC], fY2 = avValues[frameFirst+2][iC];
		splineFit.m_fError = 0.0f;
		splineFit.m_fSlopeFirstOut = (fYLast * 2.0f - fY2 * 9.0f + fY1 * 18.0f - fYFirst * 11.0f) * kf1by6;
		splineFit.m_fSlopeLastIn = (fYLast * 11.0f - fY2 * 18.0f + fY1 * 9.0f - fYFirst * 2.0f) * kf1by6;
		return;
	} else if (numFramesToFit < 4) {
		ITASSERT(numFramesToFit >= 4);
		return;
	}
	// for 5 or more points, we must iteratively solve for minimum largest error
	unsigned numIntervals = frameLast - frameFirst;
	float fNumIntervalsInv = 1.0f / (float)numIntervals;
	// Collect linearly interpolated y values and error vectors:
	// If we define  t = (frame-frameFirst)/(frameLast-frameFirst), N = frameLast-frameFirst,
	// the cubic equation that has initial value y0 and slope m0 and final value yN and slope mN is:
	//   y(t) = y0 + m0*N*t - ((2*m0 + mN)*N - 3*(yN-y0))*t^2 + ((m0 + mN)*N - 2*(yN-y0))*t^3
	//   y(t) = y0 + (yN-y0)*t + (m0*N - (yN-y0))*t*(1-t)^2 - (mN*N - (yN-y0))*t^2*(1 - t);
	// If we define   S.x = m0*N - (yN-y0), S.y = -(mN*N - (yN-y0)), E(t).x = t*(1-t)^2, E(t).y = t^2*(1-t) we have:
	//   y(i/N) = y0 + (yN-y0)*t + Dot2( S, E(i/N) )
	// To calculate the error between this value and the true value y[i], we define S.z = 1, E(i/n).z = (y0 + (yN-y0)*(i/n) - y[i]):
	//   E[i] = abs( Dot3( S, E(i/N) ) );
	// We can then find the point (S.x, S.y, 1) by finding the equal error point of every set of
	// 3 error vectors and checking if all other vectors have smaller errors.
	// We can probably refine this brute force approach somewhat if needed, but it involves at most
	// 31 Choose 3 == 4495 iterations for our data sets, which is manageable.
	std::vector<float> afYLinear;
	ITGEOM::Vec3Array avError;
	avError.push_back( ITGEOM::Vec3(0.0f, 0.0f, 0.0f) );	// placeholder
	afYLinear.push_back(fYFirst);
	for (unsigned i = 1; i < numIntervals; ++i) {
		float fT = (float)i * fNumIntervalsInv, fTComp = 1.0f - fT, fTTComp = fT*fTComp;
		float fYLinear = fYFirst + fDeltaY * fT, fY = avValues[frameFirst + i][iC];
		avError.push_back( ITGEOM::Vec3(fTTComp*fTComp, fTTComp*fT, fYLinear - fY ) );
		afYLinear.push_back( fYLinear );
	}
	afYLinear.push_back(fYLast);

	float fErrorMax_NearBest = FLT_MAX;
	for (unsigned iE0 = 1; iE0 < numIntervals; ++iE0) {
		ITGEOM::Vec3 const& vE0 = avError[ iE0 ];
		for (unsigned iE1 = iE0+1; iE1 < numIntervals; ++iE1) {
			ITGEOM::Vec3 const& vE1 = avError[ iE1 ];
			for (unsigned iE2 = iE1+1; iE2 < numIntervals; ++iE2) {
				ITGEOM::Vec3 const& vE2 = avError[ iE2 ];
				ITGEOM::Vec3 vS = GetEqualErrorPoint(vE0, vE1, vE2);
				float fError0 = fabsf( ITGEOM::Dot3( vS, vE0 ) );
				float fError1 = fabsf( ITGEOM::Dot3( vS, vE1 ) );
				float fError2 = fabsf( ITGEOM::Dot3( vS, vE2 ) );
				float fErrorMax_Best = max( max( fError0, fError1 ), fError2 );
				float fErrorMax_Limit = fErrorMax_Best * 1.0001f;
				if (fErrorMax_Limit > fErrorMax_NearBest)
					fErrorMax_Limit = fErrorMax_NearBest;
				float fErrorMax = fErrorMax_Best;
				unsigned maskE = (1<<iE0)|(1<<iE1)|(1<<iE2);
				bool bFoundBest = true, bFoundNearBest = true;
				for (unsigned iE = 1; iE < numIntervals && bFoundNearBest; ++iE) {
					if (maskE & (1<<iE))
						continue;
					float fError = fabsf( ITGEOM::Dot3( vS, avError[iE] ) );
					if (fError > fErrorMax_Best) {
						bFoundBest = false;
						if (fError > fErrorMax_Limit)
							bFoundNearBest = false;
						else
							fErrorMax = fError;
					}
				}
				if (bFoundBest || (bFoundNearBest && fErrorMax < fErrorMax_NearBest)) {
					// Now we have S.x = m0*N - (yN-y0), S.y = -(mN*N - (yN-y0)), we can calculate m0, mN
					splineFit.m_fSlopeFirstOut = (fDeltaY + vS.x) * fNumIntervalsInv;
					splineFit.m_fSlopeLastIn = (fDeltaY - vS.y) * fNumIntervalsInv;
					splineFit.m_fError = fErrorMax;
					if (bFoundBest)
						return;
					fErrorMax_NearBest = fErrorMax;
				}
			}
		}
	}
	ITASSERT(fErrorMax_NearBest != FLT_MAX);
}

unsigned TestVec3SplineCompression( ITGEOM::Vec3Array const& avValues, float fErrorTolerance )
{
	static const float kfSqrt3Inv = 0.577350269f;
	static const float kfSplineFittingErrorRatio = 0.5f, kfBitCompressionErrorRatio = 1.0f - kfSplineFittingErrorRatio;
	unsigned numBitsKeyDataSpline = 0, numBitsOverheadSpline = 0;
	// loop over components, compressing each independently
	unsigned const kNumFramesInBlockMax = 33;
	unsigned const numFrames = (unsigned)avValues.size();
	unsigned const numFormatBitsRange = GetFormatDataSize( kAcctVec3Range );
	unsigned const numBlocks = ((numFrames + kNumFramesInBlockMax-1)/kNumFramesInBlockMax);
	unsigned numFramesInBlockToSplit = ((numFrames + numBlocks-1) / numBlocks);
	if (numFramesInBlockToSplit < kNumFramesInBlockMax-3)
		numFramesInBlockToSplit = kNumFramesInBlockMax-3;

	float const fErrorToleranceC = fErrorTolerance * kfSqrt3Inv;
	for (unsigned iC = 0; iC < 3; ++iC) {
		// Starting from the first frame, try to fit splines to successively larger
		// intervals until the resulting errors diverge;  select the longest interval
		// which can match fErrorTolerance, then restart from the end of the selected interval.
		ITGEOM::Vec3Array avSplineKeys;
		std::vector<unsigned> aSplineKeyFrames;

		unsigned iBlock;
		unsigned frameLastInBlock = 0;
		unsigned maxSplineLength = 0;
		for (iBlock = 0; frameLastInBlock+1 < numFrames; ++iBlock) {
			unsigned frameFirst = frameLastInBlock;
			unsigned numFramesInBlock = (frameFirst + kNumFramesInBlockMax >= numFrames) ? numFrames - frameFirst : kNumFramesInBlockMax;
			unsigned frameLastInBlockMin = (frameFirst + numFramesInBlockToSplit >= numFrames) ? (numFrames-1) : frameFirst + numFramesInBlockToSplit;
			unsigned keyFirst = (unsigned)avSplineKeys.size();
			frameLastInBlock = frameFirst + numFramesInBlock-1;
			avSplineKeys.push_back( ITGEOM::Vec3(avValues[frameFirst][iC], 0.0f, 0.0f) );
			aSplineKeyFrames.push_back( frameFirst );

			while (frameFirst+3 <= frameLastInBlock) {
				unsigned frameLast, counterOutOfTolerance = 0;
				SplineFittingData splineFit_Best;
				// Spline compression can always fit 4 or fewer points perfectly, so we start with (iFrame0..iFrame0+3)
				TestFitSpline( splineFit_Best, avValues, iC, frameFirst, frameFirst+3 );
				for (frameLast = frameFirst+4; frameLast <= frameLastInBlock; ++frameLast) {
					SplineFittingData splineFit;
					TestFitSpline( splineFit, avValues, iC, frameFirst, frameLast );
					if (splineFit.m_fError > fErrorToleranceC*kfSplineFittingErrorRatio) {
						if (splineFit.m_fError > fErrorToleranceC*kfSplineFittingErrorRatio*2.0f && ++counterOutOfTolerance > 3)
							break;
						continue;
					}
					//TODO: should we have a "best" criterion that considers fitting error as well as length?
					splineFit_Best = splineFit;
				}
				frameLast = splineFit_Best.m_frameLast;
				if (maxSplineLength < frameLast - frameFirst)
					maxSplineLength = frameLast - frameFirst;
				avSplineKeys.back().z = splineFit_Best.m_fSlopeFirstOut - avSplineKeys.back().y;
				avSplineKeys.push_back( ITGEOM::Vec3(avValues[frameLast][iC], splineFit_Best.m_fSlopeLastIn, 0.0f) );
				aSplineKeyFrames.push_back( frameLast );
				// continue from end of selected spline
				frameFirst = splineFit_Best.m_frameLast;
				if (frameFirst >= frameLastInBlockMin) {
					frameLastInBlock = frameFirst;
					numFramesInBlock = frameLastInBlock - aSplineKeyFrames[keyFirst];
					break;
				}
			}
			// final spline needs special handling if it has fewer than 4 frames:
			if (frameFirst+2 == frameLastInBlock) {
				// fit a cubic that smoothly continues the last segment and passes through the last two points
				float fSlopeLastIn = avSplineKeys.back().y + (2.0f*avValues[frameLastInBlock][iC] - 4.0f*avValues[frameLastInBlock-1][iC] + 2.0f*avValues[frameLastInBlock-2][iC]);
				avSplineKeys.push_back( ITGEOM::Vec3(avValues[frameLastInBlock][iC], fSlopeLastIn, 0.0f) );
				aSplineKeyFrames.push_back( frameLastInBlock );
			} else if (frameFirst+1 == frameLastInBlock) {
				// fit a quadratic that smoothly continues the previous segment and passes through the last point
				float fSlopeLastIn = 2.0f * (avValues[frameLastInBlock][iC] - avValues[frameLastInBlock-1][iC]) - (avSplineKeys.back().y + avSplineKeys.back().z);
				avSplineKeys.push_back( ITGEOM::Vec3(avValues[frameLastInBlock][iC], fSlopeLastIn, 0.0f) );
				aSplineKeyFrames.push_back( frameLastInBlock );
			} // last spline ended on last frame; ok...
			// Convert first frame to have fSlopeIn = fSlopeOut
			avSplineKeys[keyFirst].y = avSplineKeys[keyFirst].z;
			avSplineKeys[keyFirst].z = 0.0f;
			numBitsOverheadSpline += (numFramesInBlock + 6) &~ 0x7;	//= numKeyBits
		}
		ITASSERT(iBlock == numBlocks);

		// calculate error tolerance requirement on fMFirstOut, fMLastIn for this key:
		float fSlopeErrorToleranceMin;
		{
			unsigned i_SlopeErrorToleranceMin = (maxSplineLength + 1)/2;
			float fT_SlopeErrorToleranceMin = (float)i_SlopeErrorToleranceMin/(float)maxSplineLength;
			fSlopeErrorToleranceMin = fErrorToleranceC * kfBitCompressionErrorRatio / (i_SlopeErrorToleranceMin * fT_SlopeErrorToleranceMin * (1.0f - fT_SlopeErrorToleranceMin));
		}
		// Calculate number of bits needed to represent these spline keys
		unsigned numKeys = (unsigned)avSplineKeys.size();
		ITGEOM::Vec3 vErrorTolerance( fErrorToleranceC, fSlopeErrorToleranceMin, fSlopeErrorToleranceMin );
		U64_Array aContextData;
		Vec3BitPackingFormat bitFormat = ComputeBitFormatFromValues_Vec3_Range(avSplineKeys, vErrorTolerance, aContextData, true);
		numBitsKeyDataSpline += bitFormat.m_numBitsTotal * numKeys;
		numBitsOverheadSpline += numFormatBitsRange;

		{	// Now compress and decompress and check that we actually satisfied our error tolerance for all frames:
			IBitCompressedVec3Array *pCompressedData = BitCompressVec3Array(avSplineKeys, kAcctVec3Range, bitFormat, &aContextData);
			ITGEOM::Vec3Array avDecompressed;
			pCompressedData->GetDecompressedSamples(avDecompressed);
			for (unsigned iKey0 = 0, iKey1 = 1; iKey1 < numKeys; ++iKey0, ++iKey1) {
				unsigned frameFirst = aSplineKeyFrames[iKey0], frameLast = aSplineKeyFrames[iKey1];
				if (frameFirst == frameLast)
					continue;
				float fNumIntervals = (float)(frameLast - frameFirst), fNumIntervalsInv = 1.0f/fNumIntervals;
				float fY0 = avDecompressed[iKey0].x, fY1 = avDecompressed[iKey1].x, fM0 = avDecompressed[iKey0].y + avDecompressed[iKey0].z, fM1 = avDecompressed[iKey1].y;
				float fA = ((fM1 + fM0)*fNumIntervals - 2.0f*(fY1 - fY0)), fB = (3.0f*(fY1 - fY0) - (fM1 + 2.0f*fM0)*fNumIntervals), fC = fM0 * fNumIntervals, fD = fY0;
				float fY0s = avSplineKeys[iKey0].x, fY1s = avSplineKeys[iKey1].x, fM0s = avSplineKeys[iKey0].y + avSplineKeys[iKey0].z, fM1s = avSplineKeys[iKey1].y;
				float fAs = ((fM1s + fM0s)*fNumIntervals - 2.0f*(fY1s - fY0s)), fBs = (3.0f*(fY1s - fY0s) - (fM1s + 2.0f*fM0s)*fNumIntervals), fCs = fM0s * fNumIntervals, fDs = fY0s;
				for (unsigned i = 0, numIntervals = frameLast-frameFirst; i < numIntervals; ++i) {
					float fT = (float)i * fNumIntervalsInv;
					float fY_uncompressed = avValues[ frameFirst + i ][ iC ];
					float fY_splinefit = ((fAs*fT + fBs)*fT + fCs)*fT + fDs;
					float fY_decompressed = ((fA*fT + fB)*fT + fC)*fT + fD;
					float fError_SplineFit = fabsf( fY_splinefit - fY_uncompressed );
					float fError = fabsf( fY_decompressed - fY_uncompressed );
					if (fError > fErrorToleranceC) {
						int zzz; zzz = 0;
					}
				}
			}
			delete pCompressedData;
		}
	}
	// Add in approximate additional overhead from unshared block headers:
	return numBitsKeyDataSpline + numBitsOverheadSpline;
}
#endif

Vec3BitPackingFormat ComputeBitFormatFromValues_Quat_LogPca(ITGEOM::QuatArray const& aqValues, float fBitErrorTolerance, std::vector<U64>& contextData, bool bNormalizeFormat = true, float *pfErrorEstimate = NULL)
{
	static const float kfPiSqrBy4 = 2.4674011f;
	CalculateQuatLogPcaOrientation( aqValues, contextData );
	ITGEOM::Quat qPre, qPost;
	GetQuatLogPcaOrientation( contextData, qPre, qPost );
	ITGEOM::Quat qPreConjugate = qPre.Conjugate();
	ITGEOM::Quat qPostConjugate = qPost.Conjugate();
	BDVOL::BoundBox bbox;
	bbox.Empty();
	size_t numQuats = aqValues.size();
#if TEST_SPLINE_COMPRESSION
	ITGEOM::Vec3Array avLogValues;
#endif
	float fMaxErrorRuntimeExp = 0.0f;
	for (size_t i = 0; i < numQuats; ++i) {
		ITGEOM::Quat q = qPreConjugate * aqValues[i] * qPostConjugate;
		ITGEOM::Vec3 vLog = QuatLog( q );
		bbox.Add( vLog );
#if TEST_SPLINE_COMPRESSION
		avLogValues.push_back(vLog);
#endif
		float fThetaSqr = (vLog.x*vLog.x + vLog.y*vLog.y + vLog.z*vLog.z);
		float fErrorRuntimeExpEst = fabsf(fThetaSqr*fThetaSqr*(fThetaSqr - 1.253f*1.254f)*(kfPiSqrBy4 - fThetaSqr)*0.000024f);
		if (fMaxErrorRuntimeExp < fErrorRuntimeExpEst)
			fMaxErrorRuntimeExp = fErrorRuntimeExpEst;
	}
	// Runtime QuatExp evaluation will reduce our accuracy anyway.  
	// No point in compressing our log values with much more accuracy than this limiting factor:
	float fQuatErrorTolerance = fBitErrorTolerance;
	if (fQuatErrorTolerance < fMaxErrorRuntimeExp)
		fQuatErrorTolerance = fMaxErrorRuntimeExp;

	// As the log of a quaternion has magnitude (angle from qMean)/2, our log must have half the error of our quaternion
	float fLogErrorTolerance = fQuatErrorTolerance * 0.5f;
#if TEST_SPLINE_COMPRESSION
	unsigned const kNumFramesInBlockMax = 33;
	unsigned numBitsTotalSpline = TestVec3SplineCompression( avLogValues, fLogErrorTolerance );
	numBitsTotalSpline += 128;	// add in overhead of qPre, qPost
	numBitsTotalSpline += 64 * ((unsigned)avLogValues.size() + kNumFramesInBlockMax-1)/kNumFramesInBlockMax;	// add in additional block overhead for unshared blocks
	Vec3BitPackingFormat format = ComputeBitFormatFromRange_Vec3_Range( bbox.MinPoint(), bbox.MaxPoint(), fLogErrorTolerance, contextData, bNormalizeFormat, pfErrorEstimate );
	unsigned numBitsTotal = 128 /*qPre, qPost*/ + format.m_numBitsTotal * (unsigned)numQuats;
	(void)numBitsTotal;
#else
	Vec3BitPackingFormat format = ComputeBitFormatFromRange_Vec3_Range( bbox.MinPoint(), bbox.MaxPoint(), fLogErrorTolerance, contextData, bNormalizeFormat, pfErrorEstimate );
#endif
	if (pfErrorEstimate)
		*pfErrorEstimate *= 2.0f;
	return format;
}

void AnimClipCompression_Init( ClipCompressionDesc& compression, unsigned clipFlags, unsigned compressionFlags)
{
	compression.m_flags = (compressionFlags & kClipCompressionMask) | (clipFlags &~ kClipCompressionMask);
	compression.m_maxBlockSize = 0;
	compression.m_maxKeysPerBlock = 0;
	compression.m_sharedSkipFrames.clear();
	for (size_t i=0; i<kNumChannelTypes; i++) {
		compression.m_unsharedSkipFrames[i].resize(0);
		compression.m_format[i].resize(0);
	}
}

#define DEBUG_PRINT_ANIM_ERROR_TABLES 1

bool AnimClipCompression_GenerateBitFormats_Vector(
	ClipCompressionDesc& compression,							// [input/output] compression structure to fill out with calculated bit formats
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors,				// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	AnimChannelCompressionType compressionType,					// type of compression to apply (one of kAcctVec3*)
	std::set<size_t> const& targetAnimatedJoints_Scale,			// set of animated joint indices to compress scales
	std::set<size_t> const& targetAnimatedJoints_Translation,	// set of animated joint indices to compress translations
	bool bSharedBitFormat)										// if true, one bit format must be chosen to be shared by all joints being compressed
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;

	std::vector<ChannelCompressionDesc>& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc>& formatTrans = compression.m_format[kChannelTypeTranslation];

	// initialise compression format for vector channels
	const ChannelCompressionDesc kCompression_None = { kAcctVec3Uncompressed, 0, kVbpfNone };
	bool bInitScale = formatScale.empty(), bInitTrans = formatTrans.empty();
	if (bInitScale || bInitTrans) {
		if (bInitScale) {
			formatScale.resize(numAnimatedJoints, kCompression_None);
		}
		if (bInitTrans) {
			formatTrans.resize(numAnimatedJoints, kCompression_None);
		}
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex ++) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			if (bInitScale && !sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				formatScale[iAnimatedJoint].m_bitFormat = kVbpfVec3Uncompressed;
			}
			if (bInitTrans && !sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				formatTrans[iAnimatedJoint].m_bitFormat = kVbpfVec3Uncompressed;
			}
		}
	}
		
	Vec3BitPackingFormat bitFormat;
	switch (compressionType) {
	case kAcctVec3Uncompressed:
		bitFormat = kVbpfVec3Uncompressed;
		break;
	case kAcctVec3Float16:
		bitFormat = kVbpfVec3Float16;
		break;
	case kAcctVec3Range:
		bitFormat = kVbpfNone;
		break;
	case kAcctVec3Auto:
		bitFormat = kVbpfNone;
		break;
	default:
		IWARN("AnimClipCompression_GenerateBitFormats_Vector: vector compression type %u is not supported yet - defaulting to uncompressed!", compressionType);
		compressionType = kAcctVec3Uncompressed;
		bitFormat = kVbpfVec3Uncompressed;
		break;
	}

	unsigned const numBitsFormatRange = GetFormatDataSize(kAcctVec3Range) * 8;
	bool bWithinErrorTolerance_SharedFloat16 = true, bWithinErrorTolerance_SharedRange = true;
	Vec3BitPackingFormat bitFormat_SharedRange = kVbpfNone;
	unsigned numChannelsTotal = 0;

	// merge scale & translation joints into a single set
	std::set<size_t> targetAnimatedJoints = targetAnimatedJoints_Scale;
	targetAnimatedJoints.insert(targetAnimatedJoints_Translation.begin(), targetAnimatedJoints_Translation.end());

#if DEBUG_PRINT_ANIM_ERROR_TABLES
	if (ShouldNote(IMsg::kDebug)) 
	{
		for (auto it = targetAnimatedJoints.begin(); it != targetAnimatedJoints.end(); it++) {
			size_t iAnimatedJoint = *it;
			if (iAnimatedJoint >= numAnimatedJoints) {
				break;
			}
			unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
			if (iJointAnimIndex >= numJointAnims) {
				continue;
			}
			static const float kfLn2 = logf( 2.0f );
			static const float kfLn2Inv = 1.0f / kfLn2;
			if (targetAnimatedJoints_Scale.count(iAnimatedJoint) && !sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex))
			{
				ITGEOM::Vec3Array scaleSamples; 
				sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex)->GetSamples(scaleSamples);
				float const fErrorUncompressed = CalculateVec3UncompressedError( scaleSamples );
				float const fErrorFloat16 = CalculateVec3Float16Error( scaleSamples );
				float const fErrorUncompressedLog2 = kfLn2Inv * logf( fErrorUncompressed );
				float const fErrorFloat16Log2 = kfLn2Inv * logf( fErrorFloat16 );
				U32 const numSamples = (U32)scaleSamples.size();
				U32 const sizeUncompressed = numSamples*kVbpfVec3Uncompressed.m_numBitsTotal;
				U32 const sizeFloat16 = numSamples*kVbpfVec3Float16.m_numBitsTotal;

				INOTE(IMsg::kDebug, "Error Table for ajoint[%u].scale %u samples\n", iAnimatedJoint, numSamples );
				INOTE(IMsg::kDebug, "uncompressed log2E=%7.3f size=%6u\n", fErrorUncompressedLog2, sizeUncompressed);
				INOTE(IMsg::kDebug, "float16      log2E=%7.3f size=%6u\n", fErrorFloat16Log2, sizeFloat16);
				INOTE(IMsg::kDebug, "        range vs. ET:\n");
				INOTE(IMsg::kDebug, "log2ET  log2EE  log2E   N  Nx Ny Nz size  X HU size  format\n");
				for (U32 iET = 4*2; iET <= 25*2; iET++)
				{
					float fETLog2 = -(float)iET / (float)2;
					float fErrorTolerance = expf( kfLn2 * fETLog2 );

					float fErrorEstRange = 0.0f, fErrorRange = 0.0f;
					U64_Array aContextDataRange;
					Vec3BitPackingFormat bitFormatRange = ComputeBitFormatFromValues_Vec3_Range(scaleSamples, fErrorTolerance, aContextDataRange, true, &fErrorEstRange);
					IBitCompressedVec3Array* pCompressedRange = BitCompressVec3Array(scaleSamples, kAcctVec3Range, bitFormatRange, &aContextDataRange);
					fErrorRange = pCompressedRange->GetMaxError();
					float fErrorEstLog2Range = kfLn2Inv * logf( fErrorEstRange );
					float fErrorLog2Range = kfLn2Inv * logf( fErrorRange );
					U32 sizeRange = numBitsFormatRange + numSamples*bitFormatRange.m_numBitsTotal;

					AnimChannelCompressionType compressionType = kAcctVec3Uncompressed;
					U32 size = sizeUncompressed;
					if (fErrorFloat16 <= fErrorTolerance && sizeFloat16 < size)
						compressionType = kAcctVec3Float16,	size = sizeFloat16;
					if ((fErrorEstRange <= fErrorTolerance || fErrorEstRange <= fErrorUncompressed) && sizeRange < size)
						compressionType = kAcctVec3Range, size = sizeRange;

					INOTE(IMsg::kDebug, "%7.3f %7.3f %7.3f %2u %2u %2u %2u %6u%c %c%c %6u %s\n", fETLog2, 
						fErrorEstLog2Range, fErrorLog2Range, bitFormatRange.m_numBitsTotal, bitFormatRange.m_numBitsX, bitFormatRange.m_numBitsY, bitFormatRange.m_numBitsZ, sizeRange, (fErrorEstRange <= fErrorTolerance)?'.':'x', 
						(fErrorFloat16 <= fErrorTolerance)?'.':'x', (fErrorUncompressed <= fErrorTolerance)?'.':'x', 
						size, ((compressionType==kAcctVec3Uncompressed)?"uncompressed":(compressionType==kAcctVec3Float16)?"float16":"range"));
					delete pCompressedRange;
				}
			}
			if (targetAnimatedJoints_Translation.count(iAnimatedJoint) && !sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex))
			{
				ITGEOM::Vec3Array translationSamples; 
				sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex)->GetSamples(translationSamples);
				float const fErrorUncompressed = CalculateVec3UncompressedError( translationSamples );
				float const fErrorFloat16 = CalculateVec3Float16Error( translationSamples );
				float const fErrorUncompressedLog2 = kfLn2Inv * logf( fErrorUncompressed );
				float const fErrorFloat16Log2 = kfLn2Inv * logf( fErrorFloat16 );
				U32 const numSamples = (U32)translationSamples.size();
				U32 const sizeUncompressed = numSamples*kVbpfVec3Uncompressed.m_numBitsTotal;
				U32 const sizeFloat16 = numSamples*kVbpfVec3Float16.m_numBitsTotal;

				INOTE(IMsg::kDebug, "Error Table for ajoint[%u].trans %u samples\n", iAnimatedJoint, numSamples );
				INOTE(IMsg::kDebug, "uncompressed log2E=%7.3f size=%6u\n", fErrorUncompressedLog2, sizeUncompressed);
				INOTE(IMsg::kDebug, "float16      log2E=%7.3f size=%6u\n", fErrorFloat16Log2, sizeFloat16);
				INOTE(IMsg::kDebug, "        range vs. ET:\n");
				INOTE(IMsg::kDebug, "log2ET  log2EE  log2E   N  Nx Ny Nz size  X HU size   format\n");
				for (U32 iET = 4*2; iET <= 25*2; iET++)
				{
					float fETLog2 = -(float)iET / (float)2;
					float fErrorTolerance = expf( kfLn2 * fETLog2 );

					float fErrorEstRange = 0.0f, fErrorRange = 0.0f;
					U64_Array aContextDataRange;
					Vec3BitPackingFormat bitFormatRange = ComputeBitFormatFromValues_Vec3_Range(translationSamples, fErrorTolerance, aContextDataRange, true, &fErrorEstRange);
					IBitCompressedVec3Array* pCompressedRange = BitCompressVec3Array(translationSamples, kAcctVec3Range, bitFormatRange, &aContextDataRange);
					fErrorRange = pCompressedRange->GetMaxError();
					float fErrorEstLog2Range = kfLn2Inv * logf( fErrorEstRange );
					float fErrorLog2Range = kfLn2Inv * logf( fErrorRange );
					U32 sizeRange = numBitsFormatRange + numSamples*bitFormatRange.m_numBitsTotal;

					AnimChannelCompressionType compressionType = kAcctVec3Uncompressed;
					U32 size = sizeUncompressed;
					if (fErrorFloat16 <= fErrorTolerance && sizeFloat16 < size)
						compressionType = kAcctVec3Float16,	size = sizeFloat16;
					if ((fErrorEstRange <= fErrorTolerance || fErrorEstRange <= fErrorUncompressed) && sizeRange < size)
						compressionType = kAcctVec3Range, size = sizeRange;

					INOTE(IMsg::kDebug, "%7.3f %7.3f %7.3f %2u %2u %2u %2u %6u%c %c%c %6u %s\n", fETLog2, 
						fErrorEstLog2Range, fErrorLog2Range, bitFormatRange.m_numBitsTotal, bitFormatRange.m_numBitsX, bitFormatRange.m_numBitsY, bitFormatRange.m_numBitsZ, sizeRange, (fErrorEstRange <= fErrorTolerance)?'.':'x', 
						(fErrorFloat16 <= fErrorTolerance)?'.':'x', (fErrorUncompressed <= fErrorTolerance)?'.':'x', 
						size, ((compressionType==kAcctVec3Uncompressed)?"uncompressed":(compressionType==kAcctVec3Float16)?"float16":"range"));
					delete pCompressedRange;
				}
			}
		}
	}
#endif

	for (auto it = targetAnimatedJoints.begin(); it != targetAnimatedJoints.end(); it++) {
		size_t iAnimatedJoint = *it;
		if (iAnimatedJoint >= numAnimatedJoints) {
			break;
		}
		unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
		if (iJointAnimIndex >= numJointAnims) {
			IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u] is not animated; ignoring target...\n", iAnimatedJoint);
			continue;
		}
		if (targetAnimatedJoints_Scale.count(iAnimatedJoint)) {
			SampledAnim* scaleAnim = sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
			if (!scaleAnim) {
				IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].scale anim %u has no sampled anim; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
			} else if (sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].scale anim %u has constant value; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
			} else {
				if (formatScale[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined) {
					IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].scale anim %u already has a defined bit format; discarding old format...\n", iAnimatedJoint, iJointAnimIndex);
				}
				ITGEOM::Vec3Array scaleSamples; scaleAnim->GetSamples(scaleSamples);
				ITASSERT((unsigned)scaleSamples.size() == sourceData.m_numFrames);
				numChannelsTotal++;
				if (compressionType == kAcctVec3Auto) {
					// choose best encoding type between uncompressed, float16 or range
					float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeScale][iJointAnimIndex];
					float fErrorFloat16 = CalculateVec3Float16Error( scaleSamples );
					float fErrorRange = 0.0f;
					formatScale[iAnimatedJoint].m_contextData.resize(0);
					Vec3BitPackingFormat bitFormatRange = ComputeBitFormatFromValues_Vec3_Range(scaleSamples, fErrorTolerance, formatScale[iAnimatedJoint].m_contextData, !bSharedBitFormat, &fErrorRange);
					if (bSharedBitFormat) {
						bWithinErrorTolerance_SharedFloat16 = bWithinErrorTolerance_SharedFloat16 && (fErrorFloat16 <= fErrorTolerance);
						bWithinErrorTolerance_SharedRange = bWithinErrorTolerance_SharedRange && (fErrorRange <= fErrorTolerance);
						if (bitFormat_SharedRange.m_numBitsX < bitFormatRange.m_numBitsX) bitFormat_SharedRange.m_numBitsX = bitFormatRange.m_numBitsX;
						if (bitFormat_SharedRange.m_numBitsY < bitFormatRange.m_numBitsY) bitFormat_SharedRange.m_numBitsY = bitFormatRange.m_numBitsY;
						if (bitFormat_SharedRange.m_numBitsZ < bitFormatRange.m_numBitsZ) bitFormat_SharedRange.m_numBitsZ = bitFormatRange.m_numBitsZ;
						formatScale[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
					} else {
						AnimChannelCompressionType compressionTypeAuto = kAcctVec3Uncompressed;
						Vec3BitPackingFormat bitFormatAuto = kVbpfVec3Uncompressed;
						unsigned numBitsTotal = (unsigned)kVbpfVec3Uncompressed.m_numBitsTotal * numOutputFrames;
						if (fErrorFloat16 <= fErrorTolerance) {
							compressionTypeAuto = kAcctVec3Float16;
							bitFormatAuto = kVbpfVec3Float16;
							numBitsTotal = (unsigned)kVbpfVec3Float16.m_numBitsTotal * numOutputFrames;
						}
						unsigned numBitsTotalRange = bitFormatRange.m_numBitsTotal * numOutputFrames + numBitsFormatRange;
						if (fErrorRange <= fErrorTolerance && numBitsTotalRange < numBitsTotal) {
							compressionTypeAuto = kAcctVec3Range;
							bitFormatAuto = bitFormatRange;
							numBitsTotal = numBitsTotalRange;
						}
						formatScale[iAnimatedJoint].m_compressionType = compressionTypeAuto;
						formatScale[iAnimatedJoint].m_bitFormat = bitFormatAuto;
						formatScale[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
					}
				} else {
					// encoding type was specified, compute the bit format
					formatScale[iAnimatedJoint].m_compressionType = compressionType;
					formatScale[iAnimatedJoint].m_contextData.resize(0);
					if (!IsVariableBitPackedFormat(compressionType)) {	// float16
						if (compressionType == kAcctVec3Float16) {
							float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeScale][iJointAnimIndex];
							float fErrorFloat16 = CalculateVec3Float16Error( scaleSamples );
							if (fErrorFloat16 > fErrorTolerance) {
								IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].scale anim %u has float16 error %f > tolerance %f\n", iAnimatedJoint, iJointAnimIndex, fErrorFloat16, fErrorTolerance);
							}
						}
						formatScale[iAnimatedJoint].m_bitFormat = bitFormat;
					} else {
						// calculate number of bits per channel from range
						float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeScale][iJointAnimIndex];
						if (bSharedBitFormat) {
							Vec3BitPackingFormat thisBitFormat = ComputeBitFormatFromValues_Vec3_Range(scaleSamples, fErrorTolerance, formatScale[iAnimatedJoint].m_contextData, false);
							if (bitFormat.m_numBitsX < thisBitFormat.m_numBitsX) bitFormat.m_numBitsX = thisBitFormat.m_numBitsX;
							if (bitFormat.m_numBitsY < thisBitFormat.m_numBitsY) bitFormat.m_numBitsY = thisBitFormat.m_numBitsY;
							if (bitFormat.m_numBitsZ < thisBitFormat.m_numBitsZ) bitFormat.m_numBitsZ = thisBitFormat.m_numBitsZ;
						} else {
							formatScale[iAnimatedJoint].m_bitFormat = ComputeBitFormatFromValues_Vec3_Range(scaleSamples, fErrorTolerance, formatScale[iAnimatedJoint].m_contextData);
						}
					}
					formatScale[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
				}
			}
		}
		if (targetAnimatedJoints_Translation.count(iAnimatedJoint)) {
			SampledAnim* translationAnim = sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
			if (!translationAnim){
				IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].translation anim %u has no sampled anim; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
			} else if (sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].translation anim %u has constant value; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
			} else {
				if (formatTrans[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined) {
					IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].translation anim %u already has a defined bit format; discarding old format...\n", iAnimatedJoint, iJointAnimIndex);
				}
				ITGEOM::Vec3Array translationSamples;
				translationAnim->GetSamples(translationSamples);
				ITASSERT((unsigned)translationSamples.size() == sourceData.m_numFrames);
				numChannelsTotal++;
				if (compressionType == kAcctVec3Auto) {
					float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnimIndex];
					float fErrorFloat16 = CalculateVec3Float16Error( translationSamples );
					float fErrorRange = 0.0f;
					formatTrans[iAnimatedJoint].m_contextData.resize(0);
					Vec3BitPackingFormat bitFormatRange = ComputeBitFormatFromValues_Vec3_Range(translationSamples, fErrorTolerance, formatTrans[iAnimatedJoint].m_contextData, !bSharedBitFormat, &fErrorRange);
					if (bSharedBitFormat) {
						bWithinErrorTolerance_SharedFloat16 = bWithinErrorTolerance_SharedFloat16 && (fErrorFloat16 <= fErrorTolerance);
						bWithinErrorTolerance_SharedRange = bWithinErrorTolerance_SharedRange && (fErrorRange <= fErrorTolerance);
						if (bitFormat_SharedRange.m_numBitsX < bitFormatRange.m_numBitsX) bitFormat_SharedRange.m_numBitsX = bitFormatRange.m_numBitsX;
						if (bitFormat_SharedRange.m_numBitsY < bitFormatRange.m_numBitsY) bitFormat_SharedRange.m_numBitsY = bitFormatRange.m_numBitsY;
						if (bitFormat_SharedRange.m_numBitsZ < bitFormatRange.m_numBitsZ) bitFormat_SharedRange.m_numBitsZ = bitFormatRange.m_numBitsZ;
						formatTrans[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
					} else {
						AnimChannelCompressionType compressionTypeAuto = kAcctVec3Uncompressed;
						Vec3BitPackingFormat bitFormatAuto = kVbpfVec3Uncompressed;
						unsigned numBitsTotal = (unsigned)kVbpfVec3Uncompressed.m_numBitsTotal * numOutputFrames;
						if (fErrorFloat16 <= fErrorTolerance) {	// numBitsTotal for float16 is always less than for uncompressed
							compressionTypeAuto = kAcctVec3Float16;
							bitFormatAuto = kVbpfVec3Float16;
							numBitsTotal = (unsigned)kVbpfVec3Float16.m_numBitsTotal * numOutputFrames;
						}
						unsigned numBitsTotalRange = numBitsFormatRange + bitFormatRange.m_numBitsTotal * numOutputFrames;
						if (fErrorRange <= fErrorTolerance && numBitsTotalRange < numBitsTotal) {
							compressionTypeAuto = kAcctVec3Range;
							bitFormatAuto = bitFormatRange;
							numBitsTotal = numBitsTotalRange;
						}
						formatTrans[iAnimatedJoint].m_compressionType = compressionTypeAuto;
						formatTrans[iAnimatedJoint].m_bitFormat = bitFormatAuto;
						formatTrans[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
					}
				} else {
					formatTrans[iAnimatedJoint].m_compressionType = compressionType;
					formatTrans[iAnimatedJoint].m_contextData.resize(0);
					if (!IsVariableBitPackedFormat(compressionType)) {
						if (compressionType == kAcctVec3Float16) {
							float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnimIndex];
							float fErrorFloat16 = CalculateVec3Float16Error( translationSamples );
							if (fErrorFloat16 > fErrorTolerance) {
								IWARN("AnimClipCompression_GenerateBitFormats_Vector: target ajoint[%u].translation anim %u has float16 error %f > tolerance %f\n", iAnimatedJoint, iJointAnimIndex, fErrorFloat16, fErrorTolerance);
							}
						}
						formatTrans[iAnimatedJoint].m_bitFormat = bitFormat;
					} else {
						// calculate number of bits per channel from range
						float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnimIndex];
						if (bSharedBitFormat) {
							Vec3BitPackingFormat thisBitFormat = ComputeBitFormatFromValues_Vec3_Range(translationSamples, fErrorTolerance, formatTrans[iAnimatedJoint].m_contextData, false);
							if (bitFormat.m_numBitsX < thisBitFormat.m_numBitsX) bitFormat.m_numBitsX = thisBitFormat.m_numBitsX;
							if (bitFormat.m_numBitsY < thisBitFormat.m_numBitsY) bitFormat.m_numBitsY = thisBitFormat.m_numBitsY;
							if (bitFormat.m_numBitsZ < thisBitFormat.m_numBitsZ) bitFormat.m_numBitsZ = thisBitFormat.m_numBitsZ;
						} else {
							formatTrans[iAnimatedJoint].m_bitFormat = ComputeBitFormatFromValues_Vec3_Range(translationSamples, fErrorTolerance, formatTrans[iAnimatedJoint].m_contextData);
						}
					}
					formatTrans[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
				}
			}
		}
	}

	if (bSharedBitFormat) {
		if (compressionType == kAcctVec3Auto) {
			unsigned numBitsTotal;
			if (!bWithinErrorTolerance_SharedFloat16) {
				compressionType = kAcctVec3Uncompressed;
				bitFormat = kVbpfVec3Uncompressed;
				numBitsTotal = bitFormat.m_numBitsTotal * numOutputFrames * numChannelsTotal;
			} else {
				compressionType = kAcctVec3Float16;
				bitFormat = kVbpfVec3Float16;
				numBitsTotal = bitFormat.m_numBitsTotal * numOutputFrames * numChannelsTotal;
			}
			if (bWithinErrorTolerance_SharedRange)
			{
				bWithinErrorTolerance_SharedRange = !NormalizeBitFormat(bitFormat_SharedRange, 0);
				unsigned numBitsTotalRange = (numBitsFormatRange + bitFormat_SharedRange.m_numBitsTotal * numOutputFrames) * numChannelsTotal;
				if (bWithinErrorTolerance_SharedRange && numBitsTotalRange < numBitsTotal) {
					compressionType = kAcctVec3Range;
					bitFormat = bitFormat_SharedRange;
					numBitsTotal = numBitsTotalRange;
				}
			}
			for (auto it = targetAnimatedJoints.begin(); it != targetAnimatedJoints.end(); it++) {
				size_t iAnimatedJoint = *it;
				if (iAnimatedJoint >= numAnimatedJoints) {
					break;
				}
				unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
				if (iJointAnimIndex >= numJointAnims) {
					continue;	// not animated
				}
				if (targetAnimatedJoints_Scale.count(iAnimatedJoint) &&	!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex))
				{
					formatScale[iAnimatedJoint].m_compressionType = compressionType;
					formatScale[iAnimatedJoint].m_bitFormat = bitFormat;
					if (!IsVariableBitPackedFormat(compressionType)) {
						formatScale[iAnimatedJoint].m_contextData.resize(0);
					}
				}
				if (targetAnimatedJoints_Translation.count(iAnimatedJoint) && !sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex))
				{
					formatTrans[iAnimatedJoint].m_compressionType = compressionType;
					formatTrans[iAnimatedJoint].m_bitFormat = bitFormat;
					if (!IsVariableBitPackedFormat(compressionType)) {
						formatTrans[iAnimatedJoint].m_contextData.resize(0);
					}
				}
			}
		} else {
			NormalizeBitFormat(bitFormat, 0);
			for (auto it = targetAnimatedJoints.begin(); it != targetAnimatedJoints.end(); it++) {
				size_t iAnimatedJoint = *it;
				if (iAnimatedJoint >= numAnimatedJoints) {
					break;
				}
				unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
				if (iJointAnimIndex >= numJointAnims) {
					continue;	// not animated
				}
				if (targetAnimatedJoints_Scale.count(iAnimatedJoint) &&	!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex))	{
					formatScale[iAnimatedJoint].m_bitFormat = bitFormat;
				}
				if (targetAnimatedJoints_Translation.count(iAnimatedJoint) && !sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex))	{
					formatTrans[iAnimatedJoint].m_bitFormat = bitFormat;
				}
			}
		}
	}
	return true;
}

struct RotationValues
{
	RotationValues(unsigned iJointAnimIndex, float fErrorTolerance, AnimChannelCompressionType compressionType)
		: m_iJointAnimIndex(iJointAnimIndex)
		, m_fErrorTolerance(fErrorTolerance)
		, m_compressionType(compressionType)
		, m_formatUncorrelated(kVbpfNone)
		, m_formatNormalized(kVbpfNone)
	{
	}

	bool Init(ITGEOM::QuatArray const &aqValues, std::vector<U64>& contextData)
	{
		switch (m_compressionType) {
		case kAcctQuatLog:
		{
			if (m_formatUncorrelated.m_numBitsTotal == 0) {
				m_formatUncorrelated = ComputeBitFormatFromValues_Quat_Log(aqValues, m_fErrorTolerance, contextData, false, &m_fErrorEstimate);
			}
			ITGEOM::Quat qMean;
			GetQuatLogOrientation(contextData, qMean);
			ITGEOM::Quat qMeanConjugate = qMean.Conjugate();
			for (auto it = aqValues.begin(); it != aqValues.end(); ++it) {
				ITGEOM::Quat q = qMeanConjugate * (*it);
				// Take the log of q and store it as a vector
				ITGEOM::Vec3 v = QuatLog(q);
				m_avSourceLogValues.push_back(v);
			}
		}
			break;
		case kAcctQuatLogPca:
		{
			if (m_formatUncorrelated.m_numBitsTotal == 0) {
				m_formatUncorrelated = ComputeBitFormatFromValues_Quat_LogPca(aqValues, m_fErrorTolerance, contextData, false, &m_fErrorEstimate);
			}
			ITGEOM::Quat qPre, qPost;
			GetQuatLogPcaOrientation(contextData, qPre, qPost);
			ITGEOM::Quat qPreConjugate = qPre.Conjugate(), qPostConjugate = qPost.Conjugate();
			for (auto it = aqValues.begin(); it != aqValues.end(); ++it) {
				ITGEOM::Quat q = qPreConjugate * (*it) * qPostConjugate;
				// Take the log of q and store it as a vector
				ITGEOM::Vec3 v = QuatLog(q);
				m_avSourceLogValues.push_back(v);
			}
		}
			break;
		default:
			ITASSERT(m_compressionType == kAcctQuatLog || m_compressionType == kAcctQuatLogPca);
			return false;
		}

		// Data is always compressed with a normalized format, even though we track the unnormalized format
		// to allow us to better compare the quality of compression:
		m_formatNormalized = m_formatUncorrelated;
		NormalizeBitFormat(m_formatNormalized, 0);

		U64_Array aCompressed;
		return DoCompress_Vec3_Range(aCompressed, m_avOutputLogValues, contextData, m_avSourceLogValues, m_formatNormalized);
	}

	bool RecalculateOutputLogValues(RotationValues const& pred, Vec3BitPackingFormat format)
	{
		//		if (!(m_compressionType == kAcctQuatLog || m_compressionType == kAcctQuatLogPca)) {
		ITASSERT(m_compressionType == kAcctQuatLog || m_compressionType == kAcctQuatLogPca);
		//			return false;
		//		}
		unsigned numSamples = (unsigned)m_avSourceLogValues.size();
		ITGEOM::Vec3Array avLogDeltaValues;
		for (unsigned i = 0; i < numSamples; ++i) {
			ITGEOM::Vec3 vLogDelta = m_avSourceLogValues[i];
			ITGEOM::Vec3 const& vPrevOutputLogValue = pred.m_avOutputLogValues[i];
			//			if (format.m_xAxis >= 5) vLogDelta[0] += vPrevOutputLogValue[format.m_xAxis-5];
			//			else
			if (format.m_xAxis) vLogDelta[0] -= vPrevOutputLogValue[format.m_xAxis - 1];
			//			if (format.m_yAxis >= 5) vLogDelta[1] += vPrevOutputLogValue[format.m_yAxis-5];
			//			else
			if (format.m_yAxis) vLogDelta[1] -= vPrevOutputLogValue[format.m_yAxis - 1];
			//			if (format.m_zAxis >= 5) vLogDelta[2] += vPrevOutputLogValue[format.m_zAxis-5];
			//			else
			if (format.m_zAxis) vLogDelta[2] -= vPrevOutputLogValue[format.m_zAxis - 1];
			avLogDeltaValues.push_back(vLogDelta);
		}

		U64_Array aCompressed, contextData;
		if (!DoCompress_Vec3_Range(aCompressed, m_avOutputLogValues, contextData, avLogDeltaValues, format))
			return false;

		// m_avOutputLogValues now contains decompressed log space deltas; convert them back to log space:
		float fMaxErrorSqr = 0.0f;
		for (unsigned i = 0; i < numSamples; ++i) {
			ITGEOM::Vec3& vOutputLogValue = m_avOutputLogValues[i];
			ITGEOM::Vec3 const& vPrevOutputLogValue = pred.m_avOutputLogValues[i];
			//			if (format.m_xAxis >= 5) vOutputLogValue[0] -= vPrevOutputLogValue[format.m_xAxis-5];
			//			else
			if (format.m_xAxis) vOutputLogValue[0] += vPrevOutputLogValue[format.m_xAxis - 1];
			//			if (format.m_yAxis >= 5) vOutputLogValue[1] -= vPrevOutputLogValue[format.m_yAxis-5];
			//			else
			if (format.m_yAxis) vOutputLogValue[1] += vPrevOutputLogValue[format.m_yAxis - 1];
			//			if (format.m_zAxis >= 5) vOutputLogValue[2] -= vPrevOutputLogValue[format.m_zAxis-5];
			//			else
			if (format.m_zAxis) vOutputLogValue[2] += vPrevOutputLogValue[format.m_zAxis - 1];
			float fErrorSqr = DistSq(vOutputLogValue, m_avSourceLogValues[i]);
			if (fMaxErrorSqr < fErrorSqr)
				fMaxErrorSqr = fErrorSqr;
		}
		m_fErrorEstimate = 2.0f * sqrtf(fMaxErrorSqr);
		return true;
	}

	unsigned m_iJointAnimIndex;
	float m_fErrorTolerance;
	float m_fErrorEstimate;
	AnimChannelCompressionType m_compressionType;
	Vec3BitPackingFormat m_formatUncorrelated;
	Vec3BitPackingFormat m_formatNormalized;
	ITGEOM::Vec3Array m_avSourceLogValues;
	ITGEOM::Vec3Array m_avOutputLogValues;
};

// returns the number of bits saved and resulting format if aRotValues[iSucc] follows aRotValues[iPred]
unsigned EvaluateCrossCorrelationSuccessor(std::vector<RotationValues> const& aRotValues, unsigned iPred, unsigned iSucc, Vec3BitPackingFormat &formatDiff, bool bUseSourceData = false)
{
	ITGEOM::Vec3Array const& avPred = bUseSourceData ? aRotValues[iPred].m_avSourceLogValues : aRotValues[iPred].m_avOutputLogValues;
	ITGEOM::Vec3Array const& avSucc = aRotValues[iSucc].m_avSourceLogValues;
	unsigned const numSamples = (unsigned)avPred.size();

	formatDiff = aRotValues[iSucc].m_formatUncorrelated;
	float fLogErrorTolerance = aRotValues[iSucc].m_fErrorTolerance * 0.5f;
	for (unsigned iPredAxis = 0; iPredAxis < 3; ++iPredAxis) {
		ITGEOM::Vec3 vMin_Pos, vMax_Pos;
		//		ITGEOM::Vec3 vMin_Neg, vMax_Neg;
		for (unsigned iSuccAxis = 0; iSuccAxis < 3; ++iSuccAxis) {
			float fMin_Pos, fMax_Pos;
			//			float fMin_Neg, fMax_Neg;
			fMin_Pos = fMax_Pos = avSucc[0][iSuccAxis] - avPred[0][iPredAxis];
			//			fMin_Neg = fMax_Neg = avSucc[0][iSuccAxis] + avPred[0][iPredAxis];
			for (unsigned i = 1; i < numSamples; ++i) {
				float fDiff_Pos = avSucc[i][iSuccAxis] - avPred[i][iPredAxis];
				if (fMin_Pos > fDiff_Pos)
					fMin_Pos = fDiff_Pos;
				else if (fMax_Pos < fDiff_Pos)
					fMax_Pos = fDiff_Pos;
				/*
								float fDiff_Neg = avSucc[i][iSuccAxis] + avPred[i][iPredAxis];
								if (fMin_Neg > fDiff_Neg)
								fMin_Neg = fDiff_Neg;
								else if (fMax_Neg < fDiff_Neg)
								fMax_Neg = fDiff_Neg;
								*/
			}
			vMin_Pos[iSuccAxis] = fMin_Pos;
			vMax_Pos[iSuccAxis] = fMax_Pos;
			//			vMin_Neg[iSuccAxis] = fMin_Neg;
			//			vMax_Neg[iSuccAxis] = fMax_Neg;
		}
		Vec3BitPackingFormat format_Pos;
		ITGEOM::Vec3 vScale_Pos, vBias_Pos;
		CalculateVec3RangeAndFormat(vMin_Pos, vMax_Pos, fLogErrorTolerance, format_Pos, vScale_Pos, vBias_Pos);
		if (formatDiff.m_numBitsX > format_Pos.m_numBitsX) {
			formatDiff.m_numBitsX = format_Pos.m_numBitsX;
			formatDiff.m_xAxis = (U8)(1 + iPredAxis);
		}
		if (formatDiff.m_numBitsY > format_Pos.m_numBitsY) {
			formatDiff.m_numBitsY = format_Pos.m_numBitsY;
			formatDiff.m_yAxis = (U8)(1 + iPredAxis);
		}
		if (formatDiff.m_numBitsZ > format_Pos.m_numBitsZ) {
			formatDiff.m_numBitsZ = format_Pos.m_numBitsZ;
			formatDiff.m_zAxis = (U8)(1 + iPredAxis);
		}
		/*
				Vec3BitPackingFormat format_Neg;
				ITGEOM::Vec3 vScale_Neg, vBias_Neg;
				CalculateVec3RangeAndFormat(vMin_Neg, vMax_Neg, fLogErrorTolerance, format_Neg, vScale_Neg, vBias_Neg);
				if (formatDiff.m_numBitsX > format_Neg.m_numBitsX) {
				formatDiff.m_numBitsX = format_Neg.m_numBitsX;
				formatDiff.m_xAxis = (U8)(5 + iPredAxis);
				}
				if (formatDiff.m_numBitsY > format_Neg.m_numBitsY) {
				formatDiff.m_numBitsY = format_Neg.m_numBitsY;
				formatDiff.m_yAxis = (U8)(5 + iPredAxis);
				}
				if (formatDiff.m_numBitsZ > format_Neg.m_numBitsZ) {
				formatDiff.m_numBitsZ = format_Neg.m_numBitsZ;
				formatDiff.m_zAxis = (U8)(5 + iPredAxis);
				}
				*/
	}
	formatDiff.m_numBitsTotal = formatDiff.m_numBitsX + formatDiff.m_numBitsY + formatDiff.m_numBitsZ;
	ITASSERT(formatDiff.m_numBitsTotal <= aRotValues[iSucc].m_formatUncorrelated.m_numBitsTotal);
	return (unsigned)(aRotValues[iSucc].m_formatUncorrelated.m_numBitsTotal - formatDiff.m_numBitsTotal);
}

// finds the best successor and resulting format to follow iPred
unsigned FindCrossCorrelationSuccessor(std::set<size_t> &remaining, std::vector<RotationValues> const& aRotValues, unsigned iPred, std::pair<unsigned, Vec3BitPackingFormat> &indexAndFormat)
{
	unsigned numBitsSavedBest = 0;
	unsigned numAccuracyBitsSavedBest = 0;
	indexAndFormat.first = (unsigned)-1;
	for (std::set<size_t>::const_iterator remainingIt = remaining.begin(); remainingIt != remaining.end(); remainingIt++) {
		size_t iSucc = *remainingIt;
		ITASSERT(iSucc != iPred);
		Vec3BitPackingFormat formatDiff;
		unsigned numAccuracyBitsSaved = EvaluateCrossCorrelationSuccessor(aRotValues, iPred, (unsigned)iSucc, formatDiff);
		if (numAccuracyBitsSaved == 0)
			continue;	// no accuracy bits saved by this successor
		Vec3BitPackingFormat formatDiffNormalized = formatDiff;
		NormalizeBitFormat(formatDiffNormalized, 0);
		if (formatDiffNormalized.m_numBitsTotal > aRotValues[iSucc].m_formatNormalized.m_numBitsTotal)
			continue;	// skip any successors which result in more real bits than original due to normalization weirdnesses
		unsigned numBitsSaved = aRotValues[iSucc].m_formatNormalized.m_numBitsTotal - formatDiffNormalized.m_numBitsTotal;
		// if we saved any real bits, or failing that, if we saved any accuracy bits, keep this as our best so far:
		if (numBitsSavedBest < numBitsSaved || (numBitsSavedBest == numBitsSaved && numAccuracyBitsSavedBest < numAccuracyBitsSaved)) {
			numBitsSavedBest = numBitsSaved;
			numAccuracyBitsSavedBest = numAccuracyBitsSaved;
			indexAndFormat.first = (unsigned)iSucc;
			indexAndFormat.second = formatDiff;
		}
	}
	return numAccuracyBitsSavedBest;
}

static unsigned FindRotationDataCrossCorrelationOrder(std::vector<RotationValues>& aRotValues, std::vector<std::pair<unsigned, Vec3BitPackingFormat> >& aOrderAndFormat)
{
	std::set<size_t> remaining;
	for (size_t iRemaining = 0; iRemaining < aRotValues.size(); iRemaining++) {
		remaining.emplace(iRemaining);
	}

	unsigned numChannels = (unsigned)aRotValues.size();
	unsigned numFrames = aRotValues.empty() ? 0 : (unsigned)aRotValues[0].m_avSourceLogValues.size();
	AnimChannelCompressionType compressionType = aRotValues.empty() ? kAcctQuatLog : aRotValues[0].m_compressionType;
	INOTE(IMsg::kDebug, "Cross Correlating %u %s compressed quaternion anims with %u frames:\n", (unsigned)remaining.size(), (compressionType == kAcctQuatLog) ? "log" : "logpca", numFrames);

	unsigned numBitsSavedTotal = 0;
	unsigned numBitsTotal = 0;
	unsigned numAccuracyBitsSavedTotal = 0;
	unsigned numAccuracyBitsTotal = 0;
	while (!remaining.empty()) {
		size_t iPred = *remaining.begin();		// which one we start with does not seem to matter given our greedy approach below
		Vec3BitPackingFormat bitFormatUncorrelated = aRotValues[iPred].m_formatNormalized;

		INOTE(IMsg::kDebug, "  anim %3u fmt(x%2u,y%2u,z%2u=%2u)\n", aRotValues[iPred].m_iJointAnimIndex, bitFormatUncorrelated.m_numBitsX, bitFormatUncorrelated.m_numBitsY, bitFormatUncorrelated.m_numBitsZ, bitFormatUncorrelated.m_numBitsTotal);
		aOrderAndFormat.push_back(std::make_pair((unsigned)iPred, bitFormatUncorrelated));
		numBitsTotal += bitFormatUncorrelated.m_numBitsTotal;
		numAccuracyBitsTotal += aRotValues[iPred].m_formatUncorrelated.m_numBitsTotal;
		remaining.erase(iPred);

		while (!remaining.empty()) {
			std::pair<unsigned, Vec3BitPackingFormat> indexAndFormat;
			unsigned numAccuracyBitsSaved = FindCrossCorrelationSuccessor(remaining, aRotValues, (unsigned)iPred, indexAndFormat);
			if (numAccuracyBitsSaved == 0)
				break;
			ITASSERT(remaining.count(indexAndFormat.first));
			size_t iSucc = indexAndFormat.first;

			unsigned numAccuracyBits = aRotValues[iSucc].m_formatUncorrelated.m_numBitsTotal;
			bitFormatUncorrelated = aRotValues[iSucc].m_formatNormalized;
			ITASSERT(indexAndFormat.second.m_numBitsTotal + numAccuracyBitsSaved == numAccuracyBits);
			NormalizeBitFormat(indexAndFormat.second, 0);
			//FindCrossCorrelationSuccessor should never return a format that increases the number of real bits used:
			ITASSERT(indexAndFormat.second.m_numBitsTotal <= bitFormatUncorrelated.m_numBitsTotal);

			bool bSuccess = aRotValues[iSucc].RecalculateOutputLogValues(aRotValues[iPred], indexAndFormat.second);
			ITASSERT(bSuccess);

			INOTE(IMsg::kDebug, "  anim %3u fmt(x%2u,y%2u,z%2u=%2u) -> fmt(x%2u,y%2u,z%2u=%2u) axes(x%u,y%u,z%u)\n", aRotValues[iSucc].m_iJointAnimIndex, bitFormatUncorrelated.m_numBitsX, bitFormatUncorrelated.m_numBitsY, bitFormatUncorrelated.m_numBitsZ, bitFormatUncorrelated.m_numBitsTotal, indexAndFormat.second.m_numBitsX, indexAndFormat.second.m_numBitsY, indexAndFormat.second.m_numBitsZ, indexAndFormat.second.m_numBitsTotal, indexAndFormat.second.m_xAxis, indexAndFormat.second.m_yAxis, indexAndFormat.second.m_zAxis);
			aOrderAndFormat.push_back(indexAndFormat);

			numBitsTotal += bitFormatUncorrelated.m_numBitsTotal;
			numBitsSavedTotal += (bitFormatUncorrelated.m_numBitsTotal - indexAndFormat.second.m_numBitsTotal);
			numAccuracyBitsTotal += numAccuracyBits;
			numAccuracyBitsSavedTotal += numAccuracyBitsSaved;

			iPred = iSucc;
			remaining.erase(iPred);
		}
	}
	if (numFrames && numBitsTotal) {
		unsigned numBitsFormat = GetFormatDataSize(compressionType);
		INOTE(IMsg::kDebug, "  By size:     Saved %u bits x%u frames + %u bytes format = %u/%u bytes (%4.1f%%)\n", numBitsSavedTotal, numFrames, (numBitsFormat*numChannels + 7) / 8, (numBitsSavedTotal*numFrames) / 8, (numBitsFormat*numChannels + 7) / 8 + (numBitsTotal*numFrames + 7) / 8, ((numBitsSavedTotal*numFrames) / 8 * 100.0f) / ((numBitsFormat*numChannels + 7) / 8 + (numBitsTotal*numFrames + 7) / 8));
		INOTE(IMsg::kDebug, "  By accuracy: Saved %u bits/%u bits (%4.1f%%)\n", numAccuracyBitsSavedTotal, numAccuracyBitsTotal, (numAccuracyBitsSavedTotal * 100.0f) / numAccuracyBitsTotal);
	}
	ITASSERT(aOrderAndFormat.size() == aRotValues.size());
	return numAccuracyBitsSavedTotal;
}

void GenerateRotationAutoFormatsBitFormatsAndOrder(
	ClipCompressionDesc& compression,
	AnimationBinding const& binding,
	AnimationClipSourceData const& sourceData,
	std::vector< std::pair<unsigned,float> > const& aJointAnimIndexAndTolerance)
{
	unsigned const numAnims = (unsigned)aJointAnimIndexAndTolerance.size();
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;

	INOTE(IMsg::kDebug, "kAcctQuatAuto compression total bit sizes by type for %u joints with %u frames of data:\n", numAnims, numOutputFrames);
	INOTE(IMsg::kDebug, "joint   unc    sm3   log   pca (uncorrelated)\n");

	// these are the number of bits required by each format & includes the actual spec size plus any context data (e.g. qMean etc)
	unsigned const numBitsFormatSmallest3 = GetFormatDataSize(kAcctQuatSmallestThree) * 8;
	unsigned const numBitsFormatLog = GetFormatDataSize(kAcctQuatLog) * 8;
	unsigned const numBitsFormatLogPca = GetFormatDataSize(kAcctQuatLogPca) * 8;

	ITASSERT(numAnims <= 0xFFFF);

	// First calculate smallest uncorrelated format
	std::vector<AnimChannelCompressionType> aCompressionType;
	std::vector<Vec3BitPackingFormat> aBitFormat;
	std::vector<unsigned> aNumBitsTotal;
	std::vector<U64_Array> aaContextDataLog(numAnims), aaContextDataLogPca(numAnims);

	// entries = { short bitSaved, short iAnim }
	std::priority_queue<int> queueLog, queueLogPca;		// ordered by total number bits saved vs smallest3 & iAnim

	unsigned numBitsTotalUncorrelated = 0;
	unsigned numAnimsToCorrelateLog = 0, numAnimsToCorrelateLogPca = 0;
	for (unsigned iAnim = 0; iAnim < numAnims; ++iAnim) {
		unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
		float fErrorTolerance = aJointAnimIndexAndTolerance[iAnim].second;
		ITGEOM::QuatArray rotationSamples; 
		sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(rotationSamples);
		ITASSERT((unsigned)rotationSamples.size() == sourceData.m_numFrames);

		// compute bit formats for each applicable encoding & return error estimates
		float const fErrorUncompressed = CalculateQuatUncompressedError( rotationSamples );
		float fErrorSmallest3 = 0.0f, fErrorLog = 0.0f, fErrorLogPca = 0.0f;
		Vec3BitPackingFormat bitFormatSmallest3 = ComputeBitFormatFromValues_Quat_SmallestThree(rotationSamples, fErrorTolerance, true, &fErrorSmallest3);
		Vec3BitPackingFormat bitFormatLog = ComputeBitFormatFromValues_Quat_Log(rotationSamples, fErrorTolerance, aaContextDataLog[iAnim], true, &fErrorLog);
		Vec3BitPackingFormat bitFormatLogPca = ComputeBitFormatFromValues_Quat_LogPca(rotationSamples, fErrorTolerance, aaContextDataLogPca[iAnim], true, &fErrorLogPca);
		
		// total bits for each encoding given the formats just calculated
		unsigned numBitsTotalUncompressed = 128 * numOutputFrames;
		unsigned numBitsTotalSmallest3 = numBitsFormatSmallest3 + bitFormatSmallest3.m_numBitsTotal * numOutputFrames;
		unsigned numBitsTotalLog = numBitsFormatLog + bitFormatLog.m_numBitsTotal * numOutputFrames;
		unsigned numBitsTotalLogPca = numBitsFormatLogPca + bitFormatLogPca.m_numBitsTotal * numOutputFrames;

		if (fErrorLog <= fErrorTolerance || fErrorLog <= fErrorUncompressed) {
			queueLog.push( ((int)(numBitsTotalSmallest3 - numBitsTotalLog) << 16) | iAnim );
			numAnimsToCorrelateLog++;
		}
		if (fErrorLogPca <= fErrorTolerance || fErrorLogPca <= fErrorUncompressed) {
			queueLogPca.push( ((int)(numBitsTotalSmallest3 - numBitsTotalLogPca) << 16) | iAnim );
			numAnimsToCorrelateLogPca++;
		}

		// select encoding with smallest error & least bits
		unsigned numBitsTotal = numBitsTotalUncompressed;
		AnimChannelCompressionType compressionType = kAcctQuatUncompressed;
		Vec3BitPackingFormat bitFormat = kVbpfQuatUncompressed;
		if ((fErrorLogPca <= fErrorTolerance || fErrorLogPca <= fErrorUncompressed) && numBitsTotal > numBitsTotalLogPca) {
			numBitsTotal = numBitsTotalLogPca;
			compressionType = kAcctQuatLogPca;
			bitFormat = bitFormatLogPca;
		}
		if ((fErrorLog <= fErrorTolerance || fErrorLog <= fErrorUncompressed) && numBitsTotal > numBitsTotalLog) {
			numBitsTotal = numBitsTotalLog;
			compressionType = kAcctQuatLog;
			bitFormat = bitFormatLog;
		}
		if ((fErrorSmallest3 <= fErrorTolerance || fErrorSmallest3 <= fErrorUncompressed)  && numBitsTotal > numBitsTotalSmallest3) {
			numBitsTotal = numBitsTotalSmallest3;
			compressionType = kAcctQuatSmallestThree;
			bitFormat = bitFormatSmallest3;
		}
		aCompressionType.push_back(compressionType);
		aBitFormat.push_back(bitFormat);
		aNumBitsTotal.push_back(numBitsTotal);
		numBitsTotalUncorrelated += numBitsTotal;

		INOTE(IMsg::kDebug, "%5u %5x%c %5x%c %5x%c %5x%c\n",
			binding.m_jointAnimIdToJointId[iJointAnimIndex],
			numBitsTotalUncompressed,		(compressionType==kAcctQuatUncompressed)?'*':(fErrorUncompressed <= fErrorTolerance)?'.':'x',
			numBitsTotalSmallest3,			(compressionType==kAcctQuatSmallestThree)?'*':(fErrorSmallest3 <= fErrorTolerance)?'.':'x',
			numBitsTotalLog,				(compressionType==kAcctQuatLog)?'*':(fErrorLog <= fErrorTolerance)?'.':'x',
			numBitsTotalLogPca,				(compressionType==kAcctQuatLogPca)?'*':(fErrorLogPca <= fErrorTolerance)?'.':'x' );
	}

	// Now cross correlate
	if (numAnimsToCorrelateLog > 1 || numAnimsToCorrelateLogPca > 1) 
	{
		// collect our rotations values for cross correlation ordered to maximize the chance of starting correlation from a good anim -
		// i.e. one for which uncorrelated log compression already saves bits
		auto CollectRotationValues = [&] (std::priority_queue<int>& q, 
										  std::vector<RotationValues>& rotValues, 
										  std::vector<unsigned>& animFromRotIndex, 
										  AnimChannelCompressionType compType, 
										  std::vector<U64_Array>& aaContextData) 
		{
			for (unsigned iRotIndex = 0; !q.empty(); ++iRotIndex) 
			{
				int iQueue = q.top(); q.pop();
				int numBitsTotalSavedVsSmallest3 = iQueue>>16;	(void)numBitsTotalSavedVsSmallest3;
				unsigned iAnim = (unsigned)(iQueue & 0xFFFF);
				unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
				float fErrorTolerance = aJointAnimIndexAndTolerance[iAnim].second;
				ITGEOM::QuatArray aqValues; 
				sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(aqValues);
				animFromRotIndex[iRotIndex] = iAnim;
				rotValues.push_back(RotationValues(iJointAnimIndex, fErrorTolerance, compType));
				bool bSuccess = rotValues.back().Init(aqValues, aaContextData[iAnim]);
				ITASSERT(bSuccess);
			}
		};

		// log
		std::vector<RotationValues> aRotValuesLog;
		std::vector<unsigned> aAnimFromRotIndexLog(numAnimsToCorrelateLog, (unsigned)-1);
		CollectRotationValues(queueLog, aRotValuesLog, aAnimFromRotIndexLog, kAcctQuatLog, aaContextDataLog);
		ITASSERT( aAnimFromRotIndexLog[numAnimsToCorrelateLog-1] != (unsigned)-1 && (unsigned)aRotValuesLog.size() == numAnimsToCorrelateLog );

		// log pca
		std::vector<RotationValues> aRotValuesLogPca;
		std::vector<unsigned> aAnimFromRotIndexLogPca(numAnimsToCorrelateLogPca, (unsigned)-1);
		CollectRotationValues(queueLogPca, aRotValuesLogPca, aAnimFromRotIndexLogPca, kAcctQuatLogPca, aaContextDataLogPca);
		ITASSERT(aAnimFromRotIndexLogPca[numAnimsToCorrelateLogPca - 1] != (unsigned)-1 && (unsigned)aRotValuesLogPca.size() == numAnimsToCorrelateLogPca);

		std::vector<Vec3BitPackingFormat> aBitFormatLog(numAnims, kVbpfNone), aBitFormatLogPca(numAnims, kVbpfNone);
		std::vector<unsigned> aNumBitsTotalLog(numAnims, (unsigned)-1), aNumBitsTotalLogPca(numAnims, (unsigned)-1);
		std::vector<std::vector<unsigned> > aAnimsInCorrelationGroupLog, aAnimsInCorrelationGroupLogPca;

		// find cross correlation order for log anims
		std::vector< std::pair<unsigned, Vec3BitPackingFormat> > aOrderAndFormat;
		unsigned numBitsSaved = FindRotationDataCrossCorrelationOrder(aRotValuesLog, aOrderAndFormat);
		ITASSERT(aOrderAndFormat.size() == numAnimsToCorrelateLog);
		if (numBitsSaved > 0) {
			unsigned iAnimStart = (unsigned)-1;
			for (unsigned iOrder = 0; iOrder < numAnimsToCorrelateLog; iOrder++) {
				unsigned iRotIndex = aOrderAndFormat[iOrder].first;
				unsigned iAnim = aAnimFromRotIndexLog[iRotIndex];
				Vec3BitPackingFormat const& bitFormatCorrelated = aOrderAndFormat[iOrder].second;
				if ( bitFormatCorrelated.m_xAxis || bitFormatCorrelated.m_yAxis || bitFormatCorrelated.m_zAxis ) {
					if (iAnimStart != (unsigned)-1) {
						aAnimsInCorrelationGroupLog.push_back( std::vector<unsigned>() );
						aAnimsInCorrelationGroupLog.back().push_back( iAnimStart );
						iAnimStart = (unsigned)-1;
					} else {
						ITASSERT(!aAnimsInCorrelationGroupLog.empty());
					}
					aAnimsInCorrelationGroupLog.back().push_back( iAnim );
				} else {
					iAnimStart = iAnim;
				}
				aBitFormatLog[iAnim] = bitFormatCorrelated;
				aNumBitsTotalLog[iAnim] = numBitsFormatLog + bitFormatCorrelated.m_numBitsTotal * numOutputFrames;
			}
		}

		// find cross correlation order for log pca anims
		aOrderAndFormat.clear();
		numBitsSaved = FindRotationDataCrossCorrelationOrder(aRotValuesLogPca, aOrderAndFormat);
		ITASSERT(aOrderAndFormat.size() == numAnimsToCorrelateLogPca);
		if (numBitsSaved > 0) {
			unsigned iAnimStart = (unsigned)-1;
			for (unsigned iOrder = 0; iOrder < numAnimsToCorrelateLogPca; iOrder++) {
				unsigned iRotIndex = aOrderAndFormat[iOrder].first;
				unsigned iAnim = aAnimFromRotIndexLogPca[iRotIndex];
				Vec3BitPackingFormat const& bitFormatCorrelated = aOrderAndFormat[iOrder].second;
				if ( bitFormatCorrelated.m_xAxis || bitFormatCorrelated.m_yAxis || bitFormatCorrelated.m_zAxis ) {
					if (iAnimStart != (unsigned)-1) {
						aAnimsInCorrelationGroupLogPca.push_back( std::vector<unsigned>() );
						aAnimsInCorrelationGroupLogPca.back().push_back( iAnimStart );
						iAnimStart = (unsigned)-1;
					} else {
						ITASSERT(!aAnimsInCorrelationGroupLogPca.empty());
					}
					aAnimsInCorrelationGroupLogPca.back().push_back( iAnim );
				} else {
					iAnimStart = iAnim;
				}
				aBitFormatLogPca[iAnim] = bitFormatCorrelated;
				aNumBitsTotalLogPca[iAnim] = numBitsFormatLogPca + bitFormatCorrelated.m_numBitsTotal * numOutputFrames;
			}
		}

		// For each log/logpca correlation group, reduce length of chain if any uncorrelated format provides better compression
		unsigned numGroupsLog = 0, numGroupsLogPca = 0;
		unsigned numBitsTotalSavedLog = 0, numBitsTotalSavedLogPca = 0;
		for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLog.size(); iGroup < numGroups; ++iGroup) {
			unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLog[iGroup].size();
			unsigned numBitsTotalUncorrelated = 0, numBitsTotalCorrelated = 0;
			unsigned numAnimsInSum = 0;
			while (numAnimsInSum < numAnimsInGroup) {
				++numAnimsInSum;
				unsigned iAnim = aAnimsInCorrelationGroupLog[iGroup][ numAnimsInGroup - numAnimsInSum ];
				numBitsTotalUncorrelated += aNumBitsTotal[ iAnim ];
				numBitsTotalCorrelated += aNumBitsTotalLog[ iAnim ];
				if (numBitsTotalUncorrelated <= numBitsTotalCorrelated) {
					numAnimsInGroup -= numAnimsInSum;
					aAnimsInCorrelationGroupLog[iGroup].resize( numAnimsInGroup );
					numAnimsInSum = numBitsTotalUncorrelated = numBitsTotalCorrelated = 0;
				}
			}
			if (numAnimsInGroup) {
				numBitsTotalSavedLog += (numBitsTotalUncorrelated - numBitsTotalCorrelated);
				++numGroupsLog;
			}
		}
		for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLogPca.size(); iGroup < numGroups; ++iGroup) {
			unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLogPca[iGroup].size();
			unsigned numBitsTotalUncorrelated = 0, numBitsTotalCorrelated = 0;
			unsigned numAnimsInSum = 0;
			while (numAnimsInSum < numAnimsInGroup) {
				++numAnimsInSum;
				unsigned iAnim = aAnimsInCorrelationGroupLogPca[iGroup][ numAnimsInGroup - numAnimsInSum ];
				numBitsTotalUncorrelated += aNumBitsTotal[ iAnim ];
				numBitsTotalCorrelated += aNumBitsTotalLogPca[ iAnim ];
				if (numBitsTotalUncorrelated <= numBitsTotalCorrelated) {
					numAnimsInGroup -= numAnimsInSum;
					aAnimsInCorrelationGroupLogPca[iGroup].resize( numAnimsInGroup );
					numAnimsInSum = numBitsTotalUncorrelated = numBitsTotalCorrelated = 0;
				}
			}
			if (numAnimsInGroup) {
				numBitsTotalSavedLogPca += (numBitsTotalUncorrelated - numBitsTotalCorrelated);
				++numGroupsLogPca;
			}
		}
		if (numGroupsLog == 0 && numGroupsLogPca == 0) {
			// if no correlation groups remain, we're done
			INOTE(IMsg::kDebug, "Cross correlation saves 0 bytes of %u bytes (uncorrelated)\n", (numBitsTotalUncorrelated+7)/8);
		} else if (numGroupsLog == 0) {
			INOTE(IMsg::kDebug, "Cross correlation with logpca saves %u bytes of %u bytes (%4.1f%%)\n", numBitsTotalSavedLogPca/8, (numBitsTotalUncorrelated+7)/8, ((numBitsTotalSavedLogPca/8) * 100.f) / ((numBitsTotalUncorrelated+7)/8));
			// if only logpca correlation groups remain, we know there are no overlaps
			for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLogPca.size(); iGroup < numGroups; ++iGroup) {
				unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLogPca[iGroup].size();
				for (unsigned iGroupIndex = 0; iGroupIndex < numAnimsInGroup; ++iGroupIndex) {
					unsigned iAnim = aAnimsInCorrelationGroupLogPca[iGroup][iGroupIndex];
					aCompressionType[iAnim] = kAcctQuatLogPca;
					aBitFormat[iAnim] = aBitFormatLogPca[iAnim];
					aNumBitsTotal[iAnim] = aNumBitsTotalLogPca[iAnim];
				}
			}
		} else if (numGroupsLogPca == 0) {
			INOTE(IMsg::kDebug, "Cross correlation with log saves %u bytes of %u bytes (%4.1f%%)\n", numBitsTotalSavedLog/8, (numBitsTotalUncorrelated+7)/8, ((numBitsTotalSavedLog/8) * 100.f) / ((numBitsTotalUncorrelated+7)/8));
			// if only log correlation groups remain, we know there are no overlaps
			for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLog.size(); iGroup < numGroups; ++iGroup) {
				unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLog[iGroup].size();
				for (unsigned iGroupIndex = 0; iGroupIndex < numAnimsInGroup; ++iGroupIndex) {
					unsigned iAnim = aAnimsInCorrelationGroupLog[iGroup][iGroupIndex];
					aCompressionType[iAnim] = kAcctQuatLog;
					aBitFormat[iAnim] = aBitFormatLog[iAnim];
					aNumBitsTotal[iAnim] = aNumBitsTotalLog[iAnim];
				}
			}
		} else {
			// choose the better of the two overall compression rates, then generate sequences for the other type from all remaining :
			AnimChannelCompressionType compressionTypePrimary = (numBitsTotalSavedLogPca > numBitsTotalSavedLog) ? kAcctQuatLogPca : kAcctQuatLog;
			std::vector<std::vector<unsigned> > const& aAnimsInCorrelationGroupPrimary = (compressionTypePrimary == kAcctQuatLog) ? aAnimsInCorrelationGroupLog : aAnimsInCorrelationGroupLogPca;
			std::vector<Vec3BitPackingFormat> const& aBitFormatPrimary = (compressionTypePrimary == kAcctQuatLog) ? aBitFormatLog : aBitFormatLogPca;
			std::vector<unsigned> const& aNumBitsTotalPrimary = (compressionTypePrimary == kAcctQuatLog) ? aNumBitsTotalLog : aNumBitsTotalLogPca;
			unsigned numBitsTotalSavedPrimary = (compressionTypePrimary == kAcctQuatLog) ? numBitsTotalSavedLog : numBitsTotalSavedLogPca;
//			U32 numAnimsToCorrelate = (compressionTypePrimary == kAcctQuatLog) ? numAnimsToCorrelateLog : numAnimsToCorrelateLogPca;
			U32 numAnimsSecondary = (compressionTypePrimary == kAcctQuatLog) ? numAnimsToCorrelateLogPca : numAnimsToCorrelateLog;
			unsigned numBitsTotalSavedSecondary = 0;
			std::vector<std::vector<unsigned> >& aAnimsInCorrelationGroupSecondary = (compressionTypePrimary == kAcctQuatLog) ? aAnimsInCorrelationGroupLogPca : aAnimsInCorrelationGroupLog;
			aAnimsInCorrelationGroupSecondary.clear();

			std::set<size_t> animsPrimary;
			for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupPrimary.size(); iGroup < numGroups; ++iGroup) {
				unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupPrimary[iGroup].size();
				for (unsigned iGroupIndex = 0; iGroupIndex < numAnimsInGroup; ++iGroupIndex) {
					unsigned iAnim = aAnimsInCorrelationGroupPrimary[iGroup][iGroupIndex];
					aCompressionType[iAnim] = compressionTypePrimary;
					aBitFormat[iAnim] = aBitFormatPrimary[iAnim];
					aNumBitsTotal[iAnim] = aNumBitsTotalPrimary[iAnim];
					animsPrimary.emplace(iAnim);
				}
			}
			if (numAnimsSecondary > 1) {
				AnimChannelCompressionType compressionTypeSecondary = (compressionTypePrimary == kAcctQuatLog) ? kAcctQuatLogPca : kAcctQuatLog;
				std::vector<Vec3BitPackingFormat>& aBitFormatSecondary = (compressionTypePrimary == kAcctQuatLog) ? aBitFormatLogPca : aBitFormatLog;
				std::vector<unsigned>& aNumBitsTotalSecondary = (compressionTypePrimary == kAcctQuatLog) ? aNumBitsTotalLogPca : aNumBitsTotalLog;
				unsigned numBitsFormatSecondary = (compressionTypePrimary == kAcctQuatLog) ? numBitsFormatLogPca : numBitsFormatLog;
				std::vector<unsigned> const& aAnimFromRotIndexSecondary = (compressionTypePrimary == kAcctQuatLog) ? aAnimFromRotIndexLogPca : aAnimFromRotIndexLog;

				std::vector<RotationValues> aRotValuesSecondary;
				std::vector<U64_Array> aaContextDataSecondary(numAnimsSecondary);
				std::vector<unsigned> aAnimFromAnimSecondary;
				unsigned iAnimSecondary = 0;
				for (unsigned iRotIndex = 0; iRotIndex < numAnimsSecondary; ++iRotIndex) {
					unsigned iAnim = aAnimFromRotIndexSecondary[iRotIndex];
					if (animsPrimary.count(iAnim))
						continue;
					unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
					aRotValuesSecondary.push_back(RotationValues(iJointAnimIndex, aJointAnimIndexAndTolerance[iAnim].second, compressionTypeSecondary));
					ITGEOM::QuatArray rotationSamples; 
					sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(rotationSamples);
					bool bSuccess = aRotValuesSecondary.back().Init( rotationSamples, aaContextDataSecondary[iAnimSecondary] );
					ITASSERT(bSuccess);
					aAnimFromAnimSecondary.push_back(iAnim);
					++iAnimSecondary;
				}
				numAnimsSecondary = iAnimSecondary;

				std::vector<std::pair<unsigned, Vec3BitPackingFormat> > aOrderAndFormat;
				unsigned numBitsSaved = FindRotationDataCrossCorrelationOrder(aRotValuesSecondary, aOrderAndFormat);
				ITASSERT((unsigned)aOrderAndFormat.size() == numAnimsSecondary);
				if (numBitsSaved > 0) {
					unsigned iEndOfGroup = numAnimsSecondary;
					unsigned numBitsTotalSumCorrelated = 0, numBitsTotalSumUncorrelated = 0;
					for (unsigned iOrder = numAnimsSecondary; iOrder > 0; ) {
						--iOrder;
						iAnimSecondary = aOrderAndFormat[iOrder].first;
						unsigned iAnim = aAnimFromAnimSecondary[iAnimSecondary];
						Vec3BitPackingFormat const& bitFormatCorrelated = aOrderAndFormat[iOrder].second;
						aBitFormatSecondary[iAnim] = bitFormatCorrelated;
						aNumBitsTotalSecondary[iAnim] = numBitsFormatSecondary + bitFormatCorrelated.m_numBitsTotal * numOutputFrames;
						numBitsTotalSumCorrelated += aNumBitsTotalSecondary[iAnim];
						numBitsTotalSumUncorrelated += aNumBitsTotal[iAnim];
						if (numBitsTotalSumUncorrelated <= numBitsTotalSumCorrelated) {
							numBitsTotalSumCorrelated = numBitsTotalSumUncorrelated = 0;
							iEndOfGroup = iOrder;
						}
						if (!(bitFormatCorrelated.m_xAxis || bitFormatCorrelated.m_yAxis || bitFormatCorrelated.m_zAxis)) {
							if (iEndOfGroup > iOrder) {
								numBitsTotalSavedSecondary += (numBitsTotalSumUncorrelated - numBitsTotalSumCorrelated);
								aAnimsInCorrelationGroupSecondary.push_back(std::vector<unsigned>());
								for (unsigned iOrderOut = iOrder; iOrderOut < iEndOfGroup; ++iOrderOut) {
									iAnimSecondary = aOrderAndFormat[iOrderOut].first;
									iAnim = aAnimFromAnimSecondary[iAnimSecondary];
									aCompressionType[iAnim] = compressionTypeSecondary;
									aBitFormat[iAnim] = aBitFormatSecondary[iAnim];
									aNumBitsTotal[iAnim] = aNumBitsTotalSecondary[iAnim];
									aAnimsInCorrelationGroupSecondary.back().push_back(iAnim);
								}
							}
							iEndOfGroup = iOrder;
						}
					}
				}
			}
			INOTE(IMsg::kDebug, "Cross correlation with %s saves %u+%u=%u bytes of %u bytes (%4.1f%%)\n",
				((compressionTypePrimary == kAcctQuatLog) ? "log,logpca" : "logpca,log"),
				numBitsTotalSavedPrimary/8, (numBitsTotalSavedPrimary + numBitsTotalSavedSecondary)/8 - numBitsTotalSavedPrimary/8, (numBitsTotalSavedPrimary + numBitsTotalSavedSecondary)/8,
				(numBitsTotalUncorrelated+7)/8, (((numBitsTotalSavedPrimary + numBitsTotalSavedSecondary)/8) * 100.f) / ((numBitsTotalUncorrelated+7)/8));
		}

		// For any remaining correlation groups, store the joint anim index order:
		for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLog.size(); iGroup < numGroups; ++iGroup) {
			unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLog[iGroup].size();
			for (unsigned iGroupIndex = 0; iGroupIndex < numAnimsInGroup; ++iGroupIndex) {
				unsigned iAnim = aAnimsInCorrelationGroupLog[iGroup][iGroupIndex];
				unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
				compression.m_jointAnimOrderQuatLog.push_back(iJointAnimIndex);
			}
		}
		for (unsigned iGroup = 0, numGroups = (unsigned)aAnimsInCorrelationGroupLogPca.size(); iGroup < numGroups; ++iGroup) {
			unsigned numAnimsInGroup = (unsigned)aAnimsInCorrelationGroupLogPca[iGroup].size();
			for (unsigned iGroupIndex = 0; iGroupIndex < numAnimsInGroup; ++iGroupIndex) {
				unsigned iAnim = aAnimsInCorrelationGroupLogPca[iGroup][iGroupIndex];
				unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
				compression.m_jointAnimOrderQuatLogPca.push_back(iJointAnimIndex);
			}
		}
	}

	// Now copy the best compression type we found for each joint into our output:
	unsigned numBitsTotalSum = 0;
	for (unsigned iAnim = 0; iAnim < numAnims; ++iAnim) {
		unsigned iJointAnimIndex = aJointAnimIndexAndTolerance[iAnim].first;
		unsigned iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		std::vector<ChannelCompressionDesc>& formatQuat = compression.m_format[kChannelTypeRotation];
		formatQuat[iAnimatedJoint].m_compressionType = aCompressionType[iAnim];
		formatQuat[iAnimatedJoint].m_bitFormat = aBitFormat[iAnim];
		formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
		if (aCompressionType[iAnim] == kAcctQuatLog) {
			formatQuat[iAnimatedJoint].m_contextData = aaContextDataLog[iAnim];
		} else if (aCompressionType[iAnim] == kAcctQuatLogPca) {
			formatQuat[iAnimatedJoint].m_contextData = aaContextDataLogPca[iAnim];
		}
		numBitsTotalSum += aNumBitsTotal[iAnim];
	}
	(void)numBitsTotalSum;
}

void DumpQuatAnimErrorTables( std::set<size_t> const& targetJoints, AnimationBinding const& binding, AnimationClipSourceData const& sourceData )
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();

	for (auto it = targetJoints.begin(); it != targetJoints.end(); it++) {
		size_t iAnimatedJoint = *it;
		if (iAnimatedJoint >= numAnimatedJoints) {
			break;
		}
		unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
		if (iJointAnimIndex >= numJointAnims) {
			continue;
		}
		if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
			continue;
		}
		static const float kfLn2 = logf( 2.0f );
		static const float kfLn2Inv = 1.0f / kfLn2;
		ITGEOM::QuatArray rotationSamples; 
		sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(rotationSamples);
		float const fErrorUncompressed = CalculateQuatUncompressedError( rotationSamples );
		float const fErrorUncompressedLog2 = kfLn2Inv * logf( fErrorUncompressed );
		U32 const numSamples = (U32)rotationSamples.size();
		U32 const sizeUncompressed = numSamples*kVbpfQuatUncompressed.m_numBitsTotal;
		U32 const numBitsFormatSmallest3 = GetFormatDataSize(kAcctQuatSmallestThree) * 8;
		U32 const numBitsFormatLog = GetFormatDataSize(kAcctQuatLog) * 8;
		U32 const numBitsFormatLogPca = GetFormatDataSize(kAcctQuatLogPca) * 8;

		INOTE(IMsg::kDebug, "Error Table for ajoint[%u].rot %u samples\n", iAnimatedJoint, numSamples );
		INOTE(IMsg::kDebug, "uncompressed log2E=%7.3f size=%6u\n", fErrorUncompressedLog2, sizeUncompressed);
		INOTE(IMsg::kDebug, "        smallest3 vs. ET:                   log vs. ET:                         logpca vs. ET:\n");
		INOTE(IMsg::kDebug, "log2ET  log2EE  log2E   N  Nx Ny Nz size  X log2EE  log2E   N  Nx Ny Nz size  X log2EE  log2E   N  Nx Ny Nz size  X U size   format\n");
		for (U32 iET = 4*2; iET <= 25*2; iET++)
		{
			float fETLog2 = -(float)iET / (float)2;
			float fErrorTolerance = expf( kfLn2 * fETLog2 );
			float fErrorEstSmallest3 = 0.0f, fErrorSmallest3 = 0.0f;
			Vec3BitPackingFormat bitFormatSmallest3 = ComputeBitFormatFromValues_Quat_SmallestThree(rotationSamples, fErrorTolerance, true, &fErrorEstSmallest3);
			IBitCompressedQuatArray* pCompressedSmallest3 = BitCompressQuatArray(rotationSamples, kAcctQuatSmallestThree, bitFormatSmallest3);
			fErrorSmallest3 = pCompressedSmallest3->GetMaxError();
			float fErrorEstLog2Smallest3 = kfLn2Inv * logf( fErrorEstSmallest3 );
			float fErrorLog2Smallest3 = kfLn2Inv * logf( fErrorSmallest3 );
			U32 sizeSmallest3 = numBitsFormatSmallest3 + numSamples*bitFormatSmallest3.m_numBitsTotal;

			float fErrorEstLog = 0.0f, fErrorLog = 0.0f;
			U64_Array aContextDataLog;
			Vec3BitPackingFormat bitFormatLog = ComputeBitFormatFromValues_Quat_Log(rotationSamples, fErrorTolerance, aContextDataLog, true, &fErrorEstLog);
			IBitCompressedQuatArray* pCompressedLog = BitCompressQuatArray(rotationSamples, kAcctQuatLog, bitFormatLog, &aContextDataLog);
			fErrorLog = pCompressedLog->GetMaxError();
			float fErrorEstLog2Log = kfLn2Inv * logf( fErrorEstLog );
			float fErrorLog2Log = kfLn2Inv * logf( fErrorLog );
			U32 sizeLog = numBitsFormatLog + numSamples*bitFormatLog.m_numBitsTotal;

			float fErrorEstLogPca = 0.0f, fErrorLogPca = 0.0f;
			U64_Array aContextDataLogPca;
			Vec3BitPackingFormat bitFormatLogPca = ComputeBitFormatFromValues_Quat_LogPca(rotationSamples, fErrorTolerance, aContextDataLogPca, true, &fErrorEstLogPca);
			IBitCompressedQuatArray* pCompressedLogPca = BitCompressQuatArray(rotationSamples, kAcctQuatLogPca, bitFormatLogPca, &aContextDataLogPca);
			fErrorLogPca = pCompressedLogPca->GetMaxError();
			float fErrorEstLog2LogPca = kfLn2Inv * logf( fErrorEstLogPca );
			float fErrorLog2LogPca = kfLn2Inv * logf( fErrorLogPca );
			U32 sizeLogPca = numBitsFormatLogPca + numSamples*bitFormatLogPca.m_numBitsTotal;

			AnimChannelCompressionType compressionType = kAcctQuatUncompressed;
			U32 size = sizeUncompressed;
			if ((fErrorEstSmallest3 <= fErrorTolerance || fErrorEstSmallest3 <= fErrorUncompressed) && sizeSmallest3 < size)
				compressionType = kAcctQuatSmallestThree, size = sizeSmallest3;
			if ((fErrorEstLog <= fErrorTolerance || fErrorEstLog <= fErrorUncompressed) && sizeLog < size)
				compressionType = kAcctQuatLog,	size = sizeLog;
			if ((fErrorEstLogPca <= fErrorTolerance || fErrorEstLogPca <= fErrorUncompressed) && sizeLogPca < size)
				compressionType = kAcctQuatLogPca, size = sizeLogPca;
			INOTE(IMsg::kDebug, "%7.3f %7.3f %7.3f %2u %2u %2u %2u %6u%c %7.3f %7.3f %2u %2u %2u %2u %6u%c %7.3f %7.3f %2u %2u %2u %2u %6u%c %c %6u %s\n", fETLog2, 
				fErrorEstLog2Smallest3, fErrorLog2Smallest3, bitFormatSmallest3.m_numBitsTotal, bitFormatSmallest3.m_numBitsX, bitFormatSmallest3.m_numBitsY, bitFormatSmallest3.m_numBitsZ, sizeSmallest3, (fErrorEstSmallest3 <= fErrorTolerance)?'.':'x',
				fErrorEstLog2Log, fErrorLog2Log, bitFormatLog.m_numBitsTotal, bitFormatLog.m_numBitsX, bitFormatLog.m_numBitsY, bitFormatLog.m_numBitsZ, sizeLog, (fErrorEstLog <= fErrorTolerance)?'.':'x',
				fErrorEstLog2LogPca, fErrorLog2LogPca, bitFormatLogPca.m_numBitsTotal, bitFormatLogPca.m_numBitsX, bitFormatLogPca.m_numBitsY, bitFormatLogPca.m_numBitsZ, sizeLogPca, (fErrorEstLogPca <= fErrorTolerance)?'.':'x',
				(fErrorUncompressed <= fErrorTolerance)?'.':'x', size, ((compressionType==kAcctQuatUncompressed)?"uncompressed":(compressionType==kAcctQuatSmallestThree)?"smallest3":(compressionType==kAcctQuatLog)?"log":"logpca") );
			delete pCompressedSmallest3;
			delete pCompressedLog;
			delete pCompressedLogPca;
		}
	}
}

bool AnimClipCompression_GenerateBitFormats_Rotation(
	ClipCompressionDesc& compression,					// [input/output] compression structure to fill out with calculated bit formats
	AnimationHierarchyDesc const& hierarchyDesc,				// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors,		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	AnimChannelCompressionType compressionType,					// type of compression to apply (one of kAcctQuat*)
	std::set<size_t> const& targetJoints,						// set of animated joint indices to compress rotations for
	bool bSharedBitFormat)										// if true, one bit format must be chosen to be shared by all joints being compressed
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	bool bUnsharedKeyFrames = (compression.m_flags & kClipKeyCompressionMask) == kClipKeysUnshared;
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;

	std::vector<ChannelCompressionDesc>& formatQuat = compression.m_format[kChannelTypeRotation];

	if (formatQuat.empty()) {
		const ChannelCompressionDesc kCompression_None = { kAcctQuatUncompressed, 0, kVbpfNone };
		formatQuat.resize(numAnimatedJoints, kCompression_None);
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex ++) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				continue;
			}
			formatQuat[iAnimatedJoint].m_bitFormat = kVbpfQuatUncompressed;
		}
	}

	Vec3BitPackingFormat bitFormat;
	switch (compressionType) {
	case kAcctQuatUncompressed:
		bitFormat = kVbpfQuatUncompressed;
		break;
	case kAcctQuatSmallestThree:
	case kAcctQuatLog:
	case kAcctQuatLogPca:
		bitFormat = kVbpfNone;
		break;
	case kAcctQuatAuto:
		bitFormat = kVbpfNone;
		break;
	default:
		IWARN("AnimClipCompression_GenerateBitFormats_Rotation: rotation compression type %u is not supported yet - defaulting to uncompressed!", compressionType);
		compressionType = kAcctQuatUncompressed;
		bitFormat = kVbpfQuatUncompressed;
		break;
	}

#if DEBUG_PRINT_ANIM_ERROR_TABLES
	if (ShouldNote(IMsg::kDebug)) {
		DumpQuatAnimErrorTables(targetJoints, binding, sourceData);
	}
#endif

	bool const bCanCrossCorrelate = !bSharedBitFormat && !bUnsharedKeyFrames;
	if (bCanCrossCorrelate && (compressionType == kAcctQuatLog || compressionType == kAcctQuatLogPca || compressionType == kAcctQuatAuto))
	{
		unsigned numProcessingGroups = (unsigned)hierarchyDesc.m_channelGroups.size();

		auto targetJointIt = targetJoints.begin();
		size_t iAnimatedJoint = *targetJointIt;

		unsigned numJointsProcessed = 0;
		unsigned numAnimatedJointsInGroup = 0;
		for (unsigned iProcessingGroup = 0; iProcessingGroup < numProcessingGroups; ++iProcessingGroup, numJointsProcessed += numAnimatedJointsInGroup) {
			numAnimatedJointsInGroup = hierarchyDesc.m_channelGroups[iProcessingGroup].m_numAnimatedJoints;
			if (numAnimatedJointsInGroup == 0) {
				ITASSERT(numJointsProcessed == numAnimatedJoints);
				continue;
			}
			ITASSERT(iProcessingGroup+1 < numProcessingGroups || numJointsProcessed + numAnimatedJointsInGroup == numAnimatedJoints);
			unsigned iFirstAnimatedJointInGroup = hierarchyDesc.m_channelGroups[iProcessingGroup].m_firstAnimatedJoint;
			size_t iLastAnimatedJointInGroup = iFirstAnimatedJointInGroup + numAnimatedJointsInGroup ;
			ITASSERT(iFirstAnimatedJointInGroup == numJointsProcessed);

			// count how many target joints are in this processing group
			unsigned numAnimsInGroup = 0;
			for (auto countIt = targetJointIt; countIt != targetJoints.end(); countIt++) {
				if (iAnimatedJoint <= *countIt && *countIt < (iLastAnimatedJointInGroup)) {
					numAnimsInGroup++;
				}
			}

			if (compressionType == kAcctQuatAuto) 
			{
				if (numAnimsInGroup == 0) {
					continue;
				}

				// build array of anim index & bit compression local space error tolerances for each target in this group
				std::vector< std::pair<unsigned,float> > aJointAnimIndexAndTolerance;
				for (; targetJointIt != targetJoints.end() && *targetJointIt < iLastAnimatedJointInGroup; targetJointIt++) {
                    iAnimatedJoint = *targetJointIt;
					unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
					if (iJointAnimIndex >= numJointAnims) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u] is not animated; ignoring target...\n", iAnimatedJoint);
						continue;
					}
					if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u has constant value; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
						continue;
					}
					if (formatQuat[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u already has a defined bit format; discarding old format...\n", iAnimatedJoint, iJointAnimIndex);
					}
					float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnimIndex];
					aJointAnimIndexAndTolerance.push_back(std::pair<unsigned,float>(iJointAnimIndex, fErrorTolerance));
				}
				ITASSERTF(aJointAnimIndexAndTolerance.size() == numAnimsInGroup,("numAnimsInGroup=%d, ToleranceArraySize=%d, lastJointInGroup=%d",numAnimsInGroup,aJointAnimIndexAndTolerance.size(),iLastAnimatedJointInGroup));
				
				// now we generate the bit formats & cross correlation ordering for the target joint anims
				GenerateRotationAutoFormatsBitFormatsAndOrder(compression, binding, sourceData, aJointAnimIndexAndTolerance);
			} 
			else 
			{
				std::vector<RotationValues> aRotValues;
                for (; targetJointIt != targetJoints.end() && *targetJointIt < iLastAnimatedJointInGroup; targetJointIt++) {
                    iAnimatedJoint = *targetJointIt;
					unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
					if (iJointAnimIndex >= numJointAnims) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u] is not animated; ignoring target...\n", iAnimatedJoint);
						continue;
					}
					if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u has constant value; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
						continue;
					}
					if (formatQuat[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined) {
						IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u already has a defined bit format; discarding old format...\n", iAnimatedJoint, iJointAnimIndex);
					}
					ITGEOM::QuatArray rotationSamples;
					sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(rotationSamples);
					float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnimIndex];
					aRotValues.push_back(RotationValues(iJointAnimIndex, fErrorTolerance, compressionType));
					bool bSuccess = aRotValues.back().Init(rotationSamples, formatQuat[iAnimatedJoint].m_contextData);
					ITASSERT(bSuccess);
				}

				ITASSERT((unsigned)aRotValues.size() == numAnimsInGroup);
				if (numAnimsInGroup == 1) {
					// if there is only one joint animation, there can be no cross correlation
					unsigned iJointAnimIndex = aRotValues[0].m_iJointAnimIndex;
					unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
					formatQuat[iAnimatedJoint].m_compressionType = compressionType;
					formatQuat[iAnimatedJoint].m_bitFormat = aRotValues[0].m_formatNormalized;
					formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
				} else if (numAnimsInGroup > 1) {
					std::vector<std::pair<unsigned, Vec3BitPackingFormat> > aOrderAndFormat;
					unsigned numBitsSaved = FindRotationDataCrossCorrelationOrder(aRotValues, aOrderAndFormat);
					ITASSERT(aOrderAndFormat.size() == numAnimsInGroup);

					if (numBitsSaved > 0) {
						std::vector<unsigned>& jointAnimOrder = (compressionType == kAcctQuatLog) ? compression.m_jointAnimOrderQuatLog : compression.m_jointAnimOrderQuatLogPca;
						unsigned iJointAnimIndexFirst = (unsigned)-1;
						for (unsigned ii = 0; ii < numAnimsInGroup; ii++) {
							unsigned iRot = aOrderAndFormat[ii].first;
							unsigned iJointAnimIndex = aRotValues[iRot].m_iJointAnimIndex;
							unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
							Vec3BitPackingFormat const& bitFormatX = aOrderAndFormat[ii].second;
							// only store jointAnimOrder for cross correlated joints:
							if (bitFormatX.m_xAxis || bitFormatX.m_yAxis || bitFormatX.m_zAxis) {
								if (iJointAnimIndexFirst != (unsigned)-1) {
									jointAnimOrder.push_back(iJointAnimIndexFirst);
									iJointAnimIndexFirst = (unsigned)-1;
								}
								jointAnimOrder.push_back(iJointAnimIndex);
							} else {
								iJointAnimIndexFirst = iJointAnimIndex;
							}
							formatQuat[iAnimatedJoint].m_compressionType = compressionType;
							formatQuat[iAnimatedJoint].m_bitFormat = bitFormatX;
							formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
						}
					} else {
						// if no bits were saved by cross correlating, just output the full format and leave these joints in default order:
						for (unsigned iRot = 0; iRot < numAnimsInGroup; ++iRot) {
							unsigned iJointAnimIndex = aRotValues[iRot].m_iJointAnimIndex;
							unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
							formatQuat[iAnimatedJoint].m_compressionType = compressionType;
							formatQuat[iAnimatedJoint].m_bitFormat = aRotValues[iRot].m_formatNormalized;
							formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
						}
					}
				}
			}
		}
	}
	else
	{
		unsigned numChannelsTotal = 0;
		for (auto countIt = targetJoints.begin(); countIt != targetJoints.end(); countIt++) {
			if (0 <= *countIt && *countIt < numAnimatedJoints) {
				numChannelsTotal++;
			}
		}

		Vec3BitPackingFormat bitFormat_SharedSmallest3 = kVbpfNone;
		Vec3BitPackingFormat bitFormat_SharedLog = kVbpfNone;
		Vec3BitPackingFormat bitFormat_SharedLogPca = kVbpfNone;
		std::vector<U64_Array> aaContextDataLog((compressionType == kAcctQuatAuto && bSharedBitFormat) ? numChannelsTotal : 0);
		std::vector<U64_Array> aaContextDataLogPca((compressionType == kAcctQuatAuto && bSharedBitFormat) ? numChannelsTotal : 0);
		unsigned const numBitsFormatSmallest3 = GetFormatDataSize(kAcctQuatSmallestThree) * 8;
		unsigned const numBitsFormatLog = GetFormatDataSize(kAcctQuatLog) * 8;
		unsigned const numBitsFormatLogPca = GetFormatDataSize(kAcctQuatLogPca) * 8;
		bool bWithinErrorTolerance_SharedSmallest3 = true, bWithinErrorTolerance_SharedLog = true, bWithinErrorTolerance_SharedLogPca = true;

		if ((compressionType == kAcctQuatAuto) && !bSharedBitFormat) {
			INOTE(IMsg::kDebug, "kAcctQuatAuto compression total bit sizes by type for joints (%3u..%3u):\n", 0, numAnimatedJoints);
			INOTE(IMsg::kDebug, " anim joint  sm3    log    pca\n");
		}

		unsigned iAnim = 0;
		for (auto targetJointIt = targetJoints.begin(); targetJointIt != targetJoints.end(); targetJointIt++) {
			size_t iAnimatedJoint = *targetJointIt;
			if (iAnimatedJoint >= numAnimatedJoints) {
				break;
			}
			unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
			if (iJointAnimIndex >= numJointAnims) {
				IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u] is not animated; ignoring target...\n", iAnimatedJoint);
				continue;
			}
			SampledAnim* rotationAnim = sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
			if (!rotationAnim) {
				IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u has no sampled anim; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
				continue;
			}
			if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u has constant value; ignoring target...\n", iAnimatedJoint, iJointAnimIndex);
				continue;
			}
			if (formatQuat[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined) {
				IWARN("AnimClipCompression_GenerateBitFormats_Rotation: target ajoint[%u].rotation anim %u already has a defined bit format; discarding old format...\n", iAnimatedJoint, iJointAnimIndex);
			}
			ITGEOM::QuatArray rotationSamples; 
			rotationAnim->GetSamples(rotationSamples);
			if (compressionType == kAcctQuatAuto) {
				float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnimIndex];
				U64_Array aContextDataLog, aContextDataLogPca;
				float fErrorLogPca = 0.0f, fErrorLog = 0.0f, fErrorSmallest3 = 0.0f;
				Vec3BitPackingFormat bitFormatLogPca = ComputeBitFormatFromValues_Quat_LogPca(rotationSamples, fErrorTolerance, aContextDataLogPca, !bSharedBitFormat, &fErrorLogPca);
				Vec3BitPackingFormat bitFormatLog = ComputeBitFormatFromValues_Quat_Log(rotationSamples, fErrorTolerance, aContextDataLog, !bSharedBitFormat, &fErrorLog);
				Vec3BitPackingFormat bitFormatSmallest3 = ComputeBitFormatFromValues_Quat_SmallestThree(rotationSamples, fErrorTolerance, !bSharedBitFormat, &fErrorSmallest3);
				if (bSharedBitFormat) {
					bWithinErrorTolerance_SharedLogPca = bWithinErrorTolerance_SharedLogPca && fErrorLogPca <= fErrorTolerance;
					bWithinErrorTolerance_SharedLog = bWithinErrorTolerance_SharedLog && fErrorLog <= fErrorTolerance;
					bWithinErrorTolerance_SharedSmallest3 = bWithinErrorTolerance_SharedSmallest3 && fErrorSmallest3 <= fErrorTolerance;
					if (bitFormat_SharedLogPca.m_numBitsX < bitFormatLogPca.m_numBitsX) bitFormat_SharedLogPca.m_numBitsX = bitFormatLogPca.m_numBitsX;
					if (bitFormat_SharedLogPca.m_numBitsY < bitFormatLogPca.m_numBitsY) bitFormat_SharedLogPca.m_numBitsY = bitFormatLogPca.m_numBitsY;
					if (bitFormat_SharedLogPca.m_numBitsZ < bitFormatLogPca.m_numBitsZ) bitFormat_SharedLogPca.m_numBitsZ = bitFormatLogPca.m_numBitsZ;
					if (bitFormat_SharedLog.m_numBitsX < bitFormatLog.m_numBitsX) bitFormat_SharedLog.m_numBitsX = bitFormatLog.m_numBitsX;
					if (bitFormat_SharedLog.m_numBitsY < bitFormatLog.m_numBitsY) bitFormat_SharedLog.m_numBitsY = bitFormatLog.m_numBitsY;
					if (bitFormat_SharedLog.m_numBitsZ < bitFormatLog.m_numBitsZ) bitFormat_SharedLog.m_numBitsZ = bitFormatLog.m_numBitsZ;
					if (bitFormat_SharedSmallest3.m_numBitsX < bitFormatSmallest3.m_numBitsX) bitFormat_SharedSmallest3.m_numBitsX = bitFormatSmallest3.m_numBitsX;
					if (bitFormat_SharedSmallest3.m_numBitsY < bitFormatSmallest3.m_numBitsY) bitFormat_SharedSmallest3.m_numBitsY = bitFormatSmallest3.m_numBitsY;
					if (bitFormat_SharedSmallest3.m_numBitsZ < bitFormatSmallest3.m_numBitsZ) bitFormat_SharedSmallest3.m_numBitsZ = bitFormatSmallest3.m_numBitsZ;
					aaContextDataLog[iAnim] = aContextDataLog;
					aaContextDataLogPca[iAnim] = aContextDataLogPca;
					formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
				} else {
					float const fErrorUncompressed = CalculateQuatUncompressedError( rotationSamples );
					AnimChannelCompressionType compressionTypeAuto = kAcctQuatUncompressed;
					Vec3BitPackingFormat bitFormatAuto = kVbpfQuatUncompressed;
					unsigned numBitsTotal = (unsigned)kVbpfQuatUncompressed.m_numBitsTotal * numOutputFrames;
					unsigned numBitsTotalSmallest3 = numBitsFormatSmallest3 + bitFormatSmallest3.m_numBitsTotal * numOutputFrames;
					if ((fErrorSmallest3 <= fErrorTolerance || fErrorSmallest3 <= fErrorUncompressed) && numBitsTotalSmallest3 < numBitsTotal) {
						compressionTypeAuto = kAcctQuatSmallestThree;
						bitFormatAuto = bitFormatSmallest3;
						numBitsTotal = numBitsTotalSmallest3;
					}
					unsigned numBitsTotalLog = numBitsFormatLog + bitFormatLog.m_numBitsTotal * numOutputFrames;
					if ((fErrorLog <= fErrorTolerance || fErrorLog <= fErrorUncompressed) && numBitsTotalLog < numBitsTotal) {
						compressionTypeAuto = kAcctQuatLog;
						bitFormatAuto = bitFormatLog;
						numBitsTotal = numBitsTotalLog;
					}
					unsigned numBitsTotalLogPca = numBitsFormatLogPca + bitFormatLogPca.m_numBitsTotal * numOutputFrames;
					if ((fErrorLogPca <= fErrorTolerance|| fErrorLogPca <= fErrorUncompressed) && numBitsTotalLogPca < numBitsTotal) {
						compressionTypeAuto = kAcctQuatLogPca;
						bitFormatAuto = bitFormatLogPca;
						numBitsTotal = numBitsTotalLogPca;
					}
					formatQuat[iAnimatedJoint].m_compressionType = compressionTypeAuto;
					formatQuat[iAnimatedJoint].m_bitFormat = bitFormatAuto;
					if (compressionTypeAuto == kAcctQuatLog) {
						formatQuat[iAnimatedJoint].m_contextData = aContextDataLog;
					} else if (compressionTypeAuto == kAcctQuatLogPca) {
						formatQuat[iAnimatedJoint].m_contextData = aContextDataLogPca;
					}
					formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;

					INOTE(IMsg::kDebug, "%5u %5u %5x%c %5x%c %5x%c\n",
						iJointAnimIndex,				binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex],
						numBitsTotalSmallest3,			(compressionTypeAuto==kAcctQuatSmallestThree)?'*':' ',
						numBitsTotalLog,				(compressionTypeAuto==kAcctQuatLog)?'*':' ',
						numBitsTotalLogPca,				(compressionTypeAuto==kAcctQuatLogPca)?'*':' ');
				}
				iAnim++;
				continue;
			}
			formatQuat[iAnimatedJoint].m_compressionType = compressionType;
			if (!IsVariableBitPackedFormat(compressionType)) {
				formatQuat[iAnimatedJoint].m_bitFormat = bitFormat;
			} else {
				float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnimIndex];
				switch (compressionType) {
				case kAcctQuatSmallestThree:
					{
						// generate a bit packing format for each joint rotation based on range
						// calculate number of bits per channel from range
						if (bSharedBitFormat) {
							Vec3BitPackingFormat thisBitFormat = ComputeBitFormatFromValues_Quat_SmallestThree(rotationSamples, fErrorTolerance, false);
							if (bitFormat.m_numBitsX < thisBitFormat.m_numBitsX) bitFormat.m_numBitsX = thisBitFormat.m_numBitsX;
							if (bitFormat.m_numBitsY < thisBitFormat.m_numBitsY) bitFormat.m_numBitsY = thisBitFormat.m_numBitsY;
							if (bitFormat.m_numBitsZ < thisBitFormat.m_numBitsZ) bitFormat.m_numBitsZ = thisBitFormat.m_numBitsZ;
						} else {
							formatQuat[iAnimatedJoint].m_bitFormat = ComputeBitFormatFromValues_Quat_SmallestThree(rotationSamples, fErrorTolerance);
						}
					}
					break;
				case kAcctQuatLog:
					{
						// generate a bit packing format for each joint rotation based on range
						// calculate number of bits per channel from range
						if (bSharedBitFormat) {
							Vec3BitPackingFormat thisBitFormat = ComputeBitFormatFromValues_Quat_Log(rotationSamples, fErrorTolerance, formatQuat[iAnimatedJoint].m_contextData, false);
							if (bitFormat.m_numBitsX < thisBitFormat.m_numBitsX) bitFormat.m_numBitsX = thisBitFormat.m_numBitsX;
							if (bitFormat.m_numBitsY < thisBitFormat.m_numBitsY) bitFormat.m_numBitsY = thisBitFormat.m_numBitsY;
							if (bitFormat.m_numBitsZ < thisBitFormat.m_numBitsZ) bitFormat.m_numBitsZ = thisBitFormat.m_numBitsZ;
						} else {
							formatQuat[iAnimatedJoint].m_bitFormat = ComputeBitFormatFromValues_Quat_Log(rotationSamples, fErrorTolerance, formatQuat[iAnimatedJoint].m_contextData);
						}
					}
					break;
				case kAcctQuatLogPca:
					{
						// generate a bit packing format for each joint rotation based on range
						// calculate number of bits per channel from range
						if (bSharedBitFormat) {
							Vec3BitPackingFormat thisBitFormat = ComputeBitFormatFromValues_Quat_LogPca(rotationSamples, fErrorTolerance, formatQuat[iAnimatedJoint].m_contextData, false);
							if (bitFormat.m_numBitsX < thisBitFormat.m_numBitsX) bitFormat.m_numBitsX = thisBitFormat.m_numBitsX;
							if (bitFormat.m_numBitsY < thisBitFormat.m_numBitsY) bitFormat.m_numBitsY = thisBitFormat.m_numBitsY;
							if (bitFormat.m_numBitsZ < thisBitFormat.m_numBitsZ) bitFormat.m_numBitsZ = thisBitFormat.m_numBitsZ;
						} else {
							formatQuat[iAnimatedJoint].m_bitFormat = ComputeBitFormatFromValues_Quat_LogPca(rotationSamples, fErrorTolerance, formatQuat[iAnimatedJoint].m_contextData);
						}
					}
					break;
				default:
					ITASSERT(0);
					break;
				}
			}
			formatQuat[iAnimatedJoint].m_flags |= kFormatFlagBitFormatDefined;
			iAnim++;
		}
		//ITASSERT(iAnim == numChannelsTotal);

		if (bSharedBitFormat) {
			if (compressionType == kAcctVec3Auto) {
				unsigned numBitsTotal;
				compressionType = kAcctQuatUncompressed;
				bitFormat = kVbpfQuatUncompressed;
				numBitsTotal = bitFormat.m_numBitsTotal * numOutputFrames * numChannelsTotal;

				if (bWithinErrorTolerance_SharedLogPca)
				{
					bWithinErrorTolerance_SharedLogPca = !NormalizeBitFormat(bitFormat_SharedLogPca, 0);
					unsigned numBitsTotalLogPca = (numBitsFormatSmallest3 + bitFormat_SharedLogPca.m_numBitsTotal * numOutputFrames) * numChannelsTotal;
					if (bWithinErrorTolerance_SharedLogPca && numBitsTotalLogPca < numBitsTotal) {
						compressionType = kAcctQuatLogPca;
						bitFormat = bitFormat_SharedLogPca;
						numBitsTotal = numBitsTotalLogPca;
					}
				}
				if (bWithinErrorTolerance_SharedLog)
				{
					bWithinErrorTolerance_SharedLog = !NormalizeBitFormat(bitFormat_SharedLog, 0);
					unsigned numBitsTotalLog = (numBitsFormatLog + bitFormat_SharedLog.m_numBitsTotal * numOutputFrames) * numChannelsTotal;
					if (bWithinErrorTolerance_SharedLog && numBitsTotalLog < numBitsTotal) {
						compressionType = kAcctQuatLog;
						bitFormat = bitFormat_SharedLog;
						numBitsTotal = numBitsTotalLog;
					}
				}
				if (bWithinErrorTolerance_SharedSmallest3)
				{
					bWithinErrorTolerance_SharedSmallest3 = !NormalizeBitFormat(bitFormat_SharedSmallest3, 0);
					unsigned numBitsTotalSmallest3 = (numBitsFormatSmallest3 + bitFormat_SharedSmallest3.m_numBitsTotal * numOutputFrames) * numChannelsTotal;
					if (bWithinErrorTolerance_SharedSmallest3 && numBitsTotalSmallest3 < numBitsTotal) {
						compressionType = kAcctQuatSmallestThree;
						bitFormat = bitFormat_SharedSmallest3;
						numBitsTotal = numBitsTotalSmallest3;
					}
				}

				unsigned iAnim = 0;
				for (auto targetJointIt = targetJoints.begin(); targetJointIt != targetJoints.end(); targetJointIt++) {
					size_t iAnimatedJoint = *targetJointIt;
					if (iAnimatedJoint >= numAnimatedJoints) {
						break;
					}
					unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
					if (iJointAnimIndex >= numJointAnims) {
						continue;
					}
					if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
						continue;
					}
					formatQuat[iAnimatedJoint].m_compressionType = compressionType;
					formatQuat[iAnimatedJoint].m_bitFormat = bitFormat;
					if (compressionType == kAcctQuatLog) {
						formatQuat[iAnimatedJoint].m_contextData = aaContextDataLog[iAnim];
					} else if (compressionType == kAcctQuatLogPca) {
						formatQuat[iAnimatedJoint].m_contextData = aaContextDataLogPca[iAnim];
					}
					iAnim++;
				}
				ITASSERT(iAnim == numChannelsTotal);
			} else {
				NormalizeBitFormat(bitFormat, compressionType == kAcctQuatSmallestThree ? 2 : 0);
				for (auto targetJointIt = targetJoints.begin(); targetJointIt != targetJoints.end(); targetJointIt++) {
					size_t iAnimatedJoint = *targetJointIt;
					if (iAnimatedJoint >= numAnimatedJoints) {
						break;
					}
					unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
					if (iJointAnimIndex >= numJointAnims) {
						continue;
					}
					if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
						continue;
					}
					formatQuat[iAnimatedJoint].m_bitFormat = bitFormat;
				}
			}
		}
	}
	return true;
}

bool AnimClipCompression_GenerateBitFormats_Float(
	ClipCompressionDesc& compression,					// [input/output] compression structure to fill out with calculated bit formats
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors,		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	AnimChannelCompressionType compressionType,					// type of compression to apply (one of kAcctFloat*)
	std::set<size_t> const& targetFloatChannels,				// set of float channel indices to compress
	bool bSharedBitFormat)										// if true, one bit format must be chosen to be shared by all joints being compressed
{
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	std::vector<ChannelCompressionDesc>& formatFloat = compression.m_format[kChannelTypeScalar];

	if (formatFloat.empty()) {
		const ChannelCompressionDesc kCompression_None = { kAcctFloatUncompressed, 0, kVbpfNone };
		formatFloat.resize(numFloatChannels, kCompression_None);
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex ++) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			formatFloat[iFloatChannel].m_bitFormat = kVbpfFloatUncompressed;
		}
	}

	Vec3BitPackingFormat bitFormat;
	switch (compressionType) {
	case kAcctFloatAuto:
		//NOTE: as we currently only support one float format (uncompressed), auto -> uncompressed always:
		compressionType = kAcctFloatUncompressed;
	case kAcctFloatUncompressed:
		bitFormat = kVbpfFloatUncompressed;
		break;
	default:
		IWARN("AnimClipCompression_GenerateBitFormats_Float: float compression type %u is not supported yet - defaulting to uncompressed!", compressionType);
		compressionType = kAcctFloatUncompressed;
		bitFormat = kVbpfFloatUncompressed;
		break;
	}

	for (std::set<size_t>::const_iterator targetFloatChannelsIt = targetFloatChannels.begin(); targetFloatChannelsIt != targetFloatChannels.end(); targetFloatChannelsIt++) {
		size_t iFloatChannel = *targetFloatChannelsIt;
		if (iFloatChannel >= numFloatChannels)
			break;
		unsigned iFloatAnimIndex = (unsigned)binding.m_floatIdToFloatAnimId[iFloatChannel];
		if (iFloatAnimIndex >= numFloatAnims) {
			IWARN("AnimClipCompression_GenerateBitFormats_Float: target floatchannel[%u] is not animated; ignoring target...\n", iFloatChannel);
			continue;
		}
		if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
			IWARN("AnimClipCompression_GenerateBitFormats_Float: target floatchannel[%u] anim %u has constant value; ignoring target...\n", iFloatChannel, iFloatAnimIndex);
			continue;
		}
		if (formatFloat[iFloatChannel].m_flags & kFormatFlagBitFormatDefined) {
			IWARN("AnimClipCompression_GenerateBitFormats_Float: target floatchannel[%u] anim %u already has a defined bit format; discarding old format...\n", iFloatChannel, iFloatAnimIndex);
		}
		formatFloat[iFloatChannel].m_compressionType = compressionType;
		if (!IsVariableBitPackedFormat(compressionType)) {
			formatFloat[iFloatChannel].m_bitFormat = bitFormat;
		} else {
			(void)localSpaceErrors;
/*			float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeScalar][iFloatAnimIndex];
			switch (compressionType) {
			case kAcctFloat...:
				break;
			}
*/
		}
		formatFloat[iFloatChannel].m_flags |= kFormatFlagBitFormatDefined;
	}

	if (bSharedBitFormat) {
		NormalizeFloatBitFormat(bitFormat);
		for (std::set<size_t>::const_iterator targetFloatChannelsIt = targetFloatChannels.begin(); targetFloatChannelsIt != targetFloatChannels.end(); targetFloatChannelsIt++) {
			size_t iFloatChannel = *targetFloatChannelsIt;
			if (iFloatChannel >= numFloatChannels) {
				break;
			}
			unsigned iFloatAnimIndex = (unsigned)binding.m_floatIdToFloatAnimId[iFloatChannel];
			if (iFloatAnimIndex >= numFloatAnims) {
				continue;	// not animated
			}
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			formatFloat[iFloatChannel].m_bitFormat = bitFormat;
		}
	}
	return true;
}

bool AnimClipCompression_GenerateSkipFrames_Shared(
	ClipCompressionDesc& compression,					// [input/output] compression description to fill out
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	)
{
	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);
	if (keyCompression != kClipKeysShared) {
		ITASSERT(keyCompression == kClipKeysShared);
		return false;
	}
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	if (numOutputFrames <= 2) {
		IWARN("AnimClipCompression_GenerateSkipFrames_Shared: animation has only %u frames (<=2); reverting to uniform keys...\n", numOutputFrames);
		compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
		return true;
	}

	// If this clip is looping, we're allowed to skip the last frame if it is linear between the second to last frame
	// and the first frame, knowing that a copy of the first frame will be appended.
	unsigned const frameMod = sourceData.m_numFrames;

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	ITASSERT(	localSpaceErrors.m_key[kChannelTypeScale].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeRotation].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeTranslation].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeScalar].size() == numFloatAnims );

	// generate skip frames across all channels based on error tolerance
	unsigned iFrame0 = 0, iFrame1;
	std::set<size_t>& skipFrames = compression.m_sharedSkipFrames;
	skipFrames.clear();

	// check we have error tolerances at each frame for all channels unless the channel is constant
	{
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex ++) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			ITASSERT(sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex) ||
				localSpaceErrors.m_key[kChannelTypeScale][iJointAnimIndex].size() == sourceData.m_numFrames);
			ITASSERT(sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex) ||
				localSpaceErrors.m_key[kChannelTypeRotation][iJointAnimIndex].size() == sourceData.m_numFrames);
			ITASSERT(sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex) ||
				localSpaceErrors.m_key[kChannelTypeTranslation][iJointAnimIndex].size() == sourceData.m_numFrames);
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex ++) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			ITASSERT(sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex) ||
				localSpaceErrors.m_key[kChannelTypeScalar][iFloatAnimIndex].size() == sourceData.m_numFrames);
		}
	}

	for (iFrame1 = 2; iFrame1 < numOutputFrames; iFrame1++) {
		float tPerFrame = 1.0f / (float)(iFrame1 - iFrame0);
		unsigned iFrame1mod = iFrame1 % frameMod;
		// Test all frames between iFrame0 and iFrame1 to see if the error introduced by lerping between them
		// compared to the real sample is within our tolerance.  We check in reverse order, since the high end
		// is most likely to fail if we have a new iFrame1 point that may have diverged from a previously linear
		// chain of frames.
		for (unsigned iFrame = iFrame1-1; iFrame > iFrame0; iFrame--) {
			float t = tPerFrame * (float)(iFrame - iFrame0);

			// We must check if any joint parameter will go out of our tolerance if we remove the chain of
			// frames between iFrame0 and iFrame1:
			bool bOutOfTolerance = false;
			for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex ++) {
				unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
				if (iAnimatedJoint >= numAnimatedJoints) {
					continue;
				}
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
					SampledAnim const& values = *sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
					float fErrorTolerance = localSpaceErrors.m_key[kChannelTypeScale][iJointAnimIndex][iFrame];
					ITGEOM::Vec3 vLerp = ITGEOM::Lerp((ITGEOM::Vec3&)values[iFrame0], values[iFrame1mod], t);
					float fError = ComputeError_Vec3( vLerp, values[iFrame] );
					bOutOfTolerance = (fError > fErrorTolerance);
					if (bOutOfTolerance) {
						INOTE(IMsg::kDebug, "Scale: frame=%2d, animIndex=%d, error=%f, tolerance=%f\n", iFrame, iJointAnimIndex, fError, fErrorTolerance);
						break;
					}
				}
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
					SampledAnim const& values = *sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
					float fErrorTolerance = localSpaceErrors.m_key[kChannelTypeRotation][iJointAnimIndex][iFrame];
					ITGEOM::Quat vLerp = ITGEOM::Slerp(values[iFrame0], values[iFrame1mod], t);
					float fError = ComputeError_Quat( vLerp, values[iFrame] );
					bOutOfTolerance = (fError > fErrorTolerance);
					if (bOutOfTolerance) {
						INOTE(IMsg::kDebug, "Rotation: frame=%2d, animIndex=%d, error=%f, tolerance=%f\n", iFrame, iJointAnimIndex, fError, fErrorTolerance);
						break;
					}
				}
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
					SampledAnim const& values = *sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
					float fErrorTolerance = localSpaceErrors.m_key[kChannelTypeTranslation][iJointAnimIndex][iFrame];
					ITGEOM::Vec3 vLerp = ITGEOM::Lerp((ITGEOM::Vec3&)values[iFrame0], values[iFrame1mod], t);
					float fError = ComputeError_Vec3( vLerp, values[iFrame] );
					bOutOfTolerance = (fError > fErrorTolerance);
					if (bOutOfTolerance) {
						INOTE(IMsg::kDebug, "Translation: frame=%2d, animIndex=%d, error=%f, tolerance=%f\n", iFrame, iJointAnimIndex, fError, fErrorTolerance);
						break;
					}
				}
			}
			for (unsigned iFloatAnimIndex = 0; !bOutOfTolerance && iFloatAnimIndex < numFloatAnims; iFloatAnimIndex ++) {
				unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
				if (iFloatChannel >= numFloatChannels) {
					continue;
				}
				if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
					continue;
				}
				SampledAnim const& values = *sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
				float fErrorTolerance = localSpaceErrors.m_key[kChannelTypeScalar][iFloatAnimIndex][iFrame];
				float fLerp = values[iFrame0] * (1.0f - t) + values[iFrame1mod] * t;
				float fError = ComputeError_Float(fLerp, values[iFrame] );
				bOutOfTolerance = (fError > fErrorTolerance);
				if (bOutOfTolerance) {
					INOTE(IMsg::kDebug, "Scalar: frame=%2d, animIndex=%d, error=%f, tolerance=%f\n", iFrame, iFloatAnimIndex, fError, fErrorTolerance);
					break;
				}
			}
			if (bOutOfTolerance) {
				// add skip frames between iFrame0 and previous iFrame1, if any
				for (unsigned i = iFrame0+1; i < iFrame1-1; i++) {
					skipFrames.emplace(i);
				}
				// reset anchor point to end of last skip chain:
				iFrame0 = iFrame1-1;
				break;
			}
		}
	}
	// add skip frames between iFrame0 and last frame, if any
	for (unsigned i = iFrame0+1; i < iFrame1-1; i++) {
		skipFrames.emplace(i);
	}
	return true;
}

// search for linearity in channel animation
template<typename ValueType, typename LerpFunc, typename ErrorFunc>
void GenerateUnsharedChannelSkipFrames( 
	ClipCompressionDesc& compression, 
	ChannelType	const chanType,
	unsigned const iAnimatedType,				// index of animated joint or float
	unsigned const numOutputFrames, 
	unsigned const frameMod,
	std::vector<float> const& errorTolerances,	// [numFrames]
	std::vector<ValueType> const& values,
	LerpFunc Lerp,
	ErrorFunc ComputeError )
{
	std::set<size_t>& skipFrames = compression.m_unsharedSkipFrames[chanType][iAnimatedType];
	if (compression.m_format[chanType][iAnimatedType].m_flags & kFormatFlagKeyFramesDefined) {
		IWARN("GenerateUnsharedChannelSkipFrames: WARNING: target[%u] track[%u] already has a defined skip list; discarding old skip list...\n", iAnimatedType, chanType);
	}
	skipFrames.clear();
	compression.m_format[chanType][iAnimatedType].m_flags |= kFormatFlagKeyFramesDefined;

	unsigned iFrame0 = 0, iFrame1;
	ValueType v0 = values[iFrame0];
	for (iFrame1 = 2; iFrame1 < numOutputFrames; iFrame1++) {
		float tPerFrame = 1.0f / (float)(iFrame1 - iFrame0);
		ValueType v1 = values[iFrame1 % frameMod];
		//FIXME: we could track 2 early out extrapolated spheres (for quick linear/non-linear rejection),
		// to reduce the O[N^2] behavior further.
		// Test all frames between iFrame0 and iFrame1 to see if the error introduced by lerping between them
		// compared to the real sample is within our tolerance.  We check in reverse order, since the high end
		// is most likely to fail if we have a new iFrame1 point that may have diverged from a previously linear
		// chain of frames.
		for (unsigned iFrame = iFrame1-1; iFrame > iFrame0; iFrame--) {
			float t = tPerFrame * (float)(iFrame - iFrame0);
			ValueType vLerp = Lerp(v0, v1, t);
			float fError = ComputeError( vLerp, values[iFrame] );
			if (fError > errorTolerances[iFrame]) {
				// add skip frames between iFrame0 and previous iFrame1, if any
				for (unsigned i = iFrame0+1; i < iFrame1-1; i++) {
					skipFrames.emplace(i);
				}
				// reset anchor point to end of last skip chain:
				iFrame0 = iFrame1-1;
				v0 = values[iFrame0];
				break;
			} else if ((fError == 0.0f) && (iFrame == iFrame1-1)) {
				//quick out to avoid O[N^2] behavior where possible:
				// If frame (iFrame1-1) is exactly linear with the previous chain of frames, we know all the other frames
				// between will still be within our error bars.
				break;
			}
		}
		// if we tested all intermediate values without error, increment iFrame1 and test for an even longer chain,
		// otherwise, increment iFrame1 to iFrame0+2 to start testing a new chain where the last left off.
	}
	// add skip frames between iFrame0 and last frame, if any
	for (unsigned i = iFrame0+1; i < iFrame1-1; i++) {
		skipFrames.emplace(i);
	}
}

// for each channel, determine which key frames can be removed (skipped) by interpolating within the given error tolerance
bool AnimClipCompression_GenerateSkipFrames_Unshared(
	ClipCompressionDesc& compression,					// [input/output] compression description to fill out
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors,		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	GenerateSkipFrameIndices const& genSkipFrames )				// set of animated joint & float channel indices to generate skip frames for
{
	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);
	if (keyCompression != kClipKeysUnshared) {
		ITASSERT(keyCompression == kClipKeysUnshared);
		return false;
	}
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	if (numOutputFrames <= 2) {
		IWARN("AnimClipCompression_GenerateSkipFrames_Unshared: animation has only %u frames (<=2); reverting to uniform keys...\n", numOutputFrames);
		compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
		return true;
	}

	// If this clip is looping, we're allowed to skip the last frame if it is linear between the second to last frame
	// and the first frame, knowing that a copy of the first frame will be appended.
	unsigned const frameMod = sourceData.m_numFrames;

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	ITASSERT(	localSpaceErrors.m_key[kChannelTypeScale].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeRotation].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeTranslation].size() == numJointAnims &&
				localSpaceErrors.m_key[kChannelTypeScalar].size() == numFloatAnims );

	// generate skip frames per joint and float channel based on error tolerance
	compression.m_unsharedSkipFrames[kChannelTypeScale].resize(numAnimatedJoints);
	compression.m_unsharedSkipFrames[kChannelTypeRotation].resize(numAnimatedJoints);
	compression.m_unsharedSkipFrames[kChannelTypeTranslation].resize(numAnimatedJoints);
	compression.m_unsharedSkipFrames[kChannelTypeScalar].resize(numFloatChannels);

	// search for linearity in joint channels
	auto Lerp_Vec3 = [] (ITGEOM::Vec3 const& v0, ITGEOM::Vec3 const& v1, float t) { return ITGEOM::Lerp(v0,v1,t); };
	auto Slerp_Quat = [] (ITGEOM::Quat const& q0, ITGEOM::Quat const& q1, float t) { return ITGEOM::Slerp(q0,q1,t); };
	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex++) {
		unsigned const iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (genSkipFrames.has(kChannelTypeScale, iAnimatedJoint)) {
			if (sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateSkipFrames_Unshared: WARNING: target ajoint[%u].scale track is constant; ignoring target...\n", iAnimatedJoint);
			} else {
				ITGEOM::Vec3Array samples; 
				sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex)->GetSamples(samples);
				GenerateUnsharedChannelSkipFrames(compression, kChannelTypeScale, iAnimatedJoint, numOutputFrames, frameMod, 
					localSpaceErrors.m_key[kChannelTypeScale][iJointAnimIndex], samples, Lerp_Vec3, ComputeError_Vec3 );
			}
		}
		if (genSkipFrames.has(kChannelTypeRotation, iAnimatedJoint)) {
			if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateSkipFrames_Unshared: WARNING: target ajoint[%u].rotation track is constant; ignoring target...\n", iAnimatedJoint);
			} else {
				ITGEOM::QuatArray samples; 
				sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)->GetSamples(samples);
				GenerateUnsharedChannelSkipFrames(compression, kChannelTypeRotation, iAnimatedJoint, numOutputFrames, frameMod, 
					localSpaceErrors.m_key[kChannelTypeRotation][iJointAnimIndex], samples, Slerp_Quat, ComputeError_Quat );

			}
		}
		if (genSkipFrames.has(kChannelTypeTranslation, iAnimatedJoint)) {
			if (sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				IWARN("AnimClipCompression_GenerateSkipFrames_Unshared: WARNING: target ajoint[%u].translation track is constant; ignoring target...\n", iAnimatedJoint);
			} else {
				ITGEOM::Vec3Array samples; 
				sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex)->GetSamples(samples);
				GenerateUnsharedChannelSkipFrames(compression, kChannelTypeTranslation, iAnimatedJoint, numOutputFrames, frameMod, 
					localSpaceErrors.m_key[kChannelTypeTranslation][iJointAnimIndex], samples, Lerp_Vec3, ComputeError_Vec3 );
			}
		}
	}
	// search for linearity in float channels
	auto Lerp_Float = [] (float const& f0, float const& f1, float t) { return f0 * (1.0f - t) +  f1 * t; };
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex++) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (genSkipFrames.has(kChannelTypeScalar, iFloatChannel)) {
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				IWARN("AnimClipCompression_GenerateSkipFrames_Unshared: WARNING: target floatchannel[%u] track is constant; ignoring target...\n", iFloatChannel);
			} else {
				std::vector<float> samples; 
				sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex)->GetSamples(samples);
				GenerateUnsharedChannelSkipFrames(compression, kChannelTypeScalar, iFloatChannel, numOutputFrames, frameMod, 
					localSpaceErrors.m_key[kChannelTypeScalar][iFloatAnimIndex], samples, Lerp_Float, ComputeError_Float );

			}
		}
	}
	return true;
}

//------------------------------------------------------------------------------------------------
// private functions to support AnimClipCompression_GenerateLocalSpaceErrors

struct JointErrorToleranceLimiter 
{
	enum Type { 
		kTypeError, 
		kTypeDelta, 
		kTypeObjectRootDelta, 
		kTypeSpeed, 
		kTypeObjectRootSpeed, 
		kTypeTranslationInherited,
		kTypeTranslationError, 
		kTypeTranslationDelta, 
		kTypeTranslationObjectRootDelta,
		kTypeTranslation = kTypeTranslationError, 
		kTypeFlagGlobal = 0x8000, 
		kTypeFlagGlobalRadial = 0x4000, 
		kTypeMaskFlags = 0xC000, 
		kTypeInvalid = (0xFFFF&~kTypeMaskFlags)  
	};

	unsigned short	m_eType;
	unsigned short	m_iJoint;
	float			m_fValue;	// either delta, or distance

	JointErrorToleranceLimiter() 
		: m_eType(kTypeInvalid) 
	{
	}
	
	JointErrorToleranceLimiter(Type eType, unsigned iJoint, float fValue = 0.0f) 
		: m_eType((unsigned short)eType)
		, m_iJoint((unsigned short)iJoint)
		, m_fValue(fValue) 
	{
	}
};

// Per-frame error tolerances for all joints with key frame data
struct JointErrorTolerances 
{
	std::vector< std::vector<float> > scale;		//[iJointAnimIndex][iFrame]
	std::vector< std::vector<float> > rotation;		//[iJointAnimIndex][iFrame]
	std::vector< std::vector<float> > translation;	//[iJointAnimIndex][iFrame]

	std::vector< std::vector<JointErrorToleranceLimiter> >	scale_limiter;			//[iJointAnimIndex][iFrame]
	std::vector< std::vector<JointErrorToleranceLimiter> >	rotation_limiter;		//[iJointAnimIndex][iFrame]
	std::vector< std::vector<JointErrorToleranceLimiter> >	translation_limiter;	//[iJointAnimIndex][iFrame]

	JointErrorTolerances() {}
	
	JointErrorTolerances(size_t numJointAnims, bool bVerboseRootSpaceErrorOutput = false) 
	{ 
		resize(numJointAnims, bVerboseRootSpaceErrorOutput); 
	}
	
	void resize(size_t numJointAnims, bool bVerboseRootSpaceErrorOutput = false) 
	{
		scale.resize(numJointAnims), rotation.resize(numJointAnims), translation.resize(numJointAnims);
		if (bVerboseRootSpaceErrorOutput) {
			scale_limiter.resize(numJointAnims), rotation_limiter.resize(numJointAnims), translation_limiter.resize(numJointAnims);
		}
	}
};

#define ERROR_TOLERANCE_UNDEFINED FLT_MAX
#define ERROR_TOLERANCE_MAX 3.402823264e+38f	//== AsFloat( AsU32(FLT_MAX)-1 )

struct ErrorTolerance
{
	ErrorTolerance() : m_tolerance(ERROR_TOLERANCE_UNDEFINED) {}
	
	float m_tolerance;
	JointErrorToleranceLimiter m_limiter;
};

struct JointErrorTolerance
{
	ErrorTolerance m_scale;
	ErrorTolerance m_rotate;
	ErrorTolerance m_translate;
};

struct ParentJointError
{
	ITGEOM::Vec3 m_vTranslation;
	float m_fScaleError;
	float m_fRotationError;
	float m_fTranslationError;

	ParentJointError(ITGEOM::Vec3 const& vTranslation, float fScaleError, float fRotationError, float fTranslationError) 
		: m_vTranslation(vTranslation)
		, m_fScaleError(fScaleError)
		, m_fRotationError(fRotationError)
		, m_fTranslationError(fTranslationError) 
	{
	}
};

/* 
	The job of LocalSpaceErrorToleranceCalculator is to convert input root space error tolerances supplied by the user 
	into joint local space error tolerances that can be used to compress individual joint animation channels.

	This is basically done in 3 steps:- (see GenerateLocalSpaceErrorTolerances)

	1. Calculate the root space errors for this frame based on deltas from the previous and next frame.
	2. Calculate the maximum error tolerance each joint parameter could have based on root space limits of all children of that joint.
	3. Traverse the joints again, adjusting errors so that each joint's root space error (calculated from the sum of all
		local space errors of parent joints) is less than the allowed root space error tolerance for that joint.
*/
class LocalSpaceErrorToleranceCalculator
{
public:
	LocalSpaceErrorToleranceCalculator(std::vector<Joint> const& joints,
									   AnimationBinding const& binding,
									   AnimationClipSourceData const& sourceData,
									   std::vector<JointRootSpaceError> const& jointErrorDescs,
									   JointErrorTolerances& jointErrorTolerance,
									   bool bCollectLimitterData )
		: m_joints(joints)
		, m_binding(binding)
		, m_sourceData(sourceData)
		, m_jointErrorDescs(jointErrorDescs)
		, m_jointErrorTolerance(jointErrorTolerance)
		, m_jointRootSpaceErrorTolerances(joints.size())
		, m_translationErrorTolerances(joints.size())
		, m_amMatrixCache( 3*(1+joints.size()) )
		, m_bCollectLimitterData(bCollectLimitterData)
	{
		m_pmRootSpacePose[0] = m_pmRootSpacePose[1] = m_pmRootSpacePose[2] = NULL;
		m_pmObjectRoot[0] = m_pmObjectRoot[1] = m_pmObjectRoot[2] = NULL;
		m_aiCacheFrame[0] = m_aiCacheFrame[1] = m_aiCacheFrame[2] = (unsigned)-1;
		m_aiFrame[0] = m_aiFrame[1] = m_aiFrame[1] = (unsigned)-1;
	}

	void GeneratePosesForFrames(unsigned iFramePrev, unsigned iFrame, unsigned iFrameNext);
	void GenerateLocalSpaceErrorTolerances();

private:
	// non-copyable
	LocalSpaceErrorToleranceCalculator(LocalSpaceErrorToleranceCalculator const& copy);
	LocalSpaceErrorToleranceCalculator& operator=(LocalSpaceErrorToleranceCalculator const&);

	void GeneratePoseRecursive(SMath::Mat44* pmRootSpacePose, SMath::Vector const& vScaleParent, unsigned iFrame, unsigned iJoint);
	void GeneratePose(SMath::Mat44* pmRootSpacePose, unsigned iFrame);
	void GenerateObjectRoot(SMath::Mat44* pmObjectRoot, unsigned iFrame);
	void GenerateRootSpaceErrors();
	void GenerateMaxLocalSpaceErrorsRecursive(JointErrorTolerance& parentErrorTolerance, unsigned iJoint, bool bParentNeedsData);
	void GetMinRadialErrorToleranceRecursive(ErrorTolerance& radialErrorTolerance, SMath::Point const& vParentTranslation, unsigned iJoint);
	void GenerateLocalSpaceErrorsForJointRecursive(int iJointAnimIndex0, std::vector<ParentJointError> const& finalizedParentJointErrors, std::vector<ParentJointError>& parentJointErrors, unsigned iJoint);
	void GenerateLocalSpaceErrorsRecursive( unsigned iJoint, std::vector<ParentJointError>& finalizedParentJointErrors );

	// inputs
	std::vector<Joint> const& m_joints;
	AnimationBinding const& m_binding;
	AnimationClipSourceData const& m_sourceData;
	std::vector<JointRootSpaceError> const& m_jointErrorDescs;	// user supplied root space descriptors [m_joints.size()]

	// output
	JointErrorTolerances& m_jointErrorTolerance;							// local space tolerances [

	// temporary working variables
	std::vector<JointErrorTolerance> m_jointRootSpaceErrorTolerances;		// root space tolerances [m_joints.size()]
	std::vector<ErrorTolerance> m_translationErrorTolerances;				// for calculating radial error tolerances [m_joints.size()]
	AlignedArray<SMath::Mat44> m_amMatrixCache;								// [3*(1+numJoints)] includes 3 object roots, 3 poses
	unsigned m_aiCacheFrame[3];												// [cacheIndex(0..2)]
	unsigned m_aiFrame[3];													// [framePos(0..2)]
	SMath::Mat44 const* m_pmObjectRoot[3];									// [framePos(0..2)]
	SMath::Mat44 const* m_pmRootSpacePose[3];								// [framePos(0..2)][numJoints]
	bool m_bCollectLimitterData;
};

static void GetLocalJointParams(SMath::Vector& vScale, SMath::Quat& qRotation, SMath::Point& vTranslation, Joint const& joint, AnimationClipSourceData const& sourceData, int iJointAnimIndex, unsigned iFrame)
{
	if (iJointAnimIndex < 0) {
		// use bind pose if no animation
		vScale = SMathVector( joint.m_jointScale );
		qRotation = SMathQuat( joint.m_jointQuat );
		vTranslation = SMathPoint( joint.m_jointTranslation );
	} else {
		// else use animation at frame iFrame
		vScale = SMathVector( (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[iFrame] );
		qRotation = SMathQuat( (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[iFrame] );
		vTranslation = SMathPoint( (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[iFrame] );
	}
}

static const SMath::Vector kvScaleIdentity(1.0f, 1.0f, 1.0f);
void LocalSpaceErrorToleranceCalculator::GeneratePoseRecursive(SMath::Mat44* pmRootSpacePose, SMath::Vector const& vScaleParent, unsigned iFrame, unsigned iJoint)
{
	// calculate the concatenation of this joint matrix with it's parent
	Joint const& joint = m_joints[iJoint];
	int iJointAnimIndex = iJoint < m_binding.m_jointIdToJointAnimId.size() ? m_binding.m_jointIdToJointAnimId[ iJoint ] : -1;

	SMath::Vector vScale;
	SMath::Quat qRotation;
	SMath::Point vTranslation;
	GetLocalJointParams(vScale, qRotation, vTranslation, joint, m_sourceData, iJointAnimIndex, iFrame);

	// calculate our root space matrix minus local scale
	bool bSegmentScaleCompensate = (joint.m_flags & kJointSegmentScaleCompensate) != 0;
	if (bSegmentScaleCompensate) {
		// If segment scale compensate is enabled, our parent's local scale applies only to our translation
		pmRootSpacePose[iJoint] = SMath::BuildTransform( qRotation, SMath::Point(vTranslation.GetVec4() * vScaleParent.GetVec4()).GetVec4() );	// translation.w = 1.0f
	} else {
		// else our parent's local scale applies normally.
		pmRootSpacePose[iJoint] = SMath::BuildTransform( qRotation, vTranslation.GetVec4() );
		if (!SMath::AllComponentsEqual(vScaleParent, kvScaleIdentity)) {
			SMath::Mat44 mScaleParent;
			mScaleParent.SetScale( SMath::Point(vScaleParent.GetVec4()).GetVec4() );	// scale.w = 1.0f
			pmRootSpacePose[iJoint] *= mScaleParent;
		}
	}
	if (joint.m_parent != -1) {
		pmRootSpacePose[iJoint] *= pmRootSpacePose[joint.m_parent];
	}

	// Now recurse to all children of this joint, passing in our local scale so each child can decide whether to apply it (based on segment scale compensate)
	for (int iChild = joint.m_child; iChild != -1; iChild = m_joints[iChild].m_sibling) {
		GeneratePoseRecursive(pmRootSpacePose, vScale, iFrame, (unsigned)iChild);
	}

	// finalize our matrix by pre-multiplying in our scale
	if (!SMath::AllComponentsEqual(vScale, kvScaleIdentity)) {
		SMath::Mat44 mScale;
		mScale.SetScale( SMath::Point(vScale.GetVec4()).GetVec4() );	// scale.w = 1.0f
		pmRootSpacePose[iJoint] = mScale * pmRootSpacePose[iJoint];
	}
	
	// if segment scale compensate:	pmRootSpacePose[iJoint] = mScale * mRotation * mScaleParent.Inverse() * mTranslation * pmRootSpacePose[iParent]
	// else:						pmRootSpacePose[iJoint] = mScale * mRotation * mTranslation * pmRootSpacePose[iParent]
}

void LocalSpaceErrorToleranceCalculator::GeneratePose(SMath::Mat44* pmRootSpacePose, unsigned iFrame)
{
	for (unsigned iJointRoot = 0, numJoints = (unsigned)m_joints.size(); iJointRoot < numJoints; ++iJointRoot) {
		if (m_joints[iJointRoot].m_parent == -1) {
			GeneratePoseRecursive(pmRootSpacePose, kvScaleIdentity, iFrame, iJointRoot);
		}
	}
}

void LocalSpaceErrorToleranceCalculator::GenerateObjectRoot(SMath::Mat44* pmObjectRoot, unsigned iFrame)
{
	if (m_sourceData.m_pRootAnim) {
		ITGEOM::Vec3 vScale = m_sourceData.m_pRootAnim->GetScale(iFrame);
		ITGEOM::Quat qRotation = m_sourceData.m_pRootAnim->GetRotation(iFrame);
		ITGEOM::Vec3 vTranslation = m_sourceData.m_pRootAnim->GetTranslation(iFrame);
		*pmObjectRoot = SMathMat44_BuildAffine( SMathVector(vScale), SMathQuat(qRotation), SMathPoint(vTranslation) );
	} else {
		*pmObjectRoot = SMath::Mat44(SMath::kIdentity);
	}
}

void LocalSpaceErrorToleranceCalculator::GeneratePosesForFrames(unsigned iFramePrev, unsigned iFrame, unsigned iFrameNext)
{
	unsigned numCacheJoints = (unsigned)(1 + m_joints.size());
	unsigned aiFramePosToCacheIndex[3] = { 3, 3, 3 };
	bool abUsedCacheIndex[3] = { false, false, false };
	m_aiFrame[0] = iFramePrev;
	m_aiFrame[1] = iFrame;
	m_aiFrame[2] = iFrameNext;

	for (unsigned iFramePos = 0; iFramePos < 3; ++iFramePos) {
		unsigned iFrame = m_aiFrame[iFramePos];
		// check if this frame is already in the cache
		for (unsigned iCacheIndex = 0; iCacheIndex < 3; ++iCacheIndex) {
			if (iFrame == m_aiCacheFrame[iCacheIndex]) {
				aiFramePosToCacheIndex[iFramePos] = iCacheIndex;
				abUsedCacheIndex[iCacheIndex] = true;
				break;
			}
		}
	}
	for (unsigned iFramePos = 0; iFramePos < 3; ++iFramePos) {
		unsigned iFrame = m_aiFrame[iFramePos];
		unsigned iCacheIndex = aiFramePosToCacheIndex[iFramePos];
		SMath::Mat44* pmPoseMatrices;
		if (iCacheIndex >= 3) {
			// check if this frame was just added to the cache
			for (unsigned iFramePosPrev = 0; iFramePosPrev < iFramePos; ++iFramePosPrev) {
				if (iFrame == m_aiFrame[iFramePosPrev]) {
					iCacheIndex = aiFramePosToCacheIndex[iFramePosPrev];
					break;
				}
			}
		}
		if (iCacheIndex >= 3) {
			// if this frame is not in the cache, find an unused cache slot and generate the new data:
			for (iCacheIndex = 0; iCacheIndex < 3; ++iCacheIndex) {
				if (!abUsedCacheIndex[iCacheIndex]) {
					break;
				}
			}
			abUsedCacheIndex[iCacheIndex] = true;
			pmPoseMatrices = &m_amMatrixCache[ iCacheIndex*numCacheJoints ];
			GenerateObjectRoot(pmPoseMatrices, iFrame);
			GeneratePose(pmPoseMatrices + 1, iFrame);
			m_aiCacheFrame[iCacheIndex] = iFrame;
		} else {
			pmPoseMatrices = &m_amMatrixCache[ iCacheIndex*numCacheJoints ];
		}
		m_pmObjectRoot[iFramePos] = pmPoseMatrices;
		m_pmRootSpacePose[iFramePos] = pmPoseMatrices + 1;
	}
}

void LocalSpaceErrorToleranceCalculator::GetMinRadialErrorToleranceRecursive( 
	ErrorTolerance& radialErrorTolerance,
	SMath::Point const& vParentTranslation,
	unsigned iJoint)
{
	// for this joint and all children of this joint, find min rotation error allowed by translation errors
	float fDistanceToJoint = SMath::Length( SMath::Point( m_pmRootSpacePose[1][iJoint].GetRow(3) ) - vParentTranslation );
	ITASSERT(m_translationErrorTolerances[iJoint].m_tolerance != ERROR_TOLERANCE_UNDEFINED);
	if (fDistanceToJoint > 0.0f) {
		radialErrorTolerance.m_tolerance = m_translationErrorTolerances[iJoint].m_tolerance / fDistanceToJoint;
	} else {
		radialErrorTolerance.m_tolerance = ERROR_TOLERANCE_MAX;
	}

	ITASSERT(m_translationErrorTolerances[iJoint].m_limiter.m_eType <= JointErrorToleranceLimiter::kTypeObjectRootDelta);
	radialErrorTolerance.m_limiter.m_eType = (unsigned short)(JointErrorToleranceLimiter::kTypeTranslation + m_translationErrorTolerances[iJoint].m_limiter.m_eType);
	radialErrorTolerance.m_limiter.m_iJoint = (unsigned short)iJoint;
	radialErrorTolerance.m_limiter.m_fValue = fDistanceToJoint;

	Joint const& joint = m_joints[iJoint];
	for (int iChild = joint.m_child; iChild != -1; iChild = m_joints[iChild].m_sibling) {
		ErrorTolerance childRadialErrorTolerance;
		GetMinRadialErrorToleranceRecursive(childRadialErrorTolerance, vParentTranslation, (unsigned)iChild);
		if (radialErrorTolerance.m_tolerance > childRadialErrorTolerance.m_tolerance) {
			radialErrorTolerance = childRadialErrorTolerance;
		}
	}
}

template <typename T>
float GetDelta( T const& v0, T const& v1, T const& v2 )
{
	static const float kfEpsilon = 0.000001f;
	// For the sequence (v0, v1, v2), returns the rate of change at v1 per time interval, assuming uniform time intervals.
	// We calculate this as a weighted average of the speed from v0 and to v2, weighted by inverse speed.
	SMath::Vector vDelta01 = v1 - v0, vDelta12 = v2 - v1;
	float fDelta01 = SMath::Length(vDelta01), fDelta12 = SMath::Length(vDelta12);
	if (fDelta01 + fDelta12 < kfEpsilon) {
		return 0.0f;
	}
	SMath::Vector vDelta = ( vDelta01 * fDelta12 + vDelta12 * fDelta01 ) / (fDelta01 + fDelta12);
	float fDelta = SMath::Length(vDelta);
	return fDelta;
}

float GetDelta( SMath::Quat const& q0, SMath::Quat const& q1, SMath::Quat const& q2 )
{
	static const float kfEpsilon = 0.000001f;
	// For the sequence (q0, q1, q2), returns the rate of change at q1 per time interval, assuming uniform time intervals.
	// We calculate this as a weighted average of the speed from q0 and to q2, weighted by inverse speed.
	SMath::Quat qDelta01 = SMath::Conjugate(q0)*q1, qDelta12 = SMath::Conjugate(q1)*q2;
	float fCosHalfDelta01 = fabsf( qDelta01.W() ), fCosHalfDelta12 = fabsf( qDelta12.W() );
	float fDelta01 = (fCosHalfDelta01 > 1.0f - kfEpsilon) ? 0.0f : 2.0f * acosf(fCosHalfDelta01);
	float fDelta12 = (fCosHalfDelta12 > 1.0f - kfEpsilon) ? 0.0f : 2.0f * acosf(fCosHalfDelta12);
	if (fDelta01 + fDelta12 < kfEpsilon) {
		return 0.0f;
	}
	SMath::Quat qDelta = Slerp( qDelta01, qDelta12, fDelta01 / (fDelta01 + fDelta12) );
	float fCosHalfDelta = fabsf( qDelta.W() );
	float fDelta = (fCosHalfDelta > 1.0f - kfEpsilon) ? 0.0f : 2.0f * acosf(fCosHalfDelta);
	return fDelta;
}

float GetDelta( float f0, float f1, float f2 )
{
	static const float kfEpsilon = 0.000001f;
	// For the sequence (f0, f1, f2), returns the rate of change at f1 per time interval, assuming uniform time intervals.
	// We calculate this as a weighted average of the speed from f0 and to f2, weighted by inverse speed.
	float fDelta01 = f1 - f0, fDelta12 = f2 - f1;
	float fAbsDelta01 = fabsf(fDelta01), fAbsDelta12 = fabsf(fDelta12);
	if (fAbsDelta01 + fAbsDelta12 <= kfEpsilon) {
		return 0.0f;
	}
	float fDelta = ( fDelta01 * fAbsDelta12 + fDelta12 * fAbsDelta01 ) / (fAbsDelta01 + fAbsDelta12);
	float fAbsDelta = fabsf(fDelta);
	return fAbsDelta;
}

// fills out m_jointRootSpaceErrorTolerances for each joint (from descriptors in m_jointErrorDescs)
// increases the root space error tolerances of any joint channels that have delta or speed factors set
void LocalSpaceErrorToleranceCalculator::GenerateRootSpaceErrors()
{
	for (unsigned iJoint = 0, numJoints = (unsigned)m_joints.size(); iJoint < numJoints; ++iJoint) 
	{
		JointRootSpaceError const& jointErrorDesc = m_jointErrorDescs[iJoint];		// src descriptors
		JointErrorTolerance& jointErrorTolerance = m_jointRootSpaceErrorTolerances[iJoint];		// dst tolerances
		Joint const& joint = m_joints[iJoint];

		float scaleTolerance = jointErrorDesc.m_scale.m_tolerance;
		float rotationTolerance = jointErrorDesc.m_rotation.m_tolerance;
		float translationTolerance = jointErrorDesc.m_translation.m_tolerance;

		{
			//FIXME: should we apply ObjectRoot scale or not when considering translation error tolerances?
			SMath::Mat44 mParent = m_pmObjectRoot[1][0];
			if (joint.m_parent != -1) {
				mParent = m_pmRootSpacePose[1][joint.m_parent] * mParent;
			}
			float fParentScaleSqrMax = max( max( (float)SMath::Length3Sqr( mParent.GetRow(0) ), (float)SMath::Length3Sqr( mParent.GetRow(1) ) ), (float)SMath::Length3Sqr( mParent.GetRow(2) ) );
			if (fabsf(fParentScaleSqrMax - 1.0f) > 1.79e-7f) {	// 1.79e-7f is the smallest distance from 1.0f which will produce sqrtf != 1.0f
				if (fParentScaleSqrMax > 0.0f) {
					translationTolerance /= sqrtf(fParentScaleSqrMax);
				} else {
					translationTolerance = ERROR_TOLERANCE_MAX;
				}
			}
		}

		// determine which channels have delta & speed factors
		bool bCheckScaleDelta = (jointErrorDesc.m_scale.m_deltaFactor > 0.0f);
		bool bCheckScaleSpeed = (jointErrorDesc.m_scale.m_speedFactor > 0.0f);
		if (!bCheckScaleDelta && !bCheckScaleSpeed) {
			jointErrorTolerance.m_scale.m_tolerance = scaleTolerance;
			jointErrorTolerance.m_scale.m_limiter = JointErrorToleranceLimiter(JointErrorToleranceLimiter::kTypeError, iJoint);
		}
		bool bCheckRotationDelta = (jointErrorDesc.m_rotation.m_deltaFactor > 0.0f);
		bool bCheckRotationSpeed = (jointErrorDesc.m_rotation.m_speedFactor > 0.0f);
		if (!bCheckRotationDelta && !bCheckRotationSpeed) {
			jointErrorTolerance.m_rotate.m_tolerance = rotationTolerance;
			jointErrorTolerance.m_rotate.m_limiter = JointErrorToleranceLimiter(JointErrorToleranceLimiter::kTypeError, iJoint);
		}
		bool bCheckTranslationDelta = jointErrorDesc.m_translation.m_deltaFactor > 0.0f;
		if (!bCheckTranslationDelta) {
			jointErrorTolerance.m_translate.m_tolerance = translationTolerance;
			jointErrorTolerance.m_translate.m_limiter = JointErrorToleranceLimiter(JointErrorToleranceLimiter::kTypeError, iJoint);
		}

		// increase this joint's root space error tolerance based on its root-space and object-root-space deltas
		if (bCheckScaleDelta || bCheckScaleSpeed || bCheckRotationDelta || bCheckRotationSpeed || bCheckTranslationDelta) 
		{
			// decompose root & object-root space transforms into joint params for the current joint & the current 3 frames (poses)
			// object-root incorporates any motion in the movement/locomotion anim tracks via m_pmObjectRoot (see GeneratePosesForFrames)
			SMath::Vector avScale[3], avObjectRootScale[3];
			SMath::Quat aqRotation[3], aqObjectRootRotation[3];
			SMath::Point avTranslation[3], avObjectRootTranslation[3];
			for (unsigned iFramePos = 0; iFramePos < 3; ++iFramePos) {
				SMathMat44_DecomposeAffine( m_pmRootSpacePose[iFramePos][iJoint], &(avScale[iFramePos]), &(aqRotation[iFramePos]), &(avTranslation[iFramePos]) );
				SMathMat44_DecomposeAffine( m_pmRootSpacePose[iFramePos][iJoint] * m_pmObjectRoot[iFramePos][0], &(avObjectRootScale[iFramePos]), &(aqObjectRootRotation[iFramePos]), &(avObjectRootTranslation[iFramePos]) );
			}

			float fSpeed = 0.0f;
			JointErrorToleranceLimiter::Type eLimiterTypeSpeed = JointErrorToleranceLimiter::kTypeInvalid;
			if (bCheckTranslationDelta || bCheckScaleSpeed || bCheckRotationSpeed) {
				float fDelta = GetDelta(avTranslation[0], avTranslation[1], avTranslation[2]);
				float fObjectRootDelta = GetDelta(avObjectRootTranslation[0], avObjectRootTranslation[1], avObjectRootTranslation[2]);
				JointErrorToleranceLimiter::Type eLimiterType = (fDelta > fObjectRootDelta) ? JointErrorToleranceLimiter::kTypeObjectRootDelta : JointErrorToleranceLimiter::kTypeDelta;
				eLimiterTypeSpeed = (fDelta > fObjectRootDelta) ? JointErrorToleranceLimiter::kTypeObjectRootSpeed : JointErrorToleranceLimiter::kTypeSpeed;
				if (fDelta > fObjectRootDelta) {
					fDelta = fObjectRootDelta;
				}
				fSpeed = fDelta;
				if (bCheckTranslationDelta) {
					translationTolerance += jointErrorDesc.m_translation.m_deltaFactor * fDelta;
					jointErrorTolerance.m_translate.m_tolerance = translationTolerance;
					jointErrorTolerance.m_translate.m_limiter = JointErrorToleranceLimiter(eLimiterType, iJoint, fDelta);
				}
			}
			if (bCheckScaleDelta || bCheckScaleSpeed) {
				JointErrorToleranceLimiter::Type eLimiterType = JointErrorToleranceLimiter::kTypeInvalid;
				float fDelta = 0.0f;
				if (bCheckScaleDelta) {
					fDelta = GetDelta(avScale[0], avScale[1], avScale[2]);
					float fObjectRootDelta = GetDelta(avObjectRootScale[0], avObjectRootScale[1], avObjectRootScale[2]);
					eLimiterType = (fDelta > fObjectRootDelta) ? JointErrorToleranceLimiter::kTypeObjectRootDelta : JointErrorToleranceLimiter::kTypeDelta;
					if (fDelta > fObjectRootDelta) {
						fDelta = fObjectRootDelta;
					}
				}
				float fScaleErrorDeltaFactor = bCheckScaleDelta ? jointErrorDesc.m_scale.m_deltaFactor * fDelta : 0.0f;
				float fScaleErrorSpeedFactor = bCheckScaleSpeed ? jointErrorDesc.m_scale.m_speedFactor * fSpeed : 0.0f;
				scaleTolerance += fScaleErrorDeltaFactor + fScaleErrorSpeedFactor;
				jointErrorTolerance.m_scale.m_tolerance = scaleTolerance;
				if (bCheckScaleSpeed && fScaleErrorSpeedFactor > fScaleErrorDeltaFactor) {
					jointErrorTolerance.m_scale.m_limiter = JointErrorToleranceLimiter(eLimiterTypeSpeed, iJoint, fSpeed);
				} else {
					jointErrorTolerance.m_scale.m_limiter = JointErrorToleranceLimiter(eLimiterType, iJoint, fDelta);
				}
			}
			if (bCheckRotationDelta || bCheckRotationSpeed) {
				JointErrorToleranceLimiter::Type eLimiterType = JointErrorToleranceLimiter::kTypeInvalid;
				float fDelta = 0.0f;
				if (bCheckRotationDelta) {
					fDelta = GetDelta(aqRotation[0], aqRotation[1], aqRotation[2]);
					float fObjectRootDelta = GetDelta(aqObjectRootRotation[0], aqObjectRootRotation[1], aqObjectRootRotation[2]);
					eLimiterType = (fDelta > fObjectRootDelta) ? JointErrorToleranceLimiter::kTypeObjectRootDelta : JointErrorToleranceLimiter::kTypeDelta;
					if (fDelta > fObjectRootDelta) {
						fDelta = fObjectRootDelta;
					}
				}
				float fRotationErrorDeltaFactor = bCheckRotationDelta ? jointErrorDesc.m_rotation.m_deltaFactor * fDelta : 0.0f;
				float fRotationErrorSpeedFactor = bCheckRotationSpeed ? jointErrorDesc.m_rotation.m_speedFactor * fSpeed : 0.0f;
				rotationTolerance += fRotationErrorDeltaFactor + fRotationErrorSpeedFactor;
				jointErrorTolerance.m_rotate.m_tolerance = rotationTolerance;
				if (bCheckRotationSpeed && fRotationErrorSpeedFactor > fRotationErrorDeltaFactor) {
					jointErrorTolerance.m_rotate.m_limiter = JointErrorToleranceLimiter(eLimiterTypeSpeed, iJoint, fSpeed);
				} else {
					jointErrorTolerance.m_rotate.m_limiter = JointErrorToleranceLimiter(eLimiterType, iJoint, fDelta);
				}
			}
		}
	}
}

// results are stored in m_jointErrorTolerance
void LocalSpaceErrorToleranceCalculator::GenerateMaxLocalSpaceErrorsRecursive(JointErrorTolerance& parentErrorTolerance, unsigned iJoint, bool bParentNeedsData)
{
	Joint const& joint = m_joints[iJoint];
	JointErrorTolerance localErrorTolerance = m_jointRootSpaceErrorTolerances[iJoint];	// input root space tolerances (user supplied)

	int iJointAnimIndex = iJoint < m_binding.m_jointIdToJointAnimId.size() ? m_binding.m_jointIdToJointAnimId[iJoint] : -1;
	bool bNeedsData = bParentNeedsData || (iJointAnimIndex >= 0);

	if (joint.m_child != -1) {
		// recurse to our first child, who will iterate through its children and siblings, collecting minimum error tolerances for us:
		GenerateMaxLocalSpaceErrorsRecursive(localErrorTolerance, (unsigned)joint.m_child, bNeedsData);
		if (bNeedsData) {
			// translation error tolerances of children may restrict our rotation error tolerance:
			SMath::Point vTranslation( m_pmRootSpacePose[1][iJoint].GetRow(3) );
			ErrorTolerance childRadialErrorTolerance;
			GetMinRadialErrorToleranceRecursive(childRadialErrorTolerance, vTranslation, (unsigned)joint.m_child);
			if (localErrorTolerance.m_rotate.m_tolerance > childRadialErrorTolerance.m_tolerance) {
				localErrorTolerance.m_rotate.m_tolerance = childRadialErrorTolerance.m_tolerance;
				localErrorTolerance.m_rotate.m_limiter = childRadialErrorTolerance.m_limiter;
			}
			if (localErrorTolerance.m_scale.m_tolerance > childRadialErrorTolerance.m_tolerance) {
				localErrorTolerance.m_scale.m_tolerance = childRadialErrorTolerance.m_tolerance;
				localErrorTolerance.m_scale.m_limiter = childRadialErrorTolerance.m_limiter;
			}
		}
	}

	// store our error tolerances if they are needed:
	if (iJointAnimIndex >= 0) {
		unsigned const iFrame = m_aiFrame[1];	// index of anim frame we're currently dealing with
		m_jointErrorTolerance.scale[iJointAnimIndex][iFrame] = localErrorTolerance.m_scale.m_tolerance;
		m_jointErrorTolerance.rotation[iJointAnimIndex][iFrame] = localErrorTolerance.m_rotate.m_tolerance;
		m_jointErrorTolerance.translation[iJointAnimIndex][iFrame] = localErrorTolerance.m_translate.m_tolerance;
		if (m_bCollectLimitterData) {
			m_jointErrorTolerance.scale_limiter[iJointAnimIndex][iFrame] = localErrorTolerance.m_scale.m_limiter;
			m_jointErrorTolerance.rotation_limiter[iJointAnimIndex][iFrame] = localErrorTolerance.m_rotate.m_limiter;
			m_jointErrorTolerance.translation_limiter[iJointAnimIndex][iFrame] = localErrorTolerance.m_translate.m_limiter;
		}
	}

	if (bParentNeedsData) {
		// calculate the minimum scale, rotation, and translation error tolerance allowed by respective error tolerances of our parent's children
		if (parentErrorTolerance.m_scale.m_tolerance > localErrorTolerance.m_scale.m_tolerance) {
			parentErrorTolerance.m_scale.m_tolerance = localErrorTolerance.m_scale.m_tolerance;
			parentErrorTolerance.m_scale.m_limiter = localErrorTolerance.m_scale.m_limiter;
		}
		if (parentErrorTolerance.m_rotate.m_tolerance > localErrorTolerance.m_rotate.m_tolerance) {
			parentErrorTolerance.m_rotate.m_tolerance = localErrorTolerance.m_rotate.m_tolerance;
			if (localErrorTolerance.m_rotate.m_limiter.m_eType >= JointErrorToleranceLimiter::kTypeTranslation) {
				parentErrorTolerance.m_rotate.m_limiter = JointErrorToleranceLimiter( JointErrorToleranceLimiter::kTypeTranslationInherited, iJoint );
			} else {
				parentErrorTolerance.m_rotate.m_limiter = localErrorTolerance.m_rotate.m_limiter;
			}
		}
		if (parentErrorTolerance.m_translate.m_tolerance > localErrorTolerance.m_translate.m_tolerance) {
			parentErrorTolerance.m_translate.m_tolerance = localErrorTolerance.m_translate.m_tolerance;
			parentErrorTolerance.m_translate.m_limiter = localErrorTolerance.m_translate.m_limiter;
		}
		m_translationErrorTolerances[iJoint].m_tolerance = localErrorTolerance.m_translate.m_tolerance;
		m_translationErrorTolerances[iJoint].m_limiter = localErrorTolerance.m_translate.m_limiter;
	}

	// Finally recurse to our sibling if any, updating the running min error tolerance for its children if bParentNeedsData:
	if (joint.m_sibling != -1) {
		GenerateMaxLocalSpaceErrorsRecursive(parentErrorTolerance, (unsigned)joint.m_sibling, bParentNeedsData);
		if (bParentNeedsData) {
			ITASSERT(joint.m_parent != -1);
			// translation error tolerances of our parent's children may also restrict our parent's rotation error tolerance:
			SMath::Point vTranslation( m_pmRootSpacePose[1][joint.m_parent].GetRow(3) );
			ErrorTolerance siblingRadialErrorTolerance;
			GetMinRadialErrorToleranceRecursive(siblingRadialErrorTolerance, vTranslation, (unsigned)joint.m_sibling);
			if (parentErrorTolerance.m_rotate.m_tolerance > siblingRadialErrorTolerance.m_tolerance) {
				parentErrorTolerance.m_rotate.m_tolerance = siblingRadialErrorTolerance.m_tolerance;
				parentErrorTolerance.m_rotate.m_limiter = siblingRadialErrorTolerance.m_limiter;
			}
			if (parentErrorTolerance.m_scale.m_tolerance > siblingRadialErrorTolerance.m_tolerance) {
				parentErrorTolerance.m_scale.m_tolerance = siblingRadialErrorTolerance.m_tolerance;
				parentErrorTolerance.m_scale.m_limiter = siblingRadialErrorTolerance.m_limiter;
			}
		}
	}
}

void LocalSpaceErrorToleranceCalculator::GenerateLocalSpaceErrorsForJointRecursive(
	int iJointAnimIndex0,
	std::vector<ParentJointError> const& finalizedParentJointErrors,
	std::vector<ParentJointError>& parentJointErrors,
	unsigned iJoint)
{
	Joint const& joint = m_joints[iJoint];
	int iJointAnimIndex = iJoint < m_binding.m_jointIdToJointAnimId.size() ? m_binding.m_jointIdToJointAnimId[iJoint] : -1;
	JointErrorTolerance const& rootSpaceErrorTolerance = m_jointRootSpaceErrorTolerances[iJoint];

	unsigned iFrame = m_aiFrame[1];
	SMath::Point vJointTranslation( m_pmRootSpacePose[1][iJoint].GetRow(3) );
	bool bConstantScale = true, bConstantRotation = true, bConstantTranslation = true;
	if (iJointAnimIndex >= 0) {
		bConstantScale = m_sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex);
		bConstantRotation =	m_sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex);
		bConstantTranslation = m_sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex);
	}
	float fJointScaleError = bConstantScale ? 0.0f : m_jointErrorTolerance.scale[iJointAnimIndex][iFrame];
	float fJointRotationError = bConstantRotation ? 0.0f : m_jointErrorTolerance.rotation[iJointAnimIndex][iFrame];
	float fJointTranslationError = bConstantTranslation ? 0.0f : m_jointErrorTolerance.translation[iJointAnimIndex][iFrame];

	// sum up the error tolerance contributions of all parents of this joint
	float fScaleErrorSum = fJointScaleError;
	float fRotationErrorSum = fJointRotationError;
	float fTranslationErrorSum = fJointTranslationError;
	for (auto it = parentJointErrors.end(), itEnd = parentJointErrors.begin(); it != itEnd; ) {
		--it;
		float fDistance = SMath::Length( vJointTranslation - SMathPoint(it->m_vTranslation) );
		fScaleErrorSum += it->m_fScaleError;
		fRotationErrorSum += it->m_fRotationError;
		fTranslationErrorSum += it->m_fTranslationError + fDistance * it->m_fScaleError + fDistance * it->m_fRotationError;
	}
	float fScaleErrorLimit = rootSpaceErrorTolerance.m_scale.m_tolerance;
	float fRotationErrorLimit = rootSpaceErrorTolerance.m_rotate.m_tolerance;
	float fTranslationErrorLimit = rootSpaceErrorTolerance.m_translate.m_tolerance;
	for (auto it = finalizedParentJointErrors.begin(), itEnd = finalizedParentJointErrors.end(); it != itEnd; ++it) {
		float fDistance = SMath::Length( vJointTranslation - SMathPoint(it->m_vTranslation) );
		fScaleErrorLimit -= it->m_fScaleError;
		fRotationErrorLimit -= it->m_fRotationError;
		fTranslationErrorLimit -= it->m_fTranslationError + fDistance * it->m_fScaleError + fDistance * it->m_fRotationError;
	}
//FIXME: Should possibly replace this by some test against -epsilon?
//	ITASSERT(fScaleErrorLimit >= 0.0f && fRotationErrorLimit >= 0.0f && fTranslationErrorLimit >= 0.0f);
	if (!(fScaleErrorLimit >= 0.0f && fRotationErrorLimit >= 0.0f && fTranslationErrorLimit >= 0.0f))
	{
//FIXME: Should this output a warning in some cases?
		if (fScaleErrorLimit < 0)
			fScaleErrorLimit = 0.0f;
		if (fRotationErrorLimit < 0)
			fRotationErrorLimit = 0.0f;
		if (fTranslationErrorLimit < 0)
			fTranslationErrorLimit = 0.0f;
	}

	// determine if parent joint's channels are constant
	bool bConstantParentScale = true, bConstantParentRotation = true, bConstantParentTranslation = true;
	unsigned numParentDataChannels = 0;
	if (iJointAnimIndex0 >= 0) {
		bConstantParentScale = m_sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex0);
		bConstantParentRotation = m_sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex0);
		bConstantParentTranslation = m_sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex0);
		numParentDataChannels =	!bConstantParentScale + !bConstantParentRotation + !bConstantParentTranslation;
	}

	if (!bConstantParentScale && fScaleErrorSum > fScaleErrorLimit) {
		float fErrorFactor = fScaleErrorLimit / fScaleErrorSum;
		float fMaxScaleErrorToleranceRoot = parentJointErrors[0].m_fScaleError * fErrorFactor;
		if (m_jointErrorTolerance.scale[iJointAnimIndex0][iFrame] > fMaxScaleErrorToleranceRoot) {
			m_jointErrorTolerance.scale[iJointAnimIndex0][iFrame] = fMaxScaleErrorToleranceRoot;
			if (m_bCollectLimitterData) {
				m_jointErrorTolerance.scale_limiter[iJointAnimIndex0][iFrame].m_eType |= JointErrorToleranceLimiter::kTypeFlagGlobal;
			}
		}
	}
	if (!bConstantParentRotation && fRotationErrorSum > fRotationErrorLimit) {
		float fErrorFactor = fRotationErrorLimit / fRotationErrorSum;
		float fMaxRotationErrorToleranceRoot = parentJointErrors[0].m_fRotationError * fErrorFactor;
		if (m_jointErrorTolerance.rotation[iJointAnimIndex0][iFrame] > fMaxRotationErrorToleranceRoot) {
			m_jointErrorTolerance.rotation[iJointAnimIndex0][iFrame] = fMaxRotationErrorToleranceRoot;
			if (m_bCollectLimitterData) {
				m_jointErrorTolerance.rotation_limiter[iJointAnimIndex0][iFrame].m_eType |= JointErrorToleranceLimiter::kTypeFlagGlobal;
			}
		}
	}
	if (numParentDataChannels != 0 && fTranslationErrorSum > fTranslationErrorLimit) {
		float fErrorFactor = fTranslationErrorLimit / fTranslationErrorSum;
		float fMaxScaleErrorToleranceRoot = parentJointErrors[0].m_fScaleError * fErrorFactor;
		float fMaxRotationErrorToleranceRoot = parentJointErrors[0].m_fRotationError * fErrorFactor;
		float fMaxTranslationErrorToleranceRoot = parentJointErrors[0].m_fTranslationError * fErrorFactor;
		if (!bConstantParentScale && m_jointErrorTolerance.scale[iJointAnimIndex0][iFrame] > fMaxScaleErrorToleranceRoot) {
			m_jointErrorTolerance.scale[iJointAnimIndex0][iFrame] = fMaxScaleErrorToleranceRoot;
			if (m_bCollectLimitterData) {
				m_jointErrorTolerance.scale_limiter[iJointAnimIndex0][iFrame].m_eType |= JointErrorToleranceLimiter::kTypeFlagGlobalRadial;
			}
		}
		if (!bConstantParentRotation && m_jointErrorTolerance.rotation[iJointAnimIndex0][iFrame] > fMaxRotationErrorToleranceRoot) {
			m_jointErrorTolerance.rotation[iJointAnimIndex0][iFrame] = fMaxRotationErrorToleranceRoot;
			if (m_bCollectLimitterData) {
				m_jointErrorTolerance.rotation_limiter[iJointAnimIndex0][iFrame].m_eType |= JointErrorToleranceLimiter::kTypeFlagGlobalRadial;
			}
		}
		if (!bConstantParentTranslation && m_jointErrorTolerance.translation[iJointAnimIndex0][iFrame] > fMaxTranslationErrorToleranceRoot) {
			m_jointErrorTolerance.translation[iJointAnimIndex0][iFrame] = fMaxTranslationErrorToleranceRoot;
			if (m_bCollectLimitterData) {
				m_jointErrorTolerance.translation_limiter[iJointAnimIndex0][iFrame].m_eType |= JointErrorToleranceLimiter::kTypeFlagGlobal;
			}
		}
	}

	if (joint.m_child != -1) {
		parentJointErrors.push_back( ParentJointError( ITGEOMVec3(vJointTranslation), fJointScaleError, fJointRotationError, fJointTranslationError ) );
		for (int iChild = joint.m_child; iChild != -1; iChild = m_joints[iChild].m_sibling) {
			GenerateLocalSpaceErrorsForJointRecursive(iJointAnimIndex0, finalizedParentJointErrors, parentJointErrors, (unsigned)iChild);
		}
		parentJointErrors.pop_back();
	}
}

void LocalSpaceErrorToleranceCalculator::GenerateLocalSpaceErrorsRecursive( unsigned iJoint, std::vector<ParentJointError>& finalizedParentJointErrors )
{
	Joint const& joint = m_joints[iJoint];
	int iJointAnimIndex = iJoint < m_binding.m_jointIdToJointAnimId.size() ? m_binding.m_jointIdToJointAnimId[iJoint] : -1;

	// determine each channel's constness
	bool bConstantScale = true, bConstantRotation = true, bConstantTranslation = true;
	unsigned numLocalDataChannels = 0;
	if (iJointAnimIndex >= 0) {
		bConstantScale = m_sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex);
		bConstantRotation = m_sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex);
		bConstantTranslation = m_sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex);
		numLocalDataChannels = !bConstantScale + !bConstantRotation + !bConstantTranslation;
	}

	if (joint.m_child != -1) {
		if (numLocalDataChannels) {
			unsigned iFrame = m_aiFrame[1];	// current frame we're interested in
			ITGEOM::Vec3 vJointTranslation = ITGEOMVec3( SMath::Point( m_pmRootSpacePose[1][iJoint].GetRow(3) ) );	// joint position in root space
			
			// max local tolerances computed in previous step (see GenerateMaxLocalSpaceErrorsRecursive)
			float fJointScaleError = bConstantScale ? 0.0f : m_jointErrorTolerance.scale[iJointAnimIndex][iFrame];
			float fJointRotationError = bConstantRotation ? 0.0f : m_jointErrorTolerance.rotation[iJointAnimIndex][iFrame];
			float fJointTranslationError = bConstantTranslation ? 0.0f : m_jointErrorTolerance.translation[iJointAnimIndex][iFrame];

			//1) find a value for our local error which satisfies all the requirements of our child root space errors
			std::vector<ParentJointError> parentJointErrors;
			parentJointErrors.push_back( ParentJointError(vJointTranslation, fJointScaleError, fJointRotationError, fJointTranslationError ) );
			for (int iChild = joint.m_child; iChild != -1; iChild = m_joints[iChild].m_sibling) {
				GenerateLocalSpaceErrorsForJointRecursive(iJointAnimIndex, finalizedParentJointErrors, parentJointErrors, (unsigned)iChild);
			}
			parentJointErrors.pop_back();
			// get updated local error for non-constant channels
			if (!bConstantScale) {
				fJointScaleError = m_jointErrorTolerance.scale[iJointAnimIndex][iFrame];
			}
			if (!bConstantRotation) {
				fJointRotationError = m_jointErrorTolerance.rotation[iJointAnimIndex][iFrame];
			}
			if (!bConstantTranslation) {
				fJointTranslationError = m_jointErrorTolerance.translation[iJointAnimIndex][iFrame];
			}

			//2) now finalize the error we've chosen and recurse to our children to calculate their final local error tolerances
			finalizedParentJointErrors.push_back( ParentJointError( vJointTranslation, fJointScaleError, fJointRotationError, fJointTranslationError ) );
			GenerateLocalSpaceErrorsRecursive((unsigned)joint.m_child, finalizedParentJointErrors);
			finalizedParentJointErrors.pop_back();
		} else {
			// if all local channels are constant, don't bother adding an all zero entry to finalizedParentJointErrors
			GenerateLocalSpaceErrorsRecursive((unsigned)joint.m_child, finalizedParentJointErrors);
		}
	}

	if (joint.m_sibling != -1) {
		//2) now recurse to our sibling to calculate its final local error tolerances
		GenerateLocalSpaceErrorsRecursive((unsigned)joint.m_sibling, finalizedParentJointErrors);
	}
}

void LocalSpaceErrorToleranceCalculator::GenerateLocalSpaceErrorTolerances()
{
	// resize tolerance arrays to the number of frames in the clip
	unsigned const numJointAnims = (unsigned)m_binding.m_jointAnimIdToJointId.size();
	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		unsigned iJoint = (unsigned)m_binding.m_jointAnimIdToJointId[iJointAnimIndex];
		if (iJoint >= (unsigned)m_binding.m_jointIdToJointAnimId.size()) {
			continue;
		}
		m_jointErrorTolerance.scale[iJointAnimIndex].resize( m_sourceData.m_numFrames );
		m_jointErrorTolerance.rotation[iJointAnimIndex].resize( m_sourceData.m_numFrames );
		m_jointErrorTolerance.translation[iJointAnimIndex].resize( m_sourceData.m_numFrames );
		if (m_bCollectLimitterData) {
			m_jointErrorTolerance.scale_limiter[iJointAnimIndex].resize( m_sourceData.m_numFrames );
			m_jointErrorTolerance.rotation_limiter[iJointAnimIndex].resize( m_sourceData.m_numFrames );
			m_jointErrorTolerance.translation_limiter[iJointAnimIndex].resize( m_sourceData.m_numFrames );
		}
	}

	// Calculate the root space errors for this frame based on deltas from the previous and next frame:
	// (increases the root space error tolerances of any joint channels that have delta or speed factors set)
	GenerateRootSpaceErrors();

	// Calculate the maximum error tolerance each joint parameter could have based on root space limits of all children of that joint:
	JointErrorTolerance rootErrorTolerance;
	GenerateMaxLocalSpaceErrorsRecursive(rootErrorTolerance, 0/*first root*/, false);

	// Now traverse the joints again, adjusting errors so that each joint's root space error (calculated from the sum of all
	// local space errors of parent joints) is less than the allowed root space error tolerance for that joint:
	std::vector<ParentJointError> finalizedParentJointErrors;
	GenerateLocalSpaceErrorsRecursive(0 /*first root*/, finalizedParentJointErrors);
}

std::string GetLeafName(std::string const& joint_name)
{
	std::string::size_type posLeafName = joint_name.find_last_of('|');
	posLeafName = (posLeafName == std::string::npos) ? 0 : posLeafName+1;
	return joint_name.substr(posLeafName);
}

template<typename T>
unsigned FindErrorParamIndexJoints(std::vector<T> const& joints, U32 iJoint)
{
	for (unsigned i = 0; i < joints.size(); ++i) {
		if (joints[i].m_iJoint == iJoint) {
			return i;
		}
	}
	return 0;
}

template<typename T>
unsigned FindErrorParamIndexFloats(std::vector<T> const& floats, U32 iFloat)
{
	for (unsigned i = 0; i < floats.size(); ++i) {
		if (floats[i].m_iFloatChannel == iFloat) {
			return i;
		}
	}
	return 0;
}

// copy local space error tolerances from the target joint & float arrays into the per joint & float animation structure
void AnimClipCompression_GenerateLocalSpaceErrors(
	ClipLocalSpaceErrors& localSpaceErrors,			// [output] filled out with local space error tolerances per joint animation and float animation
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	LocalSpaceErrorTargetJoints const& targetJoints,			// local space error tolerance parameters for joints
	LocalSpaceErrorTargetFloats const& targetFloatChannels)		// local space error tolerance parameters for float channels
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	unsigned const numTargetJoints = (unsigned)targetJoints.size();
	unsigned const numTargetFloats = (unsigned)targetFloatChannels.size();

	localSpaceErrors.init(numJointAnims, numFloatAnims);

	// find indices of default tolerances for joints & floats
	unsigned const iTargetJoint_Default = FindErrorParamIndexJoints(targetJoints, kChannelIndexDefault);
	unsigned const iTargetFloat_Default = FindErrorParamIndexFloats(targetFloatChannels, kChannelIndexDefault);

	ITASSERT(targetJoints[iTargetJoint_Default].m_iJoint == kChannelIndexDefault);
	ITASSERT(targetFloatChannels[iTargetFloat_Default].m_iFloatChannel == kChannelIndexDefault);
	
	for (unsigned iJointAnim = 0; iJointAnim < numJointAnims; ++iJointAnim) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[ iJointAnim ];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		unsigned iJoint = (unsigned)binding.m_jointAnimIdToJointId[ iJointAnim ];
		unsigned iTarget = FindErrorParamIndexJoints(targetJoints, iJoint);
		if (targetJoints[iTarget].m_iJoint != iJoint) {	// if not found
			iTarget = iTargetJoint_Default;	// use default index
		}
		// get tolerances for each channel & compression type (bit packing & key removal)
		ChannelLocalSpaceError scale, rotation, translation;
		if (iTarget < numTargetJoints) {
			scale = targetJoints[iTarget].m_error.m_scale;
			rotation = targetJoints[iTarget].m_error.m_rotation;
			translation = targetJoints[iTarget].m_error.m_translation;
		} else {
			scale.m_bitTolerance = scale.m_keyTolerance = 0.0f;
			rotation.m_bitTolerance = rotation.m_keyTolerance = 0.0f;
			translation.m_bitTolerance = translation.m_keyTolerance = 0.0f;
		}
		// copy bit packing tolerances
		localSpaceErrors.m_bit[kChannelTypeScale][iJointAnim] = scale.m_bitTolerance;
		localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnim] = rotation.m_bitTolerance;
		localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnim] = translation.m_bitTolerance;
		// copy key removal tolerances
		localSpaceErrors.m_key[kChannelTypeScale][iJointAnim].assign(sourceData.m_numFrames, scale.m_keyTolerance);
		localSpaceErrors.m_key[kChannelTypeRotation][iJointAnim].assign(sourceData.m_numFrames, rotation.m_keyTolerance);
		localSpaceErrors.m_key[kChannelTypeTranslation][iJointAnim].assign(sourceData.m_numFrames, translation.m_keyTolerance);
	}
	for (unsigned iFloatAnim = 0; iFloatAnim < numFloatAnims; ++iFloatAnim) {
		unsigned iFloatChannel = binding.m_floatAnimIdToFloatId[ iFloatAnim ];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		unsigned iTarget = FindErrorParamIndexFloats(targetFloatChannels, iFloatChannel);
		if (targetFloatChannels[iTarget].m_iFloatChannel != iFloatChannel) {	// if not found
			iTarget = iTargetFloat_Default;		// use default
		}
		ChannelLocalSpaceError floatError;
		if (iTarget < numTargetFloats) {
			floatError = targetFloatChannels[iTarget].m_error;
		} else {
			floatError.m_bitTolerance = floatError.m_keyTolerance = 0.0f;
		}
		localSpaceErrors.m_bit[kChannelTypeScalar][iFloatAnim] = floatError.m_bitTolerance;
		localSpaceErrors.m_key[kChannelTypeScalar][iFloatAnim].assign(sourceData.m_numFrames, floatError.m_keyTolerance);
	}
}

void DumpLocalFromRootSpaceErrors( 
	unsigned const numJoints, 
	unsigned const numJointAnims, 
	AnimationClipSourceData const &sourceData, 
	AnimationBinding const &binding, 
	AnimationHierarchyDesc const &hierarchyDesc, 
	JointErrorTolerances &jointErrorTolerance, 
	std::vector<JointRootSpaceError> &jointErrorDescs )
{
	std::vector<unsigned> jointLimiterFrame_scale(numJointAnims, 0);
	std::vector<unsigned> jointLimiterFrame_rotation(numJointAnims, 0);
	std::vector<unsigned> jointLimiterFrame_translation(numJointAnims, 0);
	for (unsigned iFrame = 0; iFrame < sourceData.m_numFrames; ++iFrame) {
		INOTE(IMsg::kDebug, "Root Space to Local Space Errors For Frame %5u/%5u\n", iFrame, sourceData.m_numFrames);
		for (unsigned iJoint = 0; iJoint < numJoints; ++iJoint) {
			int iJointAnimIndex = binding.m_jointIdToJointAnimId[iJoint];
			if (iJointAnimIndex < 0)
				continue;
			std::string joint_name = GetLeafName(hierarchyDesc.m_joints[iJoint].m_name);
			if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				JointErrorToleranceLimiter const& limiter = jointErrorTolerance.scale_limiter[iJointAnimIndex][iFrame];
				ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_scale;
				std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
				float fErrorTolerance = jointErrorTolerance.scale[iJointAnimIndex][iFrame];
				char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
				switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
				case JointErrorToleranceLimiter::kTypeError:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance);
					break;
				case JointErrorToleranceLimiter::kTypeDelta:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
						limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
					break;
				case JointErrorToleranceLimiter::kTypeSpeed:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
						limiterErrorDesc.m_speedFactor, limiter.m_fValue);
					break;
				case JointErrorToleranceLimiter::kTypeObjectRootDelta:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
						limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
					break;
				case JointErrorToleranceLimiter::kTypeObjectRootSpeed:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
						limiterErrorDesc.m_speedFactor, limiter.m_fValue);
					break;
				case JointErrorToleranceLimiter::kTypeTranslationError:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T %c(%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationDelta:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T %c(%.5f+%.4f*rd%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
							(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationObjectRootDelta:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T %c(%.5f+%.4f*md%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
							(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationInherited:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S %c <=...\n", joint_name.c_str(), fErrorTolerance,	limiter_name.c_str(), cGlobal);
					break;
				default:
					INOTE(IMsg::kDebug, "%20s.S %.5f <= ??? (type:%04x)\n", joint_name.c_str(), fErrorTolerance, limiter.m_eType);
					break;
				}
				if (fErrorTolerance < jointErrorTolerance.scale[iJointAnimIndex][ jointLimiterFrame_scale[iJointAnimIndex] ])
					jointLimiterFrame_scale[iJointAnimIndex] = iFrame;
			}
			if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				JointErrorToleranceLimiter const& limiter = jointErrorTolerance.rotation_limiter[iJointAnimIndex][iFrame];
				std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
				ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_rotation;
				float fErrorTolerance = jointErrorTolerance.rotation[iJointAnimIndex][iFrame];
				char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
				switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
				case JointErrorToleranceLimiter::kTypeError:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance);
					break;
				case JointErrorToleranceLimiter::kTypeDelta:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
						limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
					break;
				case JointErrorToleranceLimiter::kTypeSpeed:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
						limiterErrorDesc.m_speedFactor, limiter.m_fValue);
					break;
				case JointErrorToleranceLimiter::kTypeObjectRootDelta:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
						limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
					break;
				case JointErrorToleranceLimiter::kTypeObjectRootSpeed:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance,
						limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
						limiterErrorDesc.m_speedFactor, limiter.m_fValue);
					break;
				case JointErrorToleranceLimiter::kTypeTranslationError:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T %c(%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationDelta:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T %c(%.5f+%.4f*rd%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
							(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationObjectRootDelta:
					{
						ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
						INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T %c(%.5f+%.4f*md%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
							limiter_name.c_str(), cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
							(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
					}
					break;
				case JointErrorToleranceLimiter::kTypeTranslationInherited:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q %c <=...\n", joint_name.c_str(), fErrorTolerance,	limiter_name.c_str(), cGlobal);
					break;
				default:
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= ??? (type:%04x)\n", joint_name.c_str(), fErrorTolerance, limiter.m_eType);
					break;
				}
				if (fErrorTolerance < jointErrorTolerance.rotation[iJointAnimIndex][ jointLimiterFrame_rotation[iJointAnimIndex] ])
					jointLimiterFrame_rotation[iJointAnimIndex] = iFrame;
			}
			if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				JointErrorToleranceLimiter const& limiter = jointErrorTolerance.translation_limiter[iJointAnimIndex][iFrame];
				ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_translation;
				std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
				float fErrorTolerance = jointErrorTolerance.translation[iJointAnimIndex][iFrame];
				char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
				switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
				case JointErrorToleranceLimiter::kTypeError:
					INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance);
					break;
				case JointErrorToleranceLimiter::kTypeDelta:
					INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T %c(%.5f+%.4f*rd%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance, limiterErrorDesc.m_deltaFactor, limiter.m_fValue);
					break;
				case JointErrorToleranceLimiter::kTypeObjectRootDelta:
					INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T %c(%.5f+%.4f*md%.5f)\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), cGlobal, limiterErrorDesc.m_tolerance, limiterErrorDesc.m_deltaFactor, limiter.m_fValue);
					break;
				default:
					INOTE(IMsg::kDebug, "%20s.T %.5f <= ??? (type:%04x)\n", joint_name.c_str(), fErrorTolerance, limiter.m_eType);
					break;
				}
				if (fErrorTolerance < jointErrorTolerance.translation[iJointAnimIndex][ jointLimiterFrame_translation[iJointAnimIndex] ])
					jointLimiterFrame_translation[iJointAnimIndex] = iFrame;
			}
		}
	}
	INOTE(IMsg::kDebug, "Root Space to Local Space Min Errors over %u frames\n", sourceData.m_numFrames);
	for (unsigned iJoint = 0; iJoint < numJoints; ++iJoint) {
		int iJointAnimIndex = binding.m_jointIdToJointAnimId[iJoint];
		if (iJointAnimIndex < 0)
			continue;
		std::string joint_name = GetLeafName(hierarchyDesc.m_joints[iJoint].m_name);
		if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
			unsigned iFrame = jointLimiterFrame_scale[iJointAnimIndex];
			JointErrorToleranceLimiter const& limiter = jointErrorTolerance.scale_limiter[iJointAnimIndex][iFrame];
			ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_scale;
			std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
			float fErrorTolerance = jointErrorTolerance.scale[iJointAnimIndex][iFrame];
			char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
			switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
			case JointErrorToleranceLimiter::kTypeError:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance);
				break;
			case JointErrorToleranceLimiter::kTypeDelta:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
					limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
				break;
			case JointErrorToleranceLimiter::kTypeSpeed:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
					limiterErrorDesc.m_speedFactor, limiter.m_fValue);
				break;
			case JointErrorToleranceLimiter::kTypeObjectRootDelta:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
					limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
				break;
			case JointErrorToleranceLimiter::kTypeObjectRootSpeed:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
					limiterErrorDesc.m_speedFactor, limiter.m_fValue);
				break;
			case JointErrorToleranceLimiter::kTypeTranslationError:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T f%3u %c(%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationDelta:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T f%3u %c(%.5f+%.4f*rd%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
						(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationObjectRootDelta:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.T f%3u %c(%.5f+%.4f*md%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
						(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationInherited:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= %20s.S f%3u %c <=...\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal);
				break;
			default:
				INOTE(IMsg::kDebug, "%20s.S %.5f <= ??? f%3u (type:%04x)\n", joint_name.c_str(), fErrorTolerance, iFrame, limiter.m_eType);
				break;
			}
		}
		if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
			unsigned iFrame = jointLimiterFrame_rotation[iJointAnimIndex];
			JointErrorToleranceLimiter const& limiter = jointErrorTolerance.rotation_limiter[iJointAnimIndex][iFrame];
			std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
			ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_rotation;
			float fErrorTolerance = jointErrorTolerance.rotation[iJointAnimIndex][iFrame];
			char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
			switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
			case JointErrorToleranceLimiter::kTypeError:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance);
				break;
			case JointErrorToleranceLimiter::kTypeDelta:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
					limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
				break;
			case JointErrorToleranceLimiter::kTypeSpeed:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c(%.5f+%.4f*rd%.5f+%.5f*rs%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
					limiterErrorDesc.m_speedFactor, limiter.m_fValue);
				break;
			case JointErrorToleranceLimiter::kTypeObjectRootDelta:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, limiter.m_fValue,
					limiterErrorDesc.m_speedFactor, (limiterErrorDesc.m_speedFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_deltaFactor * limiter.m_fValue) / limiterErrorDesc.m_speedFactor );
				break;
			case JointErrorToleranceLimiter::kTypeObjectRootSpeed:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c(%.5f+%.4f*md%.5f+%.5f*ms%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance,
					limiterErrorDesc.m_deltaFactor, (limiterErrorDesc.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance - limiterErrorDesc.m_tolerance - limiterErrorDesc.m_speedFactor * limiter.m_fValue) / limiterErrorDesc.m_deltaFactor,
					limiterErrorDesc.m_speedFactor, limiter.m_fValue);
				break;
			case JointErrorToleranceLimiter::kTypeTranslationError:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T f%3u %c(%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationDelta:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T f%3u %c(%.5f+%.4f*rd%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
						(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationObjectRootDelta:
				{
					ChannelRootSpaceError const& limiterErrorDescT = jointErrorDescs[ limiter.m_iJoint ].m_translation;
					INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.T f%3u %c(%.5f+%.4f*md%.5f)/dist %9.5f\n", joint_name.c_str(), fErrorTolerance,
						limiter_name.c_str(), iFrame, cGlobal, limiterErrorDescT.m_tolerance, limiterErrorDescT.m_deltaFactor,
						(limiterErrorDescT.m_deltaFactor <= 0.0f) ? 0.0f : (fErrorTolerance * limiter.m_fValue - limiterErrorDescT.m_tolerance) / limiterErrorDescT.m_deltaFactor, limiter.m_fValue);
				}
				break;
			case JointErrorToleranceLimiter::kTypeTranslationInherited:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= %20s.Q f%3u %c <=...\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal);
				break;
			default:
				INOTE(IMsg::kDebug, "%20s.Q %.5f <= ??? f%3u (type:%04x)\n", joint_name.c_str(), fErrorTolerance, iFrame, limiter.m_eType);
				break;
			}
		}
		if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
			unsigned iFrame = jointLimiterFrame_translation[iJointAnimIndex];
			JointErrorToleranceLimiter const& limiter = jointErrorTolerance.translation_limiter[iJointAnimIndex][iFrame];
			ChannelRootSpaceError const& limiterErrorDesc = jointErrorDescs[ limiter.m_iJoint ].m_translation;
			std::string limiter_name = GetLeafName(hierarchyDesc.m_joints[ limiter.m_iJoint ].m_name);
			float fErrorTolerance = jointErrorTolerance.translation[iJointAnimIndex][iFrame];
			char cGlobal = (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobal) ? 'G' : (limiter.m_eType & JointErrorToleranceLimiter::kTypeFlagGlobalRadial) ? 'R' : ' ';
			switch (limiter.m_eType &~ JointErrorToleranceLimiter::kTypeMaskFlags) {
			case JointErrorToleranceLimiter::kTypeError:
				INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T f%3u %c(%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance);
				break;
			case JointErrorToleranceLimiter::kTypeDelta:
				INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T f%3u %c(%.5f+%.4f*rd%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance, limiterErrorDesc.m_deltaFactor, limiter.m_fValue);
				break;
			case JointErrorToleranceLimiter::kTypeObjectRootDelta:
				INOTE(IMsg::kDebug, "%20s.T %.5f <= %20s.T f%3u %c(%.5f+%.4f*md%.5f)\n", joint_name.c_str(), fErrorTolerance,
					limiter_name.c_str(), iFrame, cGlobal, limiterErrorDesc.m_tolerance, limiterErrorDesc.m_deltaFactor, limiter.m_fValue);
				break;
			default:
				INOTE(IMsg::kDebug, "%20s.T %.5f <= ??? f%3u (type:%04x)\n", joint_name.c_str(), fErrorTolerance, iFrame, limiter.m_eType);
				break;
			}
		}
	}
}

// initialises the rootSpaceJointErrorParams array from the parameters given in the targetJoints array
void InitRootSpaceJointErrorParams( 
	std::vector<JointRootSpaceError>&	rootSpaceJointErrorParams,	// output array, size is numJoints
	RootSpaceErrorTargetJoints const&				targetJoints,				// contains default params & any overrides for joints named in the metadata
	bool const										bDeltaOptimization,			
	unsigned const									numJoints )
{
	// 1. get default root space error parameters for joints
	JointRootSpaceError defaultParams;
	{
		// find index of joint target with the default parameters
		unsigned const iDefaultTarget = FindErrorParamIndexJoints(targetJoints, kChannelIndexDefault);
		bool isValid = (targetJoints[iDefaultTarget].m_iJoint == kChannelIndexDefault);

		// init defaultParams from target
		JointRootSpaceError const& defaultTarget = targetJoints[iDefaultTarget].m_error;
		if (isValid) {
			defaultParams.m_scale.m_tolerance = defaultTarget.m_scale.m_tolerance;
			defaultParams.m_rotation.m_tolerance = defaultTarget.m_rotation.m_tolerance;
			defaultParams.m_translation.m_tolerance = defaultTarget.m_translation.m_tolerance;
		} else {
			defaultParams.m_scale.m_tolerance =
				defaultParams.m_rotation.m_tolerance =
				defaultParams.m_translation.m_tolerance = 0.0f;
		}
		if (isValid && bDeltaOptimization) {
			defaultParams.m_scale.m_deltaFactor = defaultTarget.m_scale.m_deltaFactor;
			defaultParams.m_rotation.m_deltaFactor = defaultTarget.m_rotation.m_deltaFactor;
			defaultParams.m_translation.m_deltaFactor = defaultTarget.m_translation.m_deltaFactor;
			defaultParams.m_scale.m_speedFactor = defaultTarget.m_scale.m_speedFactor;
			defaultParams.m_rotation.m_speedFactor = defaultTarget.m_rotation.m_speedFactor;
			defaultParams.m_translation.m_speedFactor = 0.0f;
		} else {
			defaultParams.m_scale.m_deltaFactor =
				defaultParams.m_rotation.m_deltaFactor =
				defaultParams.m_translation.m_deltaFactor =
				defaultParams.m_scale.m_speedFactor =
				defaultParams.m_rotation.m_speedFactor =
				defaultParams.m_translation.m_speedFactor = 0.0f;
		}
	}

	// 2. initialise error params array with defaults
	rootSpaceJointErrorParams.assign(numJoints, defaultParams);

	// 3. override those defaults for any joints that have been named in the meta data (the targets)
	for (unsigned iTarget = 0; iTarget < targetJoints.size(); ++iTarget) {
		unsigned iJoint = targetJoints[iTarget].m_iJoint;
		if (iJoint < numJoints) {
			JointRootSpaceError const& src = targetJoints[iTarget].m_error;
			JointRootSpaceError& dst = rootSpaceJointErrorParams[iJoint];
			dst.m_scale.m_tolerance = src.m_scale.m_tolerance;
			dst.m_rotation.m_tolerance = src.m_rotation.m_tolerance;
			dst.m_translation.m_tolerance = src.m_translation.m_tolerance;
			if (bDeltaOptimization) {
				dst.m_scale.m_deltaFactor = src.m_scale.m_deltaFactor;
				dst.m_rotation.m_deltaFactor = src.m_rotation.m_deltaFactor;
				dst.m_translation.m_deltaFactor = src.m_translation.m_deltaFactor;
				dst.m_scale.m_speedFactor = src.m_scale.m_speedFactor;
				dst.m_rotation.m_speedFactor = src.m_rotation.m_speedFactor;
			}
		}
	}
}

// generate local space error tolerances from root space tolerances for the target joints & floats
void AnimClipCompression_GenerateLocalSpaceErrors(
	ClipLocalSpaceErrors& localSpaceErrors,			// [output] filled out with local space error tolerances per joint animation and float animation
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationHierarchyDesc const& hierarchyDesc,				// data describing hierarchical relationships between joints and float channels
	AnimationClipSourceData const& sourceData,					// source animation data
	RootSpaceErrorTargetJoints const& targetJoints,				// root space error tolerance parameters for joints
	RootSpaceErrorTargetFloats const& targetFloatChannels,		// root space error tolerance parameters for float channels
	bool bDeltaOptimization)									// enable delta optimization of error tolerances
{
	unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	unsigned const numTargetFloats = (unsigned)targetFloatChannels.size();
	
	bool const bDumpErrors = ShouldNote(IMsg::kDebug);

	// traverse the hierarchy to determine local space error tolerances per joint per frame
	JointErrorTolerances jointErrorTolerance(numJointAnims, bDumpErrors);
	{
		// initialise array of root space error parameters
		std::vector<JointRootSpaceError> rootSpaceJointErrorParams;	// size == numJoints
		InitRootSpaceJointErrorParams(rootSpaceJointErrorParams, targetJoints, bDeltaOptimization, numJoints);

		// Recurse into the joint hierarchy to calculate world space matrices, 
		// collecting 3 frames worth of data for all joints in a circular buffer.
		// Once we have collected 3 frames worth of data, calculate error tolerances 
		// for the center frame based on local error tolerances, root space deltas
		// (with and without object root movement), and error tolerances inherited from children.
		LocalSpaceErrorToleranceCalculator localSpaceErrorCalculator(hierarchyDesc.m_joints, binding, sourceData, rootSpaceJointErrorParams, jointErrorTolerance, bDumpErrors);
		localSpaceErrorCalculator.GeneratePosesForFrames((sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames-1 : 0, 0, 1);
		localSpaceErrorCalculator.GenerateLocalSpaceErrorTolerances();
		for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames-1; ++iFrame) {
			localSpaceErrorCalculator.GeneratePosesForFrames(iFrame-1, iFrame, iFrame+1);
			localSpaceErrorCalculator.GenerateLocalSpaceErrorTolerances();
		}
		localSpaceErrorCalculator.GeneratePosesForFrames(sourceData.m_numFrames-2, sourceData.m_numFrames-1, (sourceData.m_flags & kClipLooping) ? 0 : sourceData.m_numFrames-1);
		localSpaceErrorCalculator.GenerateLocalSpaceErrorTolerances();

		if (bDumpErrors) {
			DumpLocalFromRootSpaceErrors(numJoints, numJointAnims, sourceData, binding, hierarchyDesc, jointErrorTolerance, rootSpaceJointErrorParams);
		}
	}

	// Calculate float error tolerances
	std::vector< std::vector<float> > floatErrorTolerance(numFloatAnims);	//[iFloatAnimIndex][iFrame]
	{
		// get index of default error parameters
		unsigned const iTargetFloat_Default = FindErrorParamIndexFloats(targetFloatChannels, kChannelIndexDefault);
		ITASSERT(targetFloatChannels[iTargetFloat_Default].m_iFloatChannel == kChannelIndexDefault);

		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			unsigned iTarget = iTargetFloat_Default;
			for (unsigned iTargetFloat = 0; iTargetFloat < numTargetFloats; ++iTargetFloat) {
				if (targetFloatChannels[iTargetFloat].m_iFloatChannel == iFloatChannel) {
					iTarget = iTargetFloat;
					break;
				}
			}
			if (iTarget >= numTargetFloats) {
				continue;
			}
			ChannelRootSpaceError const& floatChannelErrorDesc = targetFloatChannels[iTarget].m_error;
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) 
			{
				floatErrorTolerance[iFloatAnimIndex].resize(1);
				floatErrorTolerance[iFloatAnimIndex][0] = floatChannelErrorDesc.m_tolerance;
			} 
			else if (bDeltaOptimization && (floatChannelErrorDesc.m_deltaFactor > 0.0f)) 
			{
				SampledAnim const& floatAnim = *sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
				float f0 = floatAnim[0], f1 = floatAnim[1], f2;
				floatErrorTolerance[iFloatAnimIndex].clear();
				floatErrorTolerance[iFloatAnimIndex].resize(sourceData.m_numFrames, 0.0f);
				if (sourceData.m_flags & kClipLooping) {
					float fDelta = GetDelta(floatAnim[sourceData.m_numFrames-1], f0, f1);
					floatErrorTolerance[iFloatAnimIndex][0] = floatChannelErrorDesc.m_tolerance + floatChannelErrorDesc.m_deltaFactor * fDelta;
				} else {
					float fDelta = GetDelta(f0, f0, f1);
					floatErrorTolerance[iFloatAnimIndex][0] = floatChannelErrorDesc.m_tolerance + floatChannelErrorDesc.m_deltaFactor * fDelta;
				}
				for (unsigned iFrame = 2; iFrame < sourceData.m_numFrames; ++iFrame, f0 = f1, f1 = f2) {
					f2 = floatAnim[iFrame];
					float fDelta = GetDelta(f0, f1, f2);
					floatErrorTolerance[iFloatAnimIndex][iFrame-1] = floatChannelErrorDesc.m_tolerance + floatChannelErrorDesc.m_deltaFactor * fDelta;
				}
				if (sourceData.m_flags & kClipLooping) {
					float fDelta = GetDelta((float)floatAnim[sourceData.m_numFrames-2], floatAnim[sourceData.m_numFrames-1], floatAnim[0]);
					floatErrorTolerance[iFloatAnimIndex][sourceData.m_numFrames-1] = floatChannelErrorDesc.m_tolerance + floatChannelErrorDesc.m_deltaFactor * fDelta;
				} else {
					float fDelta = GetDelta((float)floatAnim[sourceData.m_numFrames-2], floatAnim[sourceData.m_numFrames-1], floatAnim[sourceData.m_numFrames-1]);
					floatErrorTolerance[iFloatAnimIndex][sourceData.m_numFrames-1] = floatChannelErrorDesc.m_tolerance + floatChannelErrorDesc.m_deltaFactor * fDelta;
				}
			} else {
				floatErrorTolerance[iFloatAnimIndex].clear();
				floatErrorTolerance[iFloatAnimIndex].resize(sourceData.m_numFrames, floatChannelErrorDesc.m_tolerance);
			}
		}
	}

	// fill out localSpaceErrors from jointErrorTolerance, floatErrorTolerance
	localSpaceErrors.init(numJointAnims, numFloatAnims);
	{
		// joints
		for (unsigned iJointAnim = 0; iJointAnim < numJointAnims; ++iJointAnim) {
			unsigned iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[ iJointAnim ];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			std::vector<float> const& errorTolerance_scale = jointErrorTolerance.scale[iJointAnim];
			std::vector<float> const& errorTolerance_rotation = jointErrorTolerance.rotation[iJointAnim];
			std::vector<float> const& errorTolerance_translation = jointErrorTolerance.translation[iJointAnim];
			// use per-frame tolerances for key compression
			localSpaceErrors.m_key[kChannelTypeScale][iJointAnim] = errorTolerance_scale;
			localSpaceErrors.m_key[kChannelTypeRotation][iJointAnim] = errorTolerance_rotation;
			localSpaceErrors.m_key[kChannelTypeTranslation][iJointAnim] = errorTolerance_translation;
			// find minimum tolerances to use for bit compression
			float fErrorTolerance_Scale = 0.0f, fErrorTolerance_Rotation = 0.0f, fErrorTolerance_Translation = 0.0f;
			if (!errorTolerance_scale.empty()) {
				fErrorTolerance_Scale = errorTolerance_scale[0];
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnim)) {
					for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames; ++iFrame) {
						if (fErrorTolerance_Scale > errorTolerance_scale[iFrame]) {
							fErrorTolerance_Scale = errorTolerance_scale[iFrame];
						}
					}
				}
			}
			if (!errorTolerance_rotation.empty()) {
				fErrorTolerance_Rotation = errorTolerance_rotation[0];
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnim)) {
					for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames; ++iFrame) {
						if (fErrorTolerance_Rotation > errorTolerance_rotation[iFrame]) {
							fErrorTolerance_Rotation = errorTolerance_rotation[iFrame];
						}
					}
				}
			}
			if (!errorTolerance_translation.empty()) {
				fErrorTolerance_Translation = errorTolerance_translation[0];
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnim)) {
					for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames; ++iFrame) {
						if (fErrorTolerance_Translation > errorTolerance_translation[iFrame]) {
							fErrorTolerance_Translation = errorTolerance_translation[iFrame];
						}
					}
				}
			}
			localSpaceErrors.m_bit[kChannelTypeScale][iJointAnim] = fErrorTolerance_Scale;
			localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnim] = fErrorTolerance_Rotation;
			localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnim] = fErrorTolerance_Translation;
		}
		// floats
		for (unsigned iFloatAnim = 0; iFloatAnim < numFloatAnims; ++iFloatAnim) {
			unsigned iFloatChannel = binding.m_floatAnimIdToFloatId[ iFloatAnim ];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			std::vector<float> const& errorTolerance_float = floatErrorTolerance[iFloatAnim];
			// use per-frame tolerances for key compression
			localSpaceErrors.m_key[kChannelTypeScalar][iFloatAnim] = errorTolerance_float;
			// find minimum tolerance to use for bit compression
			float fErrorTolerance_Float = 0.0f;
			if (!errorTolerance_float.empty()) {
				fErrorTolerance_Float = errorTolerance_float[0];
				if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnim)) {
					for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames; ++iFrame) {
						if (fErrorTolerance_Float > errorTolerance_float[iFrame]) {
							fErrorTolerance_Float = errorTolerance_float[iFrame];
						}
					}
				}
			}
			localSpaceErrors.m_bit[kChannelTypeScalar][iFloatAnim] = fErrorTolerance_Float;
		}
	}
}

// end of AnimClipCompression_GenerateLocalSpaceErrors functions
//------------------------------------------------------------------------------------------------

// Fill in the constant compression formats for any constant joint params that have not been defined yet.
bool AnimClipCompression_SetDefaultConstFormats(
	ClipCompressionDesc& compression,					// [input/output] compression description to fill out
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	AnimChannelCompressionType constCompressionTypeScale,		// compression type to apply to constant joint scale channels
	AnimChannelCompressionType constCompressionTypeQuat,		// compression type to apply to constant joint rotation channels
	AnimChannelCompressionType constCompressionTypeTrans,		// compression type to apply to constant joint translation channels
	AnimChannelCompressionType constCompressionTypeFloat		// compression type to apply to constant float channels
	)
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	Vec3BitPackingFormat bitFormatScale, bitFormatQuat, bitFormatTrans, bitFormatFloat;
	if (constCompressionTypeScale != kAcctConstVec3Auto) {
		bitFormatScale = GetConstCompressionBitFormat(constCompressionTypeScale);
	}
	if (constCompressionTypeQuat != kAcctConstQuatAuto) {
		bitFormatQuat = GetConstCompressionBitFormat(constCompressionTypeQuat);
	}
	if (constCompressionTypeTrans != kAcctConstVec3Auto) {
		bitFormatTrans = GetConstCompressionBitFormat(constCompressionTypeTrans);
	}
	if (constCompressionTypeFloat != kAcctConstFloatAuto) {
		bitFormatFloat = GetConstCompressionBitFormat(constCompressionTypeFloat);
	}

	std::vector<ChannelCompressionDesc>& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc>& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc>& formatTrans = compression.m_format[kChannelTypeTranslation];
	std::vector<ChannelCompressionDesc>& formatFloat = compression.m_format[kChannelTypeScalar];

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex++) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex) && !(formatScale[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined)) {
			formatScale[iAnimatedJoint].m_compressionType = constCompressionTypeScale;
			if (constCompressionTypeScale != kAcctConstVec3Auto) {
				formatScale[iAnimatedJoint].m_bitFormat = bitFormatScale;
				formatScale[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
			}
		}
		if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex) && !(formatQuat[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined)) {
			formatQuat[iAnimatedJoint].m_compressionType = constCompressionTypeQuat;
			if (constCompressionTypeQuat != kAcctConstQuatAuto) {
				formatQuat[iAnimatedJoint].m_bitFormat = bitFormatQuat;
				formatQuat[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
			}
		}
		if (sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex) && !(formatTrans[iAnimatedJoint].m_flags & kFormatFlagBitFormatDefined)) {
			formatTrans[iAnimatedJoint].m_compressionType = constCompressionTypeTrans;
			if (constCompressionTypeTrans != kAcctConstVec3Auto) {
				formatTrans[iAnimatedJoint].m_bitFormat = bitFormatTrans;
				formatTrans[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
			}
		}
	}
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex++) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex) && !(formatFloat[iFloatChannel].m_flags & kFormatFlagBitFormatDefined)) {
			formatFloat[iFloatChannel].m_compressionType = constCompressionTypeFloat;
			if (constCompressionTypeFloat != kAcctConstFloatAuto) {
				formatFloat[iFloatChannel].m_bitFormat = bitFormatFloat;
				formatFloat[iFloatChannel].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
			}
		}
	}
	return true;
}

bool AnimClipCompression_FinalizeConstFormats(
	ClipCompressionDesc& compression,					// [input/output] compression description to fill out
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	ClipLocalSpaceErrors const& localSpaceErrors		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
	)
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	ITGEOM::Vec3Array avValues(1);
	ITGEOM::QuatArray aqValues(1);
	std::vector<float> afValues(1);

	std::vector<ChannelCompressionDesc>& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc>& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc>& formatTrans = compression.m_format[kChannelTypeTranslation];
	std::vector<ChannelCompressionDesc>& formatFloat = compression.m_format[kChannelTypeScalar];

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
			if (sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeScale][iJointAnimIndex];
				AnimChannelCompressionType compressionType = formatScale[iAnimatedJoint].m_compressionType;
				avValues[0] = (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0];
				if (compressionType == kAcctConstVec3Auto) {
					float fError = CalculateVec3Float16Error(avValues);
					formatScale[iAnimatedJoint].m_compressionType = (fError > fErrorTolerance) ? kAcctConstVec3Uncompressed : kAcctConstVec3Float16;
					formatScale[iAnimatedJoint].m_bitFormat = GetConstCompressionBitFormat(formatScale[iAnimatedJoint].m_compressionType);
					formatScale[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
				} else {
					switch (compressionType) {
					case kAcctConstVec3Uncompressed:	// uncompressed is always within error tolerance
						break;
					case kAcctConstVec3Float16:	{
						float fError = CalculateVec3Float16Error(avValues);
						if (fError > fErrorTolerance) {
							IWARN("AnimClipCompression_FinalizeConstFormats: ajoint[%u].scale const value compressed with float16 compression has error %f > tolerance %f\n", iAnimatedJoint, fError, fErrorTolerance);
						}
						break;
					}
					default:
						ITASSERT((unsigned)(compressionType - kAcctConstVec3Uncompressed) < kAcctConstVec3NumCompressionTypes);
						break;
					}
				}
			}
		}
		if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
			if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeRotation][iJointAnimIndex];
				AnimChannelCompressionType compressionType = formatQuat[iAnimatedJoint].m_compressionType;
				aqValues[0] = (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0];
				if (compressionType == kAcctConstQuatAuto) {
					float fError = CalculateQuat48SmallestThreeError(aqValues);
					formatQuat[iAnimatedJoint].m_compressionType = (fError > fErrorTolerance) ? kAcctConstQuatUncompressed : kAcctConstQuat48SmallestThree;
					formatQuat[iAnimatedJoint].m_bitFormat = GetConstCompressionBitFormat(formatQuat[iAnimatedJoint].m_compressionType);
					formatQuat[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
				} else {
					switch (compressionType) {
					case kAcctConstQuatUncompressed:	// uncompressed is always within error tolerance
						break;
					case kAcctConstQuat48SmallestThree: {
						float fError = CalculateQuat48SmallestThreeError(aqValues);
						if (fError > fErrorTolerance) {
							IWARN("AnimClipCompression_FinalizeConstFormats: ajoint[%u].rotation const value compressed with smallest3_48 compression has error %f > tolerance %f\n", iAnimatedJoint, fError, fErrorTolerance);
						}
						break;
					}
					default:
						ITASSERT((unsigned)(compressionType - kAcctConstQuatUncompressed) < kAcctConstQuatNumCompressionTypes);
						break;
					}
				}
			}
		}
		if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
			if (sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				float fErrorTolerance = localSpaceErrors.m_bit[kChannelTypeTranslation][iJointAnimIndex];
				AnimChannelCompressionType compressionType = formatTrans[iAnimatedJoint].m_compressionType;
				avValues[0] = (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0];
				if (compressionType == kAcctConstVec3Auto) {
					float fError = CalculateVec3Float16Error(avValues);
					formatTrans[iAnimatedJoint].m_compressionType = (fError > fErrorTolerance) ? kAcctConstVec3Uncompressed : kAcctConstVec3Float16;
					formatTrans[iAnimatedJoint].m_bitFormat = GetConstCompressionBitFormat(formatTrans[iAnimatedJoint].m_compressionType);
					formatTrans[iAnimatedJoint].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
				} else {
					switch (compressionType) {
					case kAcctConstVec3Uncompressed:	// uncompressed is always within error tolerance
						break;
					case kAcctConstVec3Float16: {
						float fError = CalculateVec3Float16Error(avValues);
						if (fError > fErrorTolerance) {
							IWARN("AnimClipCompression_FinalizeConstFormats: ajoint[%u].translation const value compressed with float16 compression has error %f > tolerance %f\n", iAnimatedJoint, fError, fErrorTolerance);
						}
						break;
					}
					default:
						ITASSERT((unsigned)(compressionType - kAcctConstVec3Uncompressed) < kAcctConstVec3NumCompressionTypes);
						break;
					}
				}
			}
		}
	}
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				AnimChannelCompressionType compressionType = formatFloat[iFloatChannel].m_compressionType;
				afValues[0] = (*sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex))[0];
				if (compressionType == kAcctConstFloatAuto) {
					formatFloat[iFloatChannel].m_compressionType = kAcctConstFloatUncompressed;
					formatFloat[iFloatChannel].m_bitFormat = GetConstCompressionBitFormat(formatFloat[iFloatChannel].m_compressionType);
					formatFloat[iFloatChannel].m_flags |= kFormatFlagConstant | kFormatFlagBitFormatDefined;
				} else {
					switch (compressionType) {
					case kAcctConstFloatUncompressed:	// uncompressed is always within error tolerance
						break;
					default:
						ITASSERT((unsigned)(compressionType - kAcctConstFloatUncompressed) < kAcctConstFloatNumCompressionTypes);
						break;
					}
				}
			}
		}
	}
	return true;
}

// helper to convert number of bits to bytes (rounding up to next byte multiple)
static inline size_t NumBytes(size_t numBits)
{
	return Align(numBits, 8) / 8; 
}

bool AnimClipCompression_Finalize(
	ClipCompressionDesc& compression,					// [input/output] compression description to finalize
	AnimationHierarchyDesc const& /*hierarchyDesc*/,			// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,							// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,					// source animation data
	AnimationClipCompressionStats* pStats )						// [output] statistics describing the resulting compression rates
{
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);

	AnimationClipCompressionStats stats_local;
	AnimationClipCompressionStats& stats = pStats ? *pStats : stats_local;
	stats.init(	kAcctVec3NumCompressionTypes, 
				kAcctQuatNumCompressionTypes, 
				kAcctFloatNumCompressionTypes, 
				kAcctConstVec3NumCompressionTypes, 
				kAcctConstQuatNumCompressionTypes, 
				kAcctConstFloatNumCompressionTypes);

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	std::vector<size_t> bitsizeofScaleKeyByType(kAcctVec3NumCompressionTypes, 0);
	std::vector<size_t> bitsizeofQuatKeyByType(kAcctQuatNumCompressionTypes, 0);
	std::vector<size_t> bitsizeofTransKeyByType(kAcctVec3NumCompressionTypes, 0);
	std::vector<size_t> bitsizeofFloatKeyByType(kAcctFloatNumCompressionTypes, 0);

	size_t sizeofConstScales = 0;
	size_t sizeofConstQuats = 0;
	size_t sizeofConstTrans = 0;
	size_t sizeofConstFloats = 0;

	std::vector<ChannelCompressionDesc>& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc>& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc>& formatTrans = compression.m_format[kChannelTypeTranslation];
	std::vector<ChannelCompressionDesc>& formatFloat = compression.m_format[kChannelTypeScalar];

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex ++) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				stats.m_numScaleJoints++;
				unsigned iType = formatScale[iAnimatedJoint].m_compressionType - kAcctVec3Uncompressed;
				ITASSERT(iType < kAcctVec3NumCompressionTypes);
				bitsizeofScaleKeyByType[iType] += formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
				stats.m_numScalesByType[iType]++;
			} else {
				stats.m_numConstChannels[kChannelTypeScale]++;
				unsigned iType = formatScale[iAnimatedJoint].m_compressionType - kAcctConstVec3Uncompressed;
				ITASSERT(iType < kAcctConstVec3NumCompressionTypes);
				stats.m_numConstScalesByType[iType]++;
				ITASSERT(!(formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal & 0x7));
				sizeofConstScales += formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal / 8;
			}
		} else {
			stats.m_numConstChannelsRemoved[kChannelTypeScale]++;
		}
		if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				stats.m_numQuatJoints++;
				unsigned iType = formatQuat[iAnimatedJoint].m_compressionType - kAcctQuatUncompressed;
				ITASSERTF(iType < kAcctQuatNumCompressionTypes, ("bad rotation compression type 0x%X, joint=%d", formatQuat[iAnimatedJoint].m_compressionType, iJointAnimIndex));
				bitsizeofQuatKeyByType[iType] += formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
				stats.m_numQuatsByType[iType]++;
			} else {
				stats.m_numConstChannels[kChannelTypeRotation]++;
				unsigned iType = formatQuat[iAnimatedJoint].m_compressionType - kAcctConstQuatUncompressed;
				ITASSERT(iType < kAcctConstQuatNumCompressionTypes);
				stats.m_numConstQuatsByType[iType]++;
				ITASSERT(!(formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal & 0x7));
				sizeofConstQuats += formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal / 8;
			}
		} else {
			stats.m_numConstChannelsRemoved[kChannelTypeRotation]++;
		}
		if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				stats.m_numTransJoints++;
				unsigned iType = formatTrans[iAnimatedJoint].m_compressionType - kAcctVec3Uncompressed;
				ITASSERT(iType < kAcctVec3NumCompressionTypes);
				bitsizeofTransKeyByType[iType] += formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
				stats.m_numTransByType[iType]++;
			} else {
				stats.m_numConstChannels[kChannelTypeTranslation]++;
				unsigned iType = formatTrans[iAnimatedJoint].m_compressionType - kAcctConstVec3Uncompressed;
				ITASSERT(iType < kAcctConstVec3NumCompressionTypes);
				stats.m_numConstTransByType[iType]++;
				ITASSERT(!(formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal & 0x7));
				sizeofConstTrans += formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal / 8;
			}
		} else {
			stats.m_numConstChannelsRemoved[kChannelTypeTranslation]++;
		}
	}
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex ++) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				stats.m_numFloatChannels++;
				unsigned iType = formatFloat[iFloatChannel].m_compressionType - kAcctFloatUncompressed;
				ITASSERT(iType < kAcctFloatNumCompressionTypes);
				bitsizeofFloatKeyByType[iType] += formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal;
				stats.m_numFloatsByType[iType]++;
			} else {
				stats.m_numConstChannels[kChannelTypeScalar]++;
				unsigned iType = formatFloat[iFloatChannel].m_compressionType - kAcctConstFloatUncompressed;
				ITASSERT(iType < kAcctConstFloatNumCompressionTypes);
				stats.m_numConstFloatsByType[iType]++;
				ITASSERT(!(formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal & 0x7));
				sizeofConstFloats += formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal / 8;
			}
		} else {
			stats.m_numConstChannelsRemoved[kChannelTypeScalar]++;
		}
	}
	size_t bitsizeofScaleKey = 0, bitsizeofQuatKey = 0, bitsizeofTransKey = 0, bitsizeofFloatKey = 0;
	for (unsigned iType = 0; iType < (unsigned)kAcctVec3NumCompressionTypes; iType++) {
		if (bitsizeofScaleKeyByType[iType] > 0) {
			stats.m_numScaleCompressionTypes++;
			bitsizeofScaleKey += bitsizeofScaleKeyByType[iType];
		}
		if (bitsizeofTransKeyByType[iType] > 0) {
			stats.m_numTransCompressionTypes++;
			bitsizeofTransKey += bitsizeofTransKeyByType[iType];
		}
	}
	for (unsigned iType = 0; iType < (unsigned)kAcctQuatNumCompressionTypes; iType++) {
		if (bitsizeofQuatKeyByType[iType] > 0) {
			stats.m_numQuatCompressionTypes++;
			bitsizeofQuatKey += bitsizeofQuatKeyByType[iType];
		}
	}
	for (unsigned iType = 0; iType < (unsigned)kAcctFloatNumCompressionTypes; iType++) {
		if (bitsizeofFloatKeyByType[iType] > 0) {
			stats.m_numFloatCompressionTypes++;
			bitsizeofFloatKey += bitsizeofFloatKeyByType[iType];
		}
	}
	stats.m_sizeofScaleKey = (bitsizeofScaleKey + 7)/8;
	stats.m_sizeofQuatKey = (bitsizeofQuatKey + 7)/8;
	stats.m_sizeofTransKey = (bitsizeofTransKey + 7)/8;
	stats.m_sizeofFloatKey = (bitsizeofFloatKey + 7)/8;

	// num bytes without compression
	stats.m_uncompressedSize = (Align(12 * numAnimatedJoints, 16) + 16 * numAnimatedJoints + Align(12 * numAnimatedJoints, 16) + Align(4 * numFloatChannels, 16)) * numOutputFrames;
	
	// size of constant compressed channels
	stats.m_sizeofConsts = Align(sizeofConstScales, 16) + Align(sizeofConstQuats, 16) + Align(sizeofConstTrans, 16) + Align(sizeofConstFloats, 16);
	
	// num bytes with just constant channel compression
	stats.m_constantCompressedSize = stats.m_sizeofConsts +	
		(Align(12 * stats.m_numScaleJoints, 16) + 16 * stats.m_numQuatJoints + Align(12 * stats.m_numTransJoints, 16) + Align(4 * stats.m_numFloatChannels, 16)) * numOutputFrames;
	
	// num bytes with constant channel & bit compression
	stats.m_bitCompressedSize = stats.m_sizeofConsts + 
		Align(NumBytes(bitsizeofScaleKey + bitsizeofQuatKey + bitsizeofTransKey + bitsizeofFloatKey), 16) * numOutputFrames;

	float fKeySkipRatio = 0.0f;
	if (keyCompression == kClipKeysUnshared) 
	{
		compression.m_maxKeysPerBlock = clamp(compression.m_maxKeysPerBlock, unsigned(3), unsigned(33));

		unsigned framesPerBlock = (compression.m_maxKeysPerBlock < 4) ? compression.m_maxKeysPerBlock : 4;
		unsigned const sizeofKeys = (unsigned)(stats.m_sizeofScaleKey + stats.m_sizeofQuatKey + stats.m_sizeofTransKey + stats.m_sizeofFloatKey);
		if (compression.m_maxBlockSize < sizeofKeys * framesPerBlock) {
			compression.m_maxBlockSize = sizeofKeys * framesPerBlock;
		}
		compression.m_maxBlockSize = Align(compression.m_maxBlockSize, 128);

		stats.m_keyCompressedSize = stats.m_sizeofConsts;

		// count num bits for each joint channel for all frames minus num skipped
		unsigned keySkipCount = 0;
		unsigned bitsizeOfScaleValues = 0, bitsizeOfQuatValues = 0, bitsizeOfTransValues = 0, bitsizeOfFloatValues = 0;
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex++) {
			unsigned const iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
					std::set<size_t> const& skipFrames = compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint];
					unsigned skipCount = (unsigned)skipFrames.size();
					bitsizeOfScaleValues += (numOutputFrames - skipCount) * formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
					keySkipCount += skipCount;
				}
			}
			if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
					std::set<size_t> const& skipFrames = compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint];
					unsigned skipCount = (unsigned)skipFrames.size();
					bitsizeOfQuatValues += (numOutputFrames - skipCount) * formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
					keySkipCount += skipCount;
				}
			}
			if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
					std::set<size_t> const& skipFrames = compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint];
					unsigned skipCount = (unsigned)skipFrames.size();
					bitsizeOfTransValues += (numOutputFrames - skipCount) * formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
					keySkipCount += skipCount;
				}
			}
		}

		// approximate runtime size, disregarding padding frame table entries required between compression types:
		unsigned const frameTableSize = 1;
		unsigned numKeyCopyOpsEstimate;
		if (stats.m_numScaleJoints + stats.m_numQuatJoints + stats.m_numTransJoints <= 0) 
		{
			// all channels are constant
			numKeyCopyOpsEstimate = 0;
		} 
		else if (stats.m_numScaleJoints + stats.m_numQuatJoints + stats.m_numTransJoints < kKeyCacheMaxSize) 
		{
			// all animated keys fit into the key cache
			stats.m_keyCompressedSize += Align((stats.m_numScaleJoints + stats.m_numQuatJoints + stats.m_numTransJoints) * frameTableSize + 
											   NumBytes(bitsizeOfScaleValues + bitsizeOfQuatValues + bitsizeOfTransValues), 16);
			numKeyCopyOpsEstimate = 1;
		} 
		else if (stats.m_numScaleJoints + stats.m_numTransJoints < kKeyCacheMaxSize) 
		{
			// only scale & translate keys fit into key cache (quats require another copy to key cache op)
			stats.m_keyCompressedSize += Align((stats.m_numScaleJoints + stats.m_numTransJoints) * frameTableSize + NumBytes(bitsizeOfScaleValues + bitsizeOfTransValues), 16) +
										 Align(stats.m_numQuatJoints * frameTableSize + NumBytes(bitsizeOfQuatValues), 16);
			numKeyCopyOpsEstimate = 2;
		} 
		else 
		{
			// each channel type requires it's own copy to key cache op (most overhead)
			stats.m_keyCompressedSize += Align(stats.m_numScaleJoints * frameTableSize + NumBytes(bitsizeOfScaleValues), 16) +
										 Align(stats.m_numQuatJoints  * frameTableSize + NumBytes(bitsizeOfQuatValues),  16) +
										 Align(stats.m_numTransJoints * frameTableSize + NumBytes(bitsizeOfTransValues), 16);
			numKeyCopyOpsEstimate = 3;
		}

		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex++) 	{
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			if (!sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			std::set<size_t> const& skipFrames = compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel];
			unsigned skipCount = (unsigned)skipFrames.size();
			bitsizeOfFloatValues += ( numOutputFrames - skipCount ) * formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal;
			keySkipCount += skipCount;
		}
		if (stats.m_numFloatChannels > 0) 
		{
			stats.m_keyCompressedSize += Align(stats.m_numFloatChannels * frameTableSize + NumBytes(bitsizeOfFloatValues), 16);
			numKeyCopyOpsEstimate += 1;
		}

		// approximate overhead from block splitting
		{
			unsigned numBlocksApproximate = (unsigned)((stats.m_keyCompressedSize + compression.m_maxBlockSize-1) / compression.m_maxBlockSize);
			if (numOutputFrames-1 > numBlocksApproximate * (compression.m_maxKeysPerBlock-1)) {
				numBlocksApproximate = (numOutputFrames-1 + compression.m_maxKeysPerBlock-1-1) / (compression.m_maxKeysPerBlock-1);
			}
			if (numBlocksApproximate > 1) {
				stats.m_keyCompressedSize += Align(15*(numBlocksApproximate-1)*numKeyCopyOpsEstimate/2, 16);	// 16 byte alignment on average adds 7.5 bytes per block split
			}
		}

		unsigned keyCount = (stats.m_numScaleJoints + stats.m_numQuatJoints + stats.m_numTransJoints + stats.m_numFloatChannels) * numOutputFrames;
		fKeySkipRatio = (float)keySkipCount / (float)keyCount;

		// size due to const, bit & key compression excluding overheads (approximate best case)
		stats.m_keyCompressedSizeNoOverheads = stats.m_sizeofConsts +
			NumBytes(bitsizeOfScaleValues + bitsizeOfQuatValues + bitsizeOfTransValues + bitsizeOfFloatValues);
	} 
	else if (keyCompression == kClipKeysShared) 
	{
		compression.m_maxKeysPerBlock = 0;
		compression.m_maxBlockSize = 0;

		unsigned skipCount = (unsigned)compression.m_sharedSkipFrames.size();
		unsigned const numOutputKeys = numOutputFrames - skipCount;
		stats.m_keyCompressedSize = stats.m_sizeofConsts + 
			(stats.m_sizeofScaleKey + stats.m_sizeofQuatKey + stats.m_sizeofTransKey + stats.m_sizeofFloatKey) * numOutputKeys + Align(sizeof(U16) /*shared time val*/ * numOutputKeys, 16);

		fKeySkipRatio = (float)skipCount / (float)numOutputFrames;
	} 
	else 
	{
		compression.m_maxKeysPerBlock = 0;
		compression.m_maxBlockSize = 0;

		stats.m_keyCompressedSize = stats.m_bitCompressedSize;
	}

	// how many quat log channels were compressed with cross-correlation
	stats.m_numCrossCorrelatedQuatLog = compression.m_jointAnimOrderQuatLog.size();
	stats.m_numCrossCorrelatedQuatLogPca = compression.m_jointAnimOrderQuatLogPca.size();

	INOTE(IMsg::kBrief, "  Compression Stats:\n");
	INOTE(IMsg::kBrief, "    Uncompressed:        %7u bytes %3u joints %3u floats %5u frames\n", stats.m_uncompressedSize, numAnimatedJoints, numFloatChannels, numOutputFrames);
	if (stats.m_constantCompressedSize > 0)
		INOTE(IMsg::kBrief, "    Constant Compressed: %7u bytes %5.2f:1 animated:%3u S,%3u Q,%3u T,%3u F\n", stats.m_constantCompressedSize, (float)stats.m_uncompressedSize / stats.m_constantCompressedSize, stats.m_numScaleJoints, stats.m_numQuatJoints, stats.m_numTransJoints, stats.m_numFloatChannels);
	else
		INOTE(IMsg::kBrief, "    Constant Compressed: %7u bytes [INF]:1 animated:%3u S,%3u Q,%3u T,%3u F\n", stats.m_constantCompressedSize, stats.m_numScaleJoints, stats.m_numQuatJoints, stats.m_numTransJoints, stats.m_numFloatChannels);
	if (stats.m_bitCompressedSize > 0)
		INOTE(IMsg::kBrief, "    Bit Compressed:      %7u bytes %5.2f:1\n", stats.m_bitCompressedSize, (float)stats.m_uncompressedSize / stats.m_bitCompressedSize);
	else
		INOTE(IMsg::kBrief, "    Bit Compressed:      %7u bytes [INF]:1\n", stats.m_bitCompressedSize);
	if (keyCompression == kClipKeysUnshared || keyCompression == kClipKeysShared) {
		if (stats.m_keyCompressedSize > 0) {
			INOTE(IMsg::kBrief, "    Keyframe Compressed: %7u bytes %5.2f:1 may skip %5.2f%% of keys (approximate)\n", stats.m_keyCompressedSize, (float)stats.m_uncompressedSize / stats.m_keyCompressedSize, fKeySkipRatio * 100.0f);
			INOTE(IMsg::kBrief, "    Keyframe Compressed: %7u bytes %5.2f:1 may skip %5.2f%% of keys (no overheads approximate)\n", stats.m_keyCompressedSizeNoOverheads, (float)stats.m_uncompressedSize / stats.m_keyCompressedSizeNoOverheads, fKeySkipRatio * 100.0f);
		} else {
			INOTE(IMsg::kBrief, "    Keyframe Compressed: %7u bytes [INF]:1 may skip %5.2f%% of keys (approximate)\n", stats.m_keyCompressedSize, fKeySkipRatio * 100.0f);
			INOTE(IMsg::kBrief, "    Keyframe Compressed: %7u bytes [INF]:1 may skip %5.2f%% of keys (no overheads approximate)\n", stats.m_keyCompressedSizeNoOverheads, fKeySkipRatio * 100.0f);
		}
	}

	return true;
}

bool AnimClipCompression_SetUncompressed(
	ClipCompressionDesc& compression,					// [output] compression description to fill out
	AnimMetaData const& metaData,
	AnimationHierarchyDesc const& hierarchyDesc,		// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,					// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData& sourceData,				// source animation data
	AnimationClipCompressionStats* stats)				//!< [output] statistics describing the resulting compression rates
{
	// expand const tolerances from metadata into localSpaceErrors
	ClipLocalSpaceErrors tolerances;	//NOTE: only const tolerances are used
	BindAnimMetaDataConstantErrorTolerancesToClip(tolerances, metaData, hierarchyDesc, binding);

	// determine which channels are constant using tolerances
	sourceData.DetermineConstantChannels(tolerances);

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	std::set<size_t> targetAnimatedJoints_Scale, targetAnimatedJoints_Rotation, targetAnimatedJoints_Translation;
	std::set<size_t> targetFloatChannels;

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
			targetAnimatedJoints_Scale.emplace(iAnimatedJoint);
		}
		if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
			targetAnimatedJoints_Rotation.emplace(iAnimatedJoint);
		}
		if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
			targetAnimatedJoints_Translation.emplace(iAnimatedJoint);
		}
	}
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
			targetFloatChannels.emplace(iFloatChannel);
		}
	}

	AnimClipCompression_Init( compression, sourceData.m_flags, 0 /*uniform keys*/ );
	if (!AnimClipCompression_GenerateBitFormats_Vector(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctVec3Uncompressed, targetAnimatedJoints_Scale, targetAnimatedJoints_Translation, false)) {
		return false;
	}
	if (!AnimClipCompression_GenerateBitFormats_Rotation(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctQuatUncompressed, targetAnimatedJoints_Rotation, false)) {
		return false;
	}
	if (!AnimClipCompression_GenerateBitFormats_Float(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctFloatUncompressed, targetFloatChannels, false)) {
		return false;
	}
	if (!AnimClipCompression_SetDefaultConstFormats(compression, binding, sourceData, kAcctConstVec3Uncompressed, kAcctConstQuatUncompressed, kAcctConstVec3Uncompressed, kAcctConstFloatUncompressed)) {
		return false;
	}
	if (!AnimClipCompression_Finalize(compression, hierarchyDesc, binding, sourceData, stats)) {
		return false;
	}
	return true;
}

// Given animation source data and local space error tolerances, set an AnimationClipCompressionDesc to all auto compressed data
bool AnimClipCompression_SetAuto(
	ClipCompressionDesc& compression,				// [output] compression description to fill out
	AnimationHierarchyDesc const& hierarchyDesc,	// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,				// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData& sourceData,			// source animation data
	ClipLocalSpaceErrors const& tolerances )		// as calculated by AnimClipCompression_GenerateLocalSpaceErrors
{
	// determine which channels are constant using tolerances
	sourceData.DetermineConstantChannels(tolerances);

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	std::set<size_t> targetAnimatedJoints_Scale, targetAnimatedJoints_Rotation, targetAnimatedJoints_Translation;
	std::set<size_t> targetFloatChannels;

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints) {
			continue;
		}
		if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
			targetAnimatedJoints_Scale.emplace(iAnimatedJoint);
		}
		if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
			targetAnimatedJoints_Rotation.emplace(iAnimatedJoint);
		}
		if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
			targetAnimatedJoints_Translation.emplace(iAnimatedJoint);
		}
	}
	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloatChannel >= numFloatChannels) {
			continue;
		}
		if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
			targetFloatChannels.emplace(iFloatChannel);
		}
	}

	AnimClipCompression_Init( compression, sourceData.m_flags, 0 /*uniform keys*/ );
	if (!AnimClipCompression_GenerateBitFormats_Vector(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctVec3Auto, targetAnimatedJoints_Scale, targetAnimatedJoints_Translation, false)) {
		return false;
	}
	if (!AnimClipCompression_GenerateBitFormats_Rotation(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctQuatAuto, targetAnimatedJoints_Rotation, false)) {
		return false;
	}
	if (!AnimClipCompression_GenerateBitFormats_Float(compression, hierarchyDesc, binding, sourceData, tolerances, kAcctFloatAuto, targetFloatChannels, false)) {
		return false;
	}
	if (!AnimClipCompression_SetDefaultConstFormats(compression, binding, sourceData, kAcctConstVec3Auto, kAcctConstQuatAuto, kAcctConstVec3Auto, kAcctConstFloatAuto)) {
		return false;
	}
	if (!AnimClipCompression_FinalizeConstFormats(compression, hierarchyDesc, binding, sourceData, tolerances)) {
		return false;
	}
	if (!AnimClipCompression_Finalize(compression, hierarchyDesc, binding, sourceData)) {
		return false;
	}
	return true;
}

template<typename T>
size_t DumpSetAsIntList(std::set<T> const& intSet, const char *szIndent)
{
	size_t numSet = 0;
	for (std::set<T>::const_iterator intSetIt = intSet.begin(); intSetIt != intSet.end(); intSetIt++) {
		if (numSet == 0)
			INOTE(IMsg::kDebug, "%s%3u", szIndent, (unsigned)*intSetIt);
		else if (!(numSet & 0x7))
			INOTE(IMsg::kDebug, ",\n%s%3u", szIndent, (unsigned)*intSetIt);
		else
			INOTE(IMsg::kDebug, ", %3u", (unsigned)*intSetIt);
		numSet++;
	}
	if (numSet)
		INOTE(IMsg::kDebug, "\n");
	return numSet;
}

// Dump the contents of an AnimationClipCompressionDesc as xml to INOTE
void DumpAnimationClipCompression(
	ClipCompressionDesc const& compression,				// finalized compression description
	AnimationHierarchyDesc const& hierarchyDesc,		// data describing hierarchical relationships between joints and float channels
	AnimationBinding const& binding,					// mapping from animations to channels (joints/float channels) and back
	AnimationClipSourceData const& sourceData,			// source animation data
	std::string const& clipName)						// name of this clip
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	unsigned const keyCompressionFlags = compression.m_flags & kClipKeyCompressionMask;

	INOTE(IMsg::kDebug, "<!-- clip=\"%s\" num_frames=%u num_animated_joints=%u num_float_channels=%u-->\n", clipName.c_str(), numOutputFrames, numAnimatedJoints, numFloatChannels);
	unsigned numSkipped;
	unsigned numAnimScale = 0, numConstantScale = 0;
	unsigned numAnimQuat = 0, numConstantQuat = 0;
	unsigned numAnimTrans = 0, numConstantTrans = 0;
	unsigned numAnimFloat = 0, numConstantFloat = 0;

	std::vector<ChannelCompressionDesc> const& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc> const& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc> const& formatTrans = compression.m_format[kChannelTypeTranslation];
	std::vector<ChannelCompressionDesc> const& formatFloat = compression.m_format[kChannelTypeScalar];

	if (keyCompressionFlags == kClipKeysUnshared) {
		INOTE(IMsg::kVerbose, "<compression keys=\"unshared\">\n");
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex++) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints)
				continue;
			unsigned iJoint = (unsigned)binding.m_jointAnimIdToJointId[iJointAnimIndex];
			INOTE(IMsg::kDebug, "\t<joint name=\"%s\">\n", hierarchyDesc.m_joints[iJoint].m_name.c_str());
			if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				if (IsVariableBitPackedFormat(formatScale[iAnimatedJoint].m_compressionType))
					INOTE(IMsg::kDebug, "\t\t<scale format=\"%s\" bitformat=\"%u,%u,%u\"><keyframes><skipframes>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType), formatScale[iAnimatedJoint].m_bitFormat.m_numBitsX, formatScale[iAnimatedJoint].m_bitFormat.m_numBitsY, formatScale[iAnimatedJoint].m_bitFormat.m_numBitsZ);
				else
					INOTE(IMsg::kDebug, "\t\t<scale format=\"%s\"><keyframes><skipframes>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType));
				numSkipped = (unsigned)DumpSetAsIntList(compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint], "\t\t\t");
				INOTE(IMsg::kDebug, "\t\t</skipframes></keyframes></scale><!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7)/8);
				numAnimScale++;
			} else {
				INOTE(IMsg::kDebug, "\t\t<scale constformat=\"%s\"/>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType));
				numConstantScale++;
			}
			if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				if (IsVariableBitPackedFormat(formatQuat[iAnimatedJoint].m_compressionType))
					INOTE(IMsg::kDebug, "\t\t<rotation format=\"%s\" bitformat=\"%u,%u,%u\"><keyframes><skipframes>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType), formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsX, formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsY, formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsZ);
				else
					INOTE(IMsg::kDebug, "\t\t<rotation format=\"%s\"><keyframes><skipframes>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType));
				numSkipped = (unsigned)DumpSetAsIntList(compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint], "\t\t\t");
				INOTE(IMsg::kDebug, "\t\t</skipframes></keyframes></rotation><!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7)/8);
				numAnimQuat++;
			} else {
				INOTE(IMsg::kDebug, "\t\t<rotation constformat=\"%s\"/>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType));
				numConstantQuat++;
			}
			if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				if (IsVariableBitPackedFormat(formatTrans[iAnimatedJoint].m_compressionType))
					INOTE(IMsg::kDebug, "\t\t<translation format=\"%s\" bitformat=\"%u,%u,%u\"><keyframes><skipframes>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType), formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsX, formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsY, formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsZ);
				else
					INOTE(IMsg::kDebug, "\t\t<translation format=\"%s\"><keyframes><skipframes>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType));
				numSkipped = (unsigned)DumpSetAsIntList(compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint], "\t\t\t");
				INOTE(IMsg::kDebug, "\t\t</skipframes></keyframes></translation><!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7)/8);
				numAnimTrans++;
			} else {
				INOTE(IMsg::kDebug, "\t\t<translation constformat=\"%s\"/>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType));
				numConstantTrans++;
			}
			INOTE(IMsg::kDebug, "\t</joint>\n");
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex++) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels)
				continue;
			INOTE(IMsg::kDebug, "\t<floatchannel name=\"%s\">\n", hierarchyDesc.m_floatChannels[iFloatChannel].m_name.c_str());
			if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				if (IsVariableBitPackedFormat(formatFloat[iFloatChannel].m_compressionType))
					INOTE(IMsg::kDebug, "\t\t<float format=\"%s\" bitformat=\"%u\"><keyframes><skipframes>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType), formatFloat[iFloatChannel].m_bitFormat.m_numBitsX);
				else
					INOTE(IMsg::kDebug, "\t\t<float format=\"%s\"><keyframes><skipframes>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType));
				numSkipped = (unsigned)DumpSetAsIntList(compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel], "\t\t\t");
				INOTE(IMsg::kDebug, "\t\t</skipframes></keyframes></float><!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal + 7)/8);
				numAnimFloat++;
			} else {
				INOTE(IMsg::kDebug, "\t\t<float constformat=\"%s\"/>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType));
				numConstantFloat++;
			}
			INOTE(IMsg::kDebug, "\t</floatchannel>\n");
		}
	} else {
		if (keyCompressionFlags == kClipKeysShared) {
			INOTE(IMsg::kDebug, "<compression keys=\"shared\">\n");
			INOTE(IMsg::kDebug, "\t<keyframes><skipframes>\n");
			numSkipped = (unsigned)DumpSetAsIntList(compression.m_sharedSkipFrames, "\t\t");
			INOTE(IMsg::kDebug, "\t</skipframes></keyframes><!-- %u/%u frames remaining -->\n", numOutputFrames - numSkipped, numOutputFrames);
		} else {
			ITASSERT(keyCompressionFlags == kClipKeysUniform || keyCompressionFlags == kClipKeysUniform2 || keyCompressionFlags == kClipKeysHermite);
			INOTE(IMsg::kDebug, "<compression keys=\"uniform\">\n");
			numSkipped = 0;
		}
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; iJointAnimIndex++) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints)
				continue;
			unsigned iJoint = (unsigned)binding.m_jointAnimIdToJointId[iJointAnimIndex];
			INOTE(IMsg::kDebug, "\t<joint name=\"%s\">\n", hierarchyDesc.m_joints[iJoint].m_name.c_str());
			if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
					if (IsVariableBitPackedFormat(formatScale[iAnimatedJoint].m_compressionType))
						INOTE(IMsg::kDebug, "\t\t<scale format=\"%s\" bitformat=\"%u,%u,%u\"/>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType), formatScale[iAnimatedJoint].m_bitFormat.m_numBitsX, formatScale[iAnimatedJoint].m_bitFormat.m_numBitsY, formatScale[iAnimatedJoint].m_bitFormat.m_numBitsZ);
					else
						INOTE(IMsg::kDebug, "\t\t<scale format=\"%s\"/>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType));
					INOTE(IMsg::kDebug, "\t\t<!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7) / 8);
					numAnimScale++;
				} else {
					INOTE(IMsg::kDebug, "\t\t<scale constformat=\"%s\"/>\n", GetFormatAttribName(formatScale[iAnimatedJoint].m_compressionType));
					numConstantScale++;
				}
			}
			if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
					if (IsVariableBitPackedFormat(formatQuat[iAnimatedJoint].m_compressionType))
						INOTE(IMsg::kDebug, "\t\t<rotation format=\"%s\" bitformat=\"%u,%u,%u\"/>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType), formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsX, formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsY, formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsZ);
					else
						INOTE(IMsg::kDebug, "\t\t<rotation format=\"%s\"/>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType));
					INOTE(IMsg::kDebug, "\t\t<!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7) / 8);
					numAnimQuat++;
				} else {
					INOTE(IMsg::kDebug, "\t\t<rotation constformat=\"%s\"/>\n", GetFormatAttribName(formatQuat[iAnimatedJoint].m_compressionType));
					numConstantQuat++;
				}
			}
			if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
					if (IsVariableBitPackedFormat(formatTrans[iAnimatedJoint].m_compressionType))
						INOTE(IMsg::kDebug, "\t\t<translation format=\"%s\" bitformat=\"%u,%u,%u\"/>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType), formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsX, formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsY, formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsZ);
					else
						INOTE(IMsg::kDebug, "\t\t<translation format=\"%s\"/>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType));
					INOTE(IMsg::kDebug, "\t\t<!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal + 7) / 8);
					numAnimTrans++;
				} else {
					INOTE(IMsg::kDebug, "\t\t<translation constformat=\"%s\"/>\n", GetFormatAttribName(formatTrans[iAnimatedJoint].m_compressionType));
					numConstantTrans++;
				}
			}
			INOTE(IMsg::kDebug, "\t</joint>\n");
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; iFloatAnimIndex++) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels)
				continue;
			INOTE(IMsg::kDebug, "\t<floatchannel name=\"%s\">\n", hierarchyDesc.m_floatChannels[iFloatChannel].m_name.c_str());
			if (sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
					if (IsVariableBitPackedFormat(formatFloat[iFloatChannel].m_compressionType))
						INOTE(IMsg::kDebug, "\t\t<float format=\"%s\" bitformat=\"%u\"/>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType), formatFloat[iFloatChannel].m_bitFormat.m_numBitsX);
					else
						INOTE(IMsg::kDebug, "\t\t<float format=\"%s\"/>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType));
					INOTE(IMsg::kDebug, "\t\t<!-- %u bits/frame * %u frames =~ %u bytes -->\n", formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal, numOutputFrames - numSkipped, ((numOutputFrames - numSkipped)*formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal + 7) / 8);
					numAnimFloat++;
				} else {
					INOTE(IMsg::kDebug, "\t\t<float constformat=\"%s\"/>\n", GetFormatAttribName(formatFloat[iFloatChannel].m_compressionType));
					numConstantFloat++;
				}
			}
			INOTE(IMsg::kDebug, "\t</floatchannel>\n");
		}
	}

	// Collect and print compression stats:
	unsigned sizeofUncompressedKeyframeData = 0, sizeofUncompressedKeyframe = 0;
	sizeofUncompressedKeyframeData += numJointAnims*16 + (numJointAnims*2)*12;
	sizeofUncompressedKeyframe += numJointAnims*16 + Align(numJointAnims*2*12, 16);
	sizeofUncompressedKeyframeData += numFloatAnims * 4;
	sizeofUncompressedKeyframe += Align(numFloatAnims * 4, 16);
	unsigned const frameTableSize = 1;
	unsigned totalUncompressed = sizeofUncompressedKeyframe*numOutputFrames;
	unsigned sizeofConstantCompressedKeyframeData = 0, sizeofConstantCompressedKeyframe = 0, sizeofConstantCompressedOffsets = 0;
	unsigned sizeofUncompressedConstantData = 0, sizeofUncompressedConstants = 0;
	unsigned sizeofBitCompressedConstantData = 0, sizeofBitCompressedConstants = 0;
	unsigned sizeofBitCompressedConstantQuats = 0, sizeofBitCompressedConstantScales = 0, sizeofBitCompressedConstantTrans = 0, sizeofBitCompressedConstantFloats = 0;
	unsigned bitsizeofBitCompressedKeyframeData = 0, sizeofBitCompressedKeyframe = 0, sizeofBitCompressedFormatData = 0;
	unsigned bitsizeofBitCompressedQuats = 0, bitsizeofBitCompressedScales = 0, bitsizeofBitCompressedTrans = 0, bitsizeofBitCompressedFloats = 0;
	size_t bitsizeofKeyframeCompressedQuats = 0, bitsizeofKeyframeCompressedScales = 0, bitsizeofKeyframeCompressedTrans = 0, bitsizeofKeyframeCompressedFloats = 0, sizeofKeyframeCompressedTimes = 0;
	unsigned const numGroups = (unsigned)hierarchyDesc.m_channelGroups.size();
	unsigned numJointGroups = 0, numFloatGroups = 0;
	for (unsigned iGroup = 0; iGroup < numGroups; ++iGroup) {
		if (hierarchyDesc.m_channelGroups[iGroup].m_numAnimatedJoints) {
			numJointGroups++;
			unsigned numAnimQuatInGroup = 0, numAnimVectorInGroup = 0, numDefinedJointsInGroup = 0;
			unsigned sizeofBitCompressedConstantQuatsInGroup = 0, sizeofBitCompressedConstantScalesInGroup = 0, sizeofBitCompressedConstantTransInGroup = 0;
			unsigned bitsizeofBitCompressedQuatsInGroup = 0, bitsizeofBitCompressedScalesInGroup = 0, bitsizeofBitCompressedTransInGroup = 0;

			unsigned maxAnimatedJointInGroup = hierarchyDesc.m_channelGroups[iGroup].m_firstAnimatedJoint + hierarchyDesc.m_channelGroups[iGroup].m_numAnimatedJoints;
			for (unsigned iAnimatedJoint = hierarchyDesc.m_channelGroups[iGroup].m_firstAnimatedJoint; iAnimatedJoint < maxAnimatedJointInGroup; iAnimatedJoint++) {
				unsigned iJointAnimIndex = binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
				if (iJointAnimIndex >= numJointAnims)
					continue;
				numDefinedJointsInGroup++;
				if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
					if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
						unsigned bitSize = formatScale[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
						bitsizeofBitCompressedScalesInGroup += bitSize;
						++numAnimVectorInGroup;
						if (keyCompressionFlags == kClipKeysUnshared) {
							size_t numDataFrames = (numOutputFrames - compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].size());
							bitsizeofKeyframeCompressedScales += bitSize * numDataFrames;
							sizeofKeyframeCompressedTimes += frameTableSize;
						}
						sizeofBitCompressedFormatData += GetFormatDataSize(formatScale[iAnimatedJoint].m_compressionType);
					} else {
						sizeofBitCompressedConstantScalesInGroup += (formatScale[iAnimatedJoint].m_compressionType == kAcctConstVec3Uncompressed) ? 12 : 6;
					}
				}
				if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
					if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
						unsigned bitSize = formatQuat[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
						bitsizeofBitCompressedQuatsInGroup += bitSize;
						++numAnimQuatInGroup;
						if (keyCompressionFlags == kClipKeysUnshared) {
							size_t numDataFrames = (numOutputFrames - compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].size());
							bitsizeofKeyframeCompressedQuats += bitSize * numDataFrames;
							sizeofKeyframeCompressedTimes += frameTableSize;
						}
						sizeofBitCompressedFormatData += GetFormatDataSize(formatQuat[iAnimatedJoint].m_compressionType);
					} else {
						sizeofBitCompressedConstantQuatsInGroup += (formatQuat[iAnimatedJoint].m_compressionType == kAcctConstQuatUncompressed) ? 16 : 6;
					}
				}
				if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
					if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
						unsigned bitSize = formatTrans[iAnimatedJoint].m_bitFormat.m_numBitsTotal;
						bitsizeofBitCompressedTransInGroup += bitSize;
						++numAnimVectorInGroup;
						if (keyCompressionFlags == kClipKeysUnshared) {
							size_t numDataFrames = (numOutputFrames - compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].size());
							bitsizeofKeyframeCompressedTrans += bitSize * numDataFrames;
							sizeofKeyframeCompressedTimes += frameTableSize;
						}
						sizeofBitCompressedFormatData += GetFormatDataSize(formatTrans[iAnimatedJoint].m_compressionType);
					} else {
						sizeofBitCompressedConstantTransInGroup += (formatTrans[iAnimatedJoint].m_compressionType == kAcctConstVec3Uncompressed) ? 12 : 6;
					}
				}
			}

			sizeofConstantCompressedKeyframeData += numAnimQuatInGroup * 16 + numAnimVectorInGroup * 12;
			sizeofConstantCompressedKeyframe += numAnimQuatInGroup * 16 + Align(numAnimVectorInGroup*12, 16);
			sizeofConstantCompressedOffsets += Align(numAnimQuatInGroup*2, 16) + Align(numAnimVectorInGroup*2, 16);
			sizeofUncompressedConstantData += (numDefinedJointsInGroup - numAnimQuatInGroup) * 16 + (numDefinedJointsInGroup*2 - numAnimVectorInGroup)*12;
			sizeofUncompressedConstants += (numDefinedJointsInGroup - numAnimQuatInGroup) * 16 + Align((numDefinedJointsInGroup*2 - numAnimVectorInGroup)*12, 16);
			sizeofConstantCompressedOffsets += Align((numDefinedJointsInGroup - numAnimQuatInGroup) * 2, 16) + Align((numDefinedJointsInGroup*2 - numAnimVectorInGroup) * 2, 16);

			sizeofBitCompressedConstantQuats += sizeofBitCompressedConstantQuatsInGroup;
			sizeofBitCompressedConstantScales += sizeofBitCompressedConstantScalesInGroup;
			sizeofBitCompressedConstantTrans += sizeofBitCompressedConstantTransInGroup;
			sizeofBitCompressedConstantData += sizeofBitCompressedConstantQuatsInGroup + sizeofBitCompressedConstantScalesInGroup + sizeofBitCompressedConstantTransInGroup;
			//NOTE: this is not quite correct if there are multiple compression types for one parameter type or if scales and translates do not share the same compression type:
			sizeofBitCompressedConstants += Align(sizeofBitCompressedConstantQuatsInGroup, 16) + Align(sizeofBitCompressedConstantScalesInGroup + sizeofBitCompressedConstantTransInGroup, 16);

			bitsizeofBitCompressedQuats += bitsizeofBitCompressedQuatsInGroup;
			bitsizeofBitCompressedScales += bitsizeofBitCompressedScalesInGroup;
			bitsizeofBitCompressedTrans += bitsizeofBitCompressedTransInGroup;
			bitsizeofBitCompressedKeyframeData += bitsizeofBitCompressedQuatsInGroup + bitsizeofBitCompressedScalesInGroup + bitsizeofBitCompressedTransInGroup;

			if (numAnimQuatInGroup + numAnimVectorInGroup <= 136) {
				sizeofBitCompressedKeyframe += (bitsizeofBitCompressedQuatsInGroup + bitsizeofBitCompressedScalesInGroup + bitsizeofBitCompressedTransInGroup + 7)/8;
			} else if (numAnimVectorInGroup <= 136) {
				sizeofBitCompressedKeyframe += (bitsizeofBitCompressedQuatsInGroup + 7)/8
											+  (bitsizeofBitCompressedScalesInGroup + bitsizeofBitCompressedTransInGroup + 7)/8;
			} else {
				sizeofBitCompressedKeyframe += (bitsizeofBitCompressedQuatsInGroup + 7)/8
											+  (bitsizeofBitCompressedScalesInGroup + 7)/8
											+  (bitsizeofBitCompressedTransInGroup + 7)/8;
			}
		}
		if (hierarchyDesc.m_channelGroups[iGroup].m_numFloatChannels > 0) {
			numFloatGroups++;
			unsigned numAnimFloatInGroup = 0, numDefinedFloatsInGroup = 0;
			unsigned bitsizeofBitCompressedFloatsInGroup = 0;
			unsigned sizeofBitCompressedConstantFloatsInGroup = 0;
			unsigned maxFloatChannelInGroup = hierarchyDesc.m_channelGroups[iGroup].m_firstFloatChannel + hierarchyDesc.m_channelGroups[iGroup].m_numFloatChannels;
			for (unsigned iFloatChannel = hierarchyDesc.m_channelGroups[iGroup].m_firstFloatChannel; iFloatChannel < maxFloatChannelInGroup; iFloatChannel++) {
				unsigned iFloatAnimIndex = binding.m_floatIdToFloatAnimId[iFloatChannel];
				if (iFloatAnimIndex >= numFloatChannels)
					continue;
				numDefinedFloatsInGroup++;
				if (sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
					if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
						unsigned bitSize = formatFloat[iFloatChannel].m_bitFormat.m_numBitsTotal;
						bitsizeofBitCompressedFloatsInGroup += bitSize;
						++numAnimFloatInGroup;
						if (keyCompressionFlags == kClipKeysUnshared) {
							size_t numDataFrames = (numOutputFrames - compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].size());
							bitsizeofKeyframeCompressedFloats += bitSize * numDataFrames;
							sizeofKeyframeCompressedTimes += frameTableSize;
						}
						sizeofBitCompressedFormatData += GetFormatDataSize(formatFloat[iFloatChannel].m_compressionType);
					} else {
						sizeofBitCompressedConstantFloatsInGroup += 4;
					}
				}
			}
			sizeofConstantCompressedKeyframeData += numAnimFloatInGroup * 4;
			sizeofConstantCompressedKeyframe += Align(numAnimFloatInGroup*4, 16);
			sizeofConstantCompressedOffsets += Align(numAnimFloatInGroup*2, 16);
			sizeofUncompressedConstantData += (numDefinedFloatsInGroup - numAnimFloatInGroup)*4;
			sizeofUncompressedConstants += Align((numDefinedFloatsInGroup - numAnimFloatInGroup)*4, 16);
			sizeofConstantCompressedOffsets += Align((numDefinedFloatsInGroup - numAnimFloatInGroup)*2, 16);

			sizeofBitCompressedConstantFloats += sizeofBitCompressedConstantFloatsInGroup;
			sizeofBitCompressedConstantData += sizeofBitCompressedConstantFloatsInGroup;
			sizeofBitCompressedConstants += Align(sizeofBitCompressedConstantFloatsInGroup, 16);

			bitsizeofBitCompressedFloats += bitsizeofBitCompressedFloatsInGroup;
			bitsizeofBitCompressedKeyframeData += bitsizeofBitCompressedFloatsInGroup;
			sizeofBitCompressedKeyframe += (bitsizeofBitCompressedFloatsInGroup + 7)/8;
		}
	}
	unsigned totalConstantCompressed = numOutputFrames*sizeofConstantCompressedKeyframe + sizeofUncompressedConstants + sizeofConstantCompressedOffsets;
	unsigned totalConstantBitCompressed = numOutputFrames*sizeofConstantCompressedKeyframe + sizeofBitCompressedConstants + sizeofConstantCompressedOffsets;
	unsigned totalBitCompressed = numOutputFrames*sizeofBitCompressedKeyframe + sizeofBitCompressedConstants + sizeofConstantCompressedOffsets + sizeofBitCompressedFormatData;

	INOTE(IMsg::kVerbose, "\t<!-- stats:  %3u joints in %3u groups, %3u floats in %3u groups -->\n", numAnimatedJoints, numJointGroups, numFloatChannels, numFloatGroups);
	INOTE(IMsg::kVerbose, "\t<!--         %3u joint anims defined,  %3u float anims defined -->\n", numJointAnims, numFloatAnims);
	if (numJointAnims || numFloatAnims) {
		INOTE(IMsg::kVerbose, "\t<!--   uncompressed size: -->\n");
		INOTE(IMsg::kVerbose, "\t<!--     keyframe:      %7u bytes -->\n", sizeofUncompressedKeyframe);
		if (numAnimQuat + numConstantQuat)
			INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes = %3u * 16 bytes -->\n", numJointAnims*16, numJointAnims);
		if (numAnimScale + numConstantScale)
			INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes = %3u * 12 bytes -->\n", numJointAnims*12, numJointAnims);
		if (numAnimTrans + numConstantTrans)
			INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes = %3u * 12 bytes -->\n", numJointAnims*12, numJointAnims);
		if (numAnimFloat + numConstantFloat)
			INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes = %3u *  4 bytes -->\n", numJointAnims*4, numJointAnims);
		INOTE(IMsg::kVerbose, "\t<!--       alignment:    %7u bytes -->\n", sizeofUncompressedKeyframe - sizeofUncompressedKeyframeData);
		INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes = %5u keyframes * %5u bytes keyframe -->\n", totalUncompressed, numOutputFrames, sizeofUncompressedKeyframe);
		INOTE(IMsg::kVerbose, "\t<!--   constant compression: -->\n");
		INOTE(IMsg::kVerbose, "\t<!--     keyframe:      %7u bytes -->\n", sizeofConstantCompressedKeyframe);
		if (numAnimQuat)
			INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes = %3u * 16 bytes -->\n", numAnimQuat*16, numAnimQuat);
		if (numAnimScale)
			INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes = %3u * 12 bytes -->\n", numAnimScale*12, numAnimScale);
		if (numAnimTrans)
			INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes = %3u * 12 bytes -->\n", numAnimTrans*12, numAnimTrans);
		if (numAnimFloat)
			INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes = %3u *  4 bytes -->\n", numAnimFloat*4, numAnimFloat);
		INOTE(IMsg::kVerbose, "\t<!--       alignment:   %7u bytes -->\n", sizeofConstantCompressedKeyframe - sizeofConstantCompressedKeyframeData);
		INOTE(IMsg::kVerbose, "\t<!--     animated:      %7u bytes = %5u keyframes * %5u bytes keyframe -->\n", numOutputFrames*sizeofConstantCompressedKeyframe, numOutputFrames, sizeofConstantCompressedKeyframe);
		INOTE(IMsg::kVerbose, "\t<!--     constants:     %7u bytes -->\n", sizeofUncompressedConstants);
		if (numConstantQuat)
			INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes = %3u * 16 bytes -->\n", numConstantQuat*16, numConstantQuat);
		if (numConstantScale)
			INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes = %3u * 12 bytes -->\n", numConstantScale*12, numConstantScale);
		if (numConstantTrans)
			INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes = %3u * 12 bytes -->\n", numConstantTrans*12, numConstantTrans);
		if (numConstantFloat)
			INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes = %3u *  4 bytes -->\n", numConstantFloat*4, numConstantFloat);
		INOTE(IMsg::kVerbose, "\t<!--       alignment:   %7u bytes -->\n", sizeofUncompressedConstants - sizeofUncompressedConstantData);
		INOTE(IMsg::kVerbose, "\t<!--     offsets:       %7u bytes -->\n", sizeofConstantCompressedOffsets);
		INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes (%5.2f:1) = %7u animated + %7u constant -->\n", totalConstantCompressed, (float)totalUncompressed/totalConstantCompressed, numOutputFrames*sizeofConstantCompressedKeyframe, sizeofUncompressedConstants + sizeofConstantCompressedOffsets);
		if (sizeofBitCompressedConstants < sizeofUncompressedConstants) {
			INOTE(IMsg::kVerbose, "\t<!--   constant bit compression: -->\n");
			INOTE(IMsg::kVerbose, "\t<!--     constants:     %7u bytes -->\n", sizeofBitCompressedConstants);
			if (sizeofBitCompressedConstantQuats)
				INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes -->\n", sizeofBitCompressedConstantQuats);
			if (sizeofBitCompressedConstantScales)
				INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes -->\n", sizeofBitCompressedConstantScales);
			if (sizeofBitCompressedConstantTrans)
				INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes -->\n", sizeofBitCompressedConstantTrans);
			if (sizeofBitCompressedConstantFloats)
				INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes -->\n", sizeofBitCompressedConstantFloats);
			INOTE(IMsg::kVerbose, "\t<!--       overhead:    %7u bytes -->\n", sizeofBitCompressedConstants - sizeofBitCompressedConstantData);
			INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes (%5.2f:1) = %7u animated + %7u constant -->\n", totalConstantBitCompressed, (float)totalUncompressed/totalConstantBitCompressed, numOutputFrames*sizeofConstantCompressedKeyframe, sizeofBitCompressedConstants + sizeofConstantCompressedOffsets);
		}
		if (sizeofBitCompressedKeyframe < sizeofConstantCompressedKeyframe) {
			INOTE(IMsg::kVerbose, "\t<!--   animated bit compression: -->\n");
			INOTE(IMsg::kVerbose, "\t<!--     keyframe:      %7u bytes -->\n", sizeofBitCompressedKeyframe);
			if (bitsizeofBitCompressedQuats)
				INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes (%7u bits) -->\n", (bitsizeofBitCompressedQuats + 7)/8, bitsizeofBitCompressedQuats);
			if (bitsizeofBitCompressedScales)
				INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes (%7u bits) -->\n", (bitsizeofBitCompressedScales + 7)/8, bitsizeofBitCompressedScales);
			if (bitsizeofBitCompressedTrans)
				INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes (%7u bits) -->\n", (bitsizeofBitCompressedTrans + 7)/8, bitsizeofBitCompressedTrans);
			if (bitsizeofBitCompressedFloats)
				INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes (%7u bits) -->\n", (bitsizeofBitCompressedFloats + 7)/8, bitsizeofBitCompressedFloats);
			INOTE(IMsg::kVerbose, "\t<!--       alignment:   %7u bytes -->\n", sizeofBitCompressedKeyframe - (bitsizeofBitCompressedKeyframeData + 7)/8);
			INOTE(IMsg::kVerbose, "\t<!--     format:        %7u bytes -->\n", sizeofBitCompressedFormatData);
			INOTE(IMsg::kVerbose, "\t<!--     animated:      %7u bytes = %5u keyframes * %5u bytes per keyframe + %5u bytes format -->\n", sizeofBitCompressedKeyframe*numOutputFrames + sizeofBitCompressedFormatData, numOutputFrames, sizeofBitCompressedKeyframe, sizeofBitCompressedFormatData);
			INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes (%5.2f:1) = %7u animated + %7u constant -->\n", totalBitCompressed, (float)totalUncompressed/totalBitCompressed, numOutputFrames*sizeofBitCompressedKeyframe + sizeofBitCompressedFormatData, sizeofBitCompressedConstants + sizeofConstantCompressedOffsets);
		}
		if (keyCompressionFlags == kClipKeysUnshared) {
			//NOTE: this doesn't include block compression overhead:
			size_t sizeofKeyframeCompressedKeyframe = (bitsizeofKeyframeCompressedQuats + bitsizeofKeyframeCompressedScales + bitsizeofKeyframeCompressedTrans + bitsizeofKeyframeCompressedFloats + 7)/8 + sizeofKeyframeCompressedTimes;
			size_t totalKeyFrameCompressed = sizeofKeyframeCompressedKeyframe + sizeofBitCompressedFormatData + sizeofBitCompressedConstants + sizeofConstantCompressedOffsets;
			INOTE(IMsg::kVerbose, "\t<!--   unshared keyframe compression: -->\n");
			INOTE(IMsg::kVerbose, "\t<!--     animated:    %7u bytes -->\n", sizeofKeyframeCompressedKeyframe );
			if (bitsizeofKeyframeCompressedQuats)
				INOTE(IMsg::kVerbose, "\t<!--       rotation:    %7u bytes (%7u bits) -->\n", (bitsizeofKeyframeCompressedQuats + 7)/8, bitsizeofKeyframeCompressedQuats);
			if (bitsizeofKeyframeCompressedScales)
				INOTE(IMsg::kVerbose, "\t<!--       scale:       %7u bytes (%7u bits) -->\n", (bitsizeofKeyframeCompressedScales + 7)/8, bitsizeofKeyframeCompressedScales);
			if (bitsizeofKeyframeCompressedTrans)
				INOTE(IMsg::kVerbose, "\t<!--       translation: %7u bytes (%7u bits) -->\n", (bitsizeofKeyframeCompressedTrans + 7)/8, bitsizeofKeyframeCompressedTrans);
			if (bitsizeofKeyframeCompressedFloats)
				INOTE(IMsg::kVerbose, "\t<!--       float:       %7u bytes (%7u bits) -->\n", (bitsizeofKeyframeCompressedFloats + 7)/8, bitsizeofKeyframeCompressedFloats);
			INOTE(IMsg::kVerbose, "\t<!--       time data:   %7u bytes -->\n", sizeofKeyframeCompressedTimes);
			INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes (%5.2f:1) = %7u animated + %7u constant (+ ? block overhead) -->\n", totalKeyFrameCompressed, (float)totalUncompressed/totalKeyFrameCompressed, sizeofKeyframeCompressedKeyframe + sizeofBitCompressedFormatData, sizeofBitCompressedConstants + sizeofConstantCompressedOffsets);
		} else if (keyCompressionFlags == kClipKeysShared) {
			size_t numDataFrames = numOutputFrames - compression.m_sharedSkipFrames.size();
			size_t totalKeyFrameCompressed = sizeofBitCompressedKeyframe*numDataFrames + sizeofBitCompressedFormatData + sizeofBitCompressedConstants + sizeofConstantCompressedOffsets;
			INOTE(IMsg::kVerbose, "\t<!--   shared keyframe compression: -->\n");
			INOTE(IMsg::kVerbose, "\t<!--     animated:      %7u bytes = %5u keyframes * %5u bytes per keyframe + %5u bytes format -->\n", sizeofBitCompressedKeyframe*numDataFrames + sizeofBitCompressedFormatData, numDataFrames, sizeofBitCompressedKeyframe, sizeofBitCompressedFormatData);
			INOTE(IMsg::kVerbose, "\t<!--     time data:     %7u bytes = %5u keyframes * 2 -->\n", numDataFrames*2, numDataFrames);
			INOTE(IMsg::kVerbose, "\t<!--     total:         %7u bytes (%5.2f:1) = %7u animated + %7u constant -->\n", totalKeyFrameCompressed, (float)totalUncompressed/totalKeyFrameCompressed, numDataFrames*sizeofBitCompressedKeyframe + sizeofBitCompressedFormatData, sizeofBitCompressedConstants + sizeofConstantCompressedOffsets + numDataFrames*2);
		}
	}
	INOTE(IMsg::kVerbose, "</compression>\n");
}

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim
//-------------------

