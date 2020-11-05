/*
 * Copyright (c) 2003, 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "bitcompressedarray.h"
#include "icelib/icesupport/bytestream.h"
#include "icelib/icesupport/streamwriter.h"
#include "icelib/geom/cgmath.h"
#include "icelib/geom/bdbox.h"
#include "icelib/jama/jama_svd.h"

#define TEST_BLOCK_PREDICTOR_COMPRESSION 0

namespace OrbisAnim { 
namespace Tools {

//================================================================================================

static inline U32 FloatAsU32( F32 f )
{
	union {
		F32 f;
		U32 ui;
	} v;
	v.f = f;
	return v.ui;
}
static inline F32 U32AsFloat( U32 ui )
{
	union {
		F32 f;
		U32 ui;
	} v;
	v.ui = ui;
	return v.f;
}


//Standard bit packing formats:
const Vec3BitPackingFormat kVbpfVec3Uncompressed(32, 32, 32, 96);
const Vec3BitPackingFormat kVbpfQuatUncompressed(32, 32, 32, 128);
const Vec3BitPackingFormat kVbpfFloatUncompressed(32, 0, 0, 32);
const Vec3BitPackingFormat kVbpfVec3Float16(16, 16, 16, 48);
const Vec3BitPackingFormat kVbpfConstQuat48SmallestThree(16, 15, 15, 48);

Vec3BitPackingFormat GetConstCompressionBitFormat(AnimChannelCompressionType constCompressionType)
{
	switch (constCompressionType) {
	case kAcctConstVec3Uncompressed:	return kVbpfVec3Uncompressed;
	case kAcctConstVec3Float16:			return kVbpfVec3Float16;
	case kAcctConstQuatUncompressed:	return kVbpfQuatUncompressed;
	case kAcctConstQuat48SmallestThree:	return kVbpfConstQuat48SmallestThree;
	case kAcctConstFloatUncompressed:	return kVbpfFloatUncompressed;
	case kAcctConstVec3Auto:
	case kAcctConstQuatAuto:
	case kAcctConstFloatAuto:			return kVbpfNone;
	default:
		ITASSERT(0);	// invalid const compression type!
		return kVbpfNone;
	}
}

//Fixed bit-packing format compressed streams are stored as arrays of a compression specific type:
typedef std::vector<Vec3Half>			Vec3HalfArray;

// non-bit-packing compression methods:
bool DoCompress_Vec3_Float16( Vec3HalfArray& aCompressed, ITGEOM::Vec3Array& avDecompressed, const ITGEOM::Vec3Array& avValues );

//Variable bit-packing compressed streams sometimes also store range data in various formats:
bool DoCompress_Vec3_Range( U64_Array& aCompressed, ITGEOM::Vec3Array& avDecompressed, U64_Array& aContextData, const ITGEOM::Vec3Array& avValues, Vec3BitPackingFormat bitPackingFormat );
bool DoCompress_Quat_Smallest3( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat );
bool DoCompress_Quat_Log( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, U64_Array& aContextData, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3Array& avPrevOutputLogValues );
bool DoCompress_Quat_LogPca( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, U64_Array& aContextData, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3Array& avPrevOutputLogValues );

void CalculateErrorsVec3(ITGEOM::Vec3Array const& source, ITGEOM::Vec3Array const& decompressed, ITGEOM::Vec3& vMaxError, float& fMaxError, ITGEOM::Vec3& vRmsError, float& fRmsError, U32& iSampleMaxError)
{
	U32 numSamples = (U32)source.size();
	iSampleMaxError = 0;
	vMaxError.x = vMaxError.y = vMaxError.z = fMaxError = 0.0;
	vRmsError.x = vRmsError.y = vRmsError.z = fRmsError = 0.0;
	if (numSamples == 0 || decompressed.size() != source.size())
		return;
	double dfErrorMaxX = 0.0, dfErrorMaxY = 0.0, dfErrorMaxZ = 0.0, dfErrorMax = 0.0;
	double dfErrorSqrX = 0.0, dfErrorSqrY = 0.0, dfErrorSqrZ = 0.0;
	for (U32 i = 0; i < numSamples; ++i)
	{
		double dfErrorAbsX = fabs((double)source[i].x - (double)decompressed[i].x);
		double dfErrorAbsY = fabs((double)source[i].y - (double)decompressed[i].y);
		double dfErrorAbsZ = fabs((double)source[i].z - (double)decompressed[i].z);
		double dfErrorSqr = dfErrorAbsX*dfErrorAbsX + dfErrorAbsY*dfErrorAbsY + dfErrorAbsZ*dfErrorAbsZ;
		if (dfErrorMaxX < dfErrorAbsX)
			dfErrorMaxX = dfErrorAbsX;
		if (dfErrorMaxY < dfErrorAbsY)
			dfErrorMaxY = dfErrorAbsY;
		if (dfErrorMaxZ < dfErrorAbsZ)
			dfErrorMaxZ = dfErrorAbsZ;
		if (dfErrorMax*dfErrorMax < dfErrorSqr) {
			iSampleMaxError = i;
			dfErrorMax = sqrt( dfErrorSqr );
		}
		dfErrorSqrX += dfErrorAbsX * dfErrorAbsX;
		dfErrorSqrY += dfErrorAbsY * dfErrorAbsY;
		dfErrorSqrZ += dfErrorAbsZ * dfErrorAbsZ;
	}
	double dfNumSamplesInv = 1.0 / (double)numSamples;
	vMaxError.x = (float)dfErrorMaxX;
	vMaxError.y = (float)dfErrorMaxY;
	vMaxError.z = (float)dfErrorMaxZ;
	fMaxError = (float)dfErrorMax;
	vRmsError.x = (float)sqrt(dfErrorSqrX * dfNumSamplesInv);
	vRmsError.y = (float)sqrt(dfErrorSqrY * dfNumSamplesInv);
	vRmsError.z = (float)sqrt(dfErrorSqrZ * dfNumSamplesInv);
	fRmsError = (float)sqrt((dfErrorSqrX + dfErrorSqrY + dfErrorSqrZ) * dfNumSamplesInv);
}

void CalculateErrorsQuat(ITGEOM::QuatArray const& source, ITGEOM::QuatArray const& decompressed, float& fMaxErrorRadians, float& fRmsErrorRadians, float& fMaxCompError, float& fMaxSrcDenorm, float& fMaxDstDenorm, U32& iSampleMaxError)
{
	U32 numSamples = (U32)source.size();
	iSampleMaxError = 0;
	fMaxErrorRadians = fRmsErrorRadians = fMaxCompError = fMaxSrcDenorm = fMaxDstDenorm = 0.0f;
	if (numSamples == 0 || decompressed.size() != source.size())
		return;
	double dfSLenSqrMax = 0.0, dfSLenSqrMin = 1000000.0;
	double dfDLenSqrMax = 0.0, dfDLenSqrMin = 1000000.0;
	double dfCompErrorMax = 0.0, dfErrorCosMin = 1.0, dfErrorRadiansSumSqr = 0.0;
	for (U32 i = 0; i < numSamples; ++i)
	{
		ITGEOM::Quat const& qs = source[i];
		ITGEOM::Quat qd = decompressed[i];
		double dfDot = (double)qs.x*(double)qd.x + (double)qs.y*(double)qd.y + (double)qs.z*(double)qd.z + (double)qs.w*(double)qd.w;
		if (dfDot < 0.0)
			qd.x = -qd.x, qd.y = -qd.y, qd.z = -qd.z, qd.w = -qd.w, dfDot = -dfDot;
		double dfSLenSqr = (double)qs.x*(double)qs.x + (double)qs.y*(double)qs.y + (double)qs.z*(double)qs.z + (double)qs.w*(double)qs.w;
		double dfDLenSqr = (double)qd.x*(double)qd.x + (double)qd.y*(double)qd.y + (double)qd.z*(double)qd.z + (double)qd.w*(double)qd.w;
		double dfErrorAbsX = fabs((double)qs.x - (double)qd.x);
		double dfErrorAbsY = fabs((double)qs.y - (double)qd.y);
		double dfErrorAbsZ = fabs((double)qs.z - (double)qd.z);
		double dfErrorAbsW = fabs((double)qs.w - (double)qd.w);
		if (dfSLenSqrMax < dfSLenSqr)
			dfSLenSqrMax = dfSLenSqr;
		if (dfSLenSqrMin > dfSLenSqr)
			dfSLenSqrMin = dfSLenSqr;
		if (dfDLenSqrMax < dfDLenSqr)
			dfDLenSqrMax = dfDLenSqr;
		if (dfDLenSqrMin > dfDLenSqr)
			dfDLenSqrMin = dfDLenSqr;
		if (dfCompErrorMax < dfErrorAbsX)
			dfCompErrorMax = dfErrorAbsX;
		if (dfCompErrorMax < dfErrorAbsY)
			dfCompErrorMax = dfErrorAbsY;
		if (dfCompErrorMax < dfErrorAbsZ)
			dfCompErrorMax = dfErrorAbsZ;
		if (dfCompErrorMax < dfErrorAbsW)
			dfCompErrorMax = dfErrorAbsW;
		double dfErrorCos = dfDot / sqrt( dfSLenSqr * dfDLenSqr );
		double dfErrorRadians = (dfErrorCos < 1.0) ? acos( dfErrorCos ) : 0.0;
		if (dfErrorCosMin > dfErrorCos) {
			iSampleMaxError = i;
			dfErrorCosMin = dfErrorCos;
		}
		dfErrorRadiansSumSqr += dfErrorRadians*dfErrorRadians;
	}
	double dfSDenormMax = fabs( dfSLenSqrMax - 1.0 );
	double dfSDenormMin = fabs( dfSLenSqrMin - 1.0 );
	if (dfSDenormMax < dfSDenormMin)
		dfSDenormMax = dfSDenormMin;
	double dfDDenormMax = fabs( dfDLenSqrMax - 1.0 );
	double dfDDenormMin = fabs( dfDLenSqrMin - 1.0 );
	if (dfDDenormMax < dfDDenormMin)
		dfDDenormMax = dfDDenormMin;

	double dfNumSamplesInv = 1.0 / (double)numSamples;
	fMaxCompError = (float)dfCompErrorMax;
	fMaxErrorRadians = (dfErrorCosMin < 1.0) ? (float)acos(dfErrorCosMin) : 0.0f;
	fRmsErrorRadians = (float)sqrt( dfErrorRadiansSumSqr * dfNumSamplesInv );
	fMaxSrcDenorm = (float)dfSDenormMax;
	fMaxDstDenorm = (float)dfDDenormMax;
}

// returns the number of bits needed to hold value, between 0 to 32
// for example GetNumberOfBits(0x12) = 5, GetNumberOfBits(0) = 0, GetNumberOfBits(0x80000000) = 32
unsigned GetNumberOfBits(U32 value)
{
	unsigned i = 16;
	unsigned bit = 0;
	while (i > 0) {
		U32 mask = ((1 << i)-1) << (bit + i);
		if (value & mask)
			bit += i;
		i >>= 1;
	}
	if (value)
		bit++;
	return bit;
}

static inline I32 F32ToI32(F32 f)
{
	union {	F32 f; I32 i; } v;
	v.f = f;
	return v.i;
}

/// Uncompressed format for Vec3 array
class UncompressedVec3Array : public IBitCompressedVec3Array
{
public:
	UncompressedVec3Array(ITGEOM::Vec3Array const& samples, bool bConstant) : IBitCompressedVec3Array(bConstant ? kAcctConstVec3Uncompressed : kAcctVec3Uncompressed), m_samples(samples) {}

	virtual bool IsFixedBitPackingFormat() const { return true; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return kVbpfVec3Uncompressed; }
	virtual size_t GetBitSizeOfSample() const { return kVbpfVec3Uncompressed.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 0; }
	virtual size_t GetSizeOfRangeData(size_t /*iRangeElement*/) const { return 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 0; }

	virtual size_t GetNumSamples() const { return m_samples.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		ITGEOM::Vec3 const& sample = m_samples[iSample];
		streamWriter.WriteF( sample.x );
		streamWriter.WriteF( sample.y );
		streamWriter.WriteF( sample.z );
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		ITGEOM::Vec3 const& sample = m_samples[iSample];
		byteStream.WriteBits( (U64)F32ToI32(sample.z), 32 );
		byteStream.WriteBits( (U64)F32ToI32(sample.y), 32 );
		byteStream.WriteBits( (U64)F32ToI32(sample.x), 32 );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Vec3 GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_samples.size());  return m_samples[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::Vec3Array& samples) const { samples = m_samples; }
	virtual U64_Array const *GetCompressedSamples() const { return NULL; }
	virtual U64_Array const *GetContextData() const { return NULL; }

	virtual float GetMaxError() const { return 0.0f; }
	virtual float GetRmsError() const { return 0.0f; }
	virtual size_t	GetMaxErrorSampleIndex() const { return 0; }

private:
	ITGEOM::Vec3Array		m_samples;		//!< our compressed Vec3 array
};

/// Uncompressed format for Quat array
class UncompressedQuatArray : public IBitCompressedQuatArray
{
public:
	UncompressedQuatArray(ITGEOM::QuatArray const& samples, bool bConstant) : IBitCompressedQuatArray(bConstant ? kAcctConstQuatUncompressed : kAcctQuatUncompressed), m_samples(samples) {}

	virtual bool IsFixedBitPackingFormat() const { return true; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return kVbpfQuatUncompressed; }
	virtual size_t GetBitSizeOfSample() const { return kVbpfQuatUncompressed.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 0; }
	virtual size_t GetSizeOfRangeData(size_t /*iRangeElement*/) const { return 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 0; }

	virtual size_t GetNumSamples() const { return m_samples.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		ITGEOM::Quat const& sample = m_samples[iSample];
		streamWriter.WriteF( sample.x );
		streamWriter.WriteF( sample.y );
		streamWriter.WriteF( sample.z );
		streamWriter.WriteF( sample.w );
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		ITGEOM::Quat const& sample = m_samples[iSample];
		byteStream.WriteBits( (U64)F32ToI32(sample.w), 32 );
		byteStream.WriteBits( (U64)F32ToI32(sample.z), 32 );
		byteStream.WriteBits( (U64)F32ToI32(sample.y), 32 );
		byteStream.WriteBits( (U64)F32ToI32(sample.x), 32 );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Quat GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_samples.size());  return m_samples[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::QuatArray& samples) const { samples = m_samples; }
	virtual U64_Array const *GetCompressedSamples() const { return NULL; }
	virtual U64_Array const *GetContextData() const { return NULL; }

	virtual float GetMaxError() const { return 0.0f; }
	virtual float GetRmsError() const { return 0.0f; }
	virtual size_t	GetMaxErrorSampleIndex() const { return 0; }

private:
	ITGEOM::QuatArray		m_samples;		//!< our compressed Quat array
};

/// Uncompressed format for float array
class UncompressedFloatArray : public IBitCompressedFloatArray
{
public:
	UncompressedFloatArray(std::vector<float> const& samples, bool bConstant) 
		: IBitCompressedFloatArray(bConstant ? kAcctConstFloatUncompressed : kAcctFloatUncompressed)
		, m_samples(samples)
	{
	}

	virtual bool IsFixedBitPackingFormat() const { return true; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return kVbpfFloatUncompressed; }
	virtual size_t GetBitSizeOfSample() const { return kVbpfFloatUncompressed.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 0; }
	virtual size_t GetSizeOfRangeData(size_t /*iRangeElement*/) const { return 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 0; }

	virtual size_t GetNumSamples() const { return m_samples.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		streamWriter.WriteF( m_samples[iSample] );
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_samples.size());
		byteStream.WriteBits( (U64)F32ToI32(m_samples[iSample]), 32 );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual float GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_samples.size());  return m_samples[iSample]; }
	virtual void GetDecompressedSamples(std::vector<float>& samples) const { samples = m_samples; }
	virtual U64_Array const *GetCompressedSamples() const { return NULL; }
	virtual U64_Array const *GetContextData() const { return NULL; }

	virtual float GetMaxError() const { return 0.0f; }
	virtual float GetRmsError() const { return 0.0f; }
	virtual size_t	GetMaxErrorSampleIndex() const { return 0; }

private:
	std::vector<float>	m_samples;		//!< our compressed float array
};

/// Float16CompressedVec3Array : 3-vectors compressed to 3 16 bit half-float (1.5.10)
class Float16CompressedVec3Array : public IBitCompressedVec3Array
{
public:
	Float16CompressedVec3Array(ITGEOM::Vec3Array const& samples, bool bConstant)
		: IBitCompressedVec3Array(bConstant ? kAcctConstVec3Float16 : kAcctVec3Float16)
	{
		m_bValid = DoCompress_Vec3_Float16( m_compressed, m_decompressed, samples );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsVec3(samples, m_decompressed, m_vMaxError, m_fMaxError, m_vRmsError, m_fRmsError, m_iSampleMaxError);
	}

	virtual bool IsFixedBitPackingFormat() const { return true; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return kVbpfVec3Float16; }
	virtual size_t GetBitSizeOfSample() const { return kVbpfVec3Float16.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 0; }
	virtual size_t GetSizeOfRangeData(size_t /*iRangeElement*/) const { return 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 0; }

	virtual size_t GetNumSamples() const { return m_compressed.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		Vec3Half const& sample = m_compressed[iSample];
		streamWriter.Write2( sample.x );
		streamWriter.Write2( sample.y );
		streamWriter.Write2( sample.z );
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		Vec3Half const& sample = m_compressed[iSample];
		byteStream.WriteBits( (U64)sample.z, 16 );
		byteStream.WriteBits( (U64)sample.y, 16 );
		byteStream.WriteBits( (U64)sample.x, 16 );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Vec3 GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_decompressed[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::Vec3Array& samples) const { samples = m_decompressed; }
	virtual U64_Array const *GetCompressedSamples() const { return NULL; }
	virtual U64_Array const *GetContextData() const { return NULL; }

	virtual float GetMaxError() const { return m_fMaxError; }
	virtual float GetRmsError() const { return m_fRmsError; }
	virtual size_t	GetMaxErrorSampleIndex() const { return m_iSampleMaxError; }

private:
	Vec3HalfArray			m_compressed;
	ITGEOM::Vec3Array		m_decompressed;
	bool					m_bValid;			//!< compression succeeded

	ITGEOM::Vec3			m_vMaxError;		//!< maximum absolute error per component of decompressed values versus source values
	float					m_fMaxError;		//!< maximum absolute error (distance) of a decompressed value from source value
	ITGEOM::Vec3			m_vRmsError;		//!< RMS average error per component of decompressed values vs. source values
	float					m_fRmsError;		//!< RMS average error of decompressed values vs. source values
	U32						m_iSampleMaxError;	//!< sample index which produced the maximum absolute error
};

/*!
-- RangeCompressedVec3Array : 3-vectors compressed with bias and scale to variable bit packed format
*/
class RangeCompressedVec3Array : public IBitCompressedVec3Array
{
public:
	RangeCompressedVec3Array(ITGEOM::Vec3Array const& samples, Vec3BitPackingFormat bitPackingFormat, U64_Array const& aContextData)
		: IBitCompressedVec3Array(kAcctVec3Range), m_bitPackingFormat(bitPackingFormat), m_aContextData(aContextData)
	{
		m_bValid = DoCompress_Vec3_Range( m_compressed, m_decompressed, m_aContextData, samples, m_bitPackingFormat );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsVec3(samples, m_decompressed, m_vMaxError, m_fMaxError, m_vRmsError, m_fRmsError, m_iSampleMaxError);
	}

	virtual bool IsFixedBitPackingFormat() const { return false; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return m_bitPackingFormat; }
	virtual size_t GetBitSizeOfSample() const { return m_bitPackingFormat.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 1; }
	virtual size_t GetSizeOfRangeData(size_t iRangeElement) const { return (iRangeElement == 0) ? 16 : 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 16; }

	virtual size_t GetNumSamples() const { return m_compressed.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(!(GetBitSizeOfSample() & 0x7));
		ITASSERT(iSample < m_compressed.size());
		U64 sample = m_compressed[iSample];
		unsigned numBytes = (unsigned)((GetBitSizeOfSample() + 0x7)>>3);
		for (unsigned iByte = 0; iByte < numBytes; ++iByte) {
			streamWriter.Write1( (U8)(sample >> ((numBytes-iByte-1)*8)) );
		}
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		byteStream.WriteBits( m_compressed[iSample], m_bitPackingFormat.m_numBitsTotal );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const
	{
		if (iRangeElement == 0) {
			streamWriter.Write8( m_aContextData[0] );
			streamWriter.Write8( m_aContextData[1] );
		}
	}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& byteStream, size_t iRangeElement) const
	{
		if (iRangeElement == 0) {
			byteStream.Write8( m_aContextData[0] );
			byteStream.Write8( m_aContextData[1] );
		}
	}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Vec3 GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_decompressed[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::Vec3Array& samples) const { samples = m_decompressed; }
	virtual U64_Array const *GetCompressedSamples() const { return &m_compressed; }
	virtual U64_Array const *GetContextData() const { return &m_aContextData; }

	virtual float GetMaxError() const { return m_fMaxError; }
	virtual float GetRmsError() const { return m_fRmsError; }
	virtual size_t GetMaxErrorSampleIndex() const { return m_iSampleMaxError; }

	ITGEOM::Vec3 const& GetMaxErrorPerComponent() const { return m_vMaxError; }
	ITGEOM::Vec3 const& GetRmsErrorPerComponent() const { return m_vRmsError; }

private:
	Vec3BitPackingFormat	m_bitPackingFormat;	//!< the bit-packing format we're using
	U64_Array				m_compressed;		//!< our compressed vector stream
	U64_Array				m_aContextData;		//!< our scale and bias range data
	ITGEOM::Vec3Array		m_decompressed;		//!< our decompressed vector stream
	bool					m_bValid;			//!< compression succeeded

	ITGEOM::Vec3			m_vMaxError;		//!< maximum absolute error per component of decompressed values versus source values
	float					m_fMaxError;		//!< maximum absolute error (distance) of a decompressed value from source value
	ITGEOM::Vec3			m_vRmsError;		//!< RMS average error per component of decompressed values vs. source values
	float					m_fRmsError;		//!< RMS average error of decompressed values vs. source values
	U32						m_iSampleMaxError;	//!< sample index which produced the maximum absolute error
};

/// SmallestThreeCompressedQuatArray : quaternions compressed using smallest three compression,
/// which stores the index of the largest component in 2 bits, and the three remaining components
/// compressed as fixed point values in the range [-sqrt(2) ... sqrt(2)]
class SmallestThreeCompressedQuatArray : public IBitCompressedQuatArray
{
public:
	SmallestThreeCompressedQuatArray(ITGEOM::QuatArray const& samples, Vec3BitPackingFormat bitPackingFormat)
		: IBitCompressedQuatArray(kAcctQuatSmallestThree), m_bitPackingFormat(bitPackingFormat)
	{
		m_bValid = DoCompress_Quat_Smallest3( m_compressed, m_decompressed, samples, m_bitPackingFormat );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsQuat(samples, m_decompressed, m_fMaxErrorRadians, m_fRmsErrorRadians, m_fMaxCompError, m_fMaxSrcDenorm, m_fMaxDstDenorm, m_iSampleMaxError);
	};
	SmallestThreeCompressedQuatArray(ITGEOM::QuatArray const& samples)
		: IBitCompressedQuatArray(kAcctConstQuat48SmallestThree), m_bitPackingFormat(kVbpfConstQuat48SmallestThree)
	{
		m_bValid = DoCompress_Quat_Smallest3( m_compressed, m_decompressed, samples, m_bitPackingFormat );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsQuat(samples, m_decompressed, m_fMaxErrorRadians, m_fRmsErrorRadians, m_fMaxCompError, m_fMaxSrcDenorm, m_fMaxDstDenorm, m_iSampleMaxError);
	};

	virtual bool IsFixedBitPackingFormat() const { return (m_compressionType & kAcctConstMask) != 0; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return m_bitPackingFormat; }
	virtual size_t GetBitSizeOfSample() const { return m_bitPackingFormat.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 0; }
	virtual size_t GetSizeOfRangeData(size_t /*iRangeElement*/) const { return 0; }
	virtual size_t GetSizeOfAllRangeData() const { return 0; }

	virtual size_t GetNumSamples() const { return m_compressed.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(!(GetBitSizeOfSample() & 0x7));
		ITASSERT(iSample < m_compressed.size());
		U64 sample = m_compressed[iSample];
		unsigned numBytes = (unsigned)((GetBitSizeOfSample() + 0x7)>>3);
		ITASSERT(numBytes==6);	// currently only 16-15-15-2 supported?
		for (unsigned iByte = 0; iByte < numBytes; ++iByte) {
			streamWriter.Write1( (U8)(sample >> iByte*8) );
		}
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		byteStream.WriteBits( m_compressed[iSample], m_bitPackingFormat.m_numBitsTotal );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Quat GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_decompressed[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::QuatArray& samples) const { samples = m_decompressed; }
	virtual U64_Array const *GetCompressedSamples() const { return &m_compressed; }
	virtual U64_Array const *GetContextData() const { return NULL; }

	virtual float GetMaxError() const { return m_fMaxErrorRadians; }
	virtual float GetRmsError() const { return m_fRmsErrorRadians; }
	virtual size_t GetMaxErrorSampleIndex() const { return m_iSampleMaxError; }

	float	GetMaxCompError() const { return m_fMaxCompError; }

	Vec3BitPackingFormat	m_bitPackingFormat;	//!< the bit-packing format we're using
	U64_Array				m_compressed;		//!< our compressed quaternion stream
private:
	ITGEOM::QuatArray		m_decompressed;		//!< our decompressed quaternion stream
	bool					m_bValid;			//!< compression succeeded

	float					m_fMaxErrorRadians;
	float					m_fRmsErrorRadians;
	float					m_fMaxCompError;
	float					m_fMaxSrcDenorm;
	float					m_fMaxDstDenorm;
	U32						m_iSampleMaxError;	//!< sample index which produced the maximum absolute error
};

///LogCompressedQuatArray : compresses quaternions to range packed 3 vector quaternion logarithms in a mean space
class LogCompressedQuatArray : public IBitCompressedQuatArray
{
public:
	LogCompressedQuatArray(ITGEOM::QuatArray const& samples, Vec3BitPackingFormat bitPackingFormat, U64_Array const& aContextData, ITGEOM::Vec3Array& avPrevOutputLogValues)
		: IBitCompressedQuatArray(kAcctQuatLog), m_bitPackingFormat(bitPackingFormat), m_aContextData(aContextData)
	{
		m_bValid = DoCompress_Quat_Log( m_compressed, m_decompressed, m_aContextData, samples, m_bitPackingFormat, avPrevOutputLogValues );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsQuat(samples, m_decompressed, m_fMaxErrorRadians, m_fRmsErrorRadians, m_fMaxCompError, m_fMaxSrcDenorm, m_fMaxDstDenorm, m_iSampleMaxError);
	};
	LogCompressedQuatArray(LogCompressedQuatArray const &c) : IBitCompressedQuatArray(kAcctQuatLog),
		m_compressed(c.m_compressed), m_decompressed(c.m_decompressed),
		m_bitPackingFormat(c.m_bitPackingFormat), m_aContextData(c.m_aContextData), m_bValid(c.m_bValid),
		m_fMaxErrorRadians(c.m_fMaxErrorRadians), m_fRmsErrorRadians(c.m_fRmsErrorRadians), m_fMaxCompError(c.m_fMaxCompError), m_fMaxSrcDenorm(c.m_fMaxSrcDenorm), m_fMaxDstDenorm(c.m_fMaxDstDenorm) 
	{
	};

	virtual bool IsFixedBitPackingFormat() const { return false; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return m_bitPackingFormat; }
	virtual size_t GetBitSizeOfSample() const { return m_bitPackingFormat.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 2; }
	virtual size_t GetSizeOfRangeData(size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		return 16;	// vec3 range compression range data
		case 1:		return 8;	// log compression range data
		default:	return 0;
		}
	}
	virtual size_t GetSizeOfAllRangeData() const { return 24; }

	virtual size_t GetNumSamples() const { return m_compressed.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(!(GetBitSizeOfSample() & 0x7));
		ITASSERT(iSample < m_compressed.size());
		U64 sample = m_compressed[iSample];
		unsigned numBytes = (unsigned)((GetBitSizeOfSample() + 0x7)>>3);
		for (unsigned iByte = 0; iByte < numBytes; ++iByte) {
			streamWriter.Write1( (U8)(sample >> ((numBytes-iByte-1)*8)) );
		}
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		byteStream.WriteBits( m_compressed[iSample], m_bitPackingFormat.m_numBitsTotal );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		// vec3 range compression range data
			streamWriter.Write8( m_aContextData[0] );
			streamWriter.Write8( m_aContextData[1] );
			break;
		case 1:		// log compression range data
			if (m_paContextDataPrev) {
				U64 uContextMean = m_aContextData[2];
				U64 uContextMeanPrev = (*m_paContextDataPrev)[2];
				streamWriter.Write2( (I16)((uContextMeanPrev >>  0) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMean     >>  0) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 16) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMean     >> 16) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 32) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMean     >> 32) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 48) & 0xFFFF) );
				streamWriter.Write2( (I16)((uContextMean     >> 48) & 0xFFFF) );
				m_paContextDataPrev = NULL;
			} else
				m_paContextDataPrev = &m_aContextData;
			break;
		}
	}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const
	{
		if (iRangeElement == 1) {	// log compression range data
			if (m_paContextDataPrev) {
				U64 uContextMeanPrev = (*m_paContextDataPrev)[2];
				streamWriter.Write2( (I16)((uContextMeanPrev >>  0) & 0xFFFF) );
				streamWriter.Write2( (I16)0 );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 16) & 0xFFFF) );
				streamWriter.Write2( (I16)0 );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 32) & 0xFFFF) );
				streamWriter.Write2( (I16)0 );
				streamWriter.Write2( (I16)((uContextMeanPrev >> 48) & 0xFFFF) );
				streamWriter.Write2( (I16)0 );
				m_paContextDataPrev = NULL;
			}
		}
	}

	virtual void WriteRangeData(IT::ByteStream& byteStream, size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		// vec3 range compression range data
			byteStream.Write8( m_aContextData[0] );
			byteStream.Write8( m_aContextData[1] );
			break;
		case 1:		// log compression range data
			if (m_paContextDataPrev) {
				U64 uContextMean = m_aContextData[2];
				U64 uContextMeanPrev = (*m_paContextDataPrev)[2];
				byteStream.Write2( (I16)((uContextMeanPrev >>  0) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMean     >>  0) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMeanPrev >> 16) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMean     >> 16) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMeanPrev >> 32) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMean     >> 32) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMeanPrev >> 48) & 0xFFFF) );
				byteStream.Write2( (I16)((uContextMean     >> 48) & 0xFFFF) );
				m_paContextDataPrev = NULL;
			} else
				m_paContextDataPrev = &m_aContextData;
			break;
		}
	}
	virtual void WriteRangeDataEnd(IT::ByteStream& byteStream, size_t iRangeElement) const
	{
		if (iRangeElement == 1) {	// log compression range data
			if (m_paContextDataPrev) {
				U64 uContextMeanPrev = (*m_paContextDataPrev)[2];
				byteStream.Write2( (I16)((uContextMeanPrev >>  0) & 0xFFFF) );
				byteStream.Write2( (I16)0 );
				byteStream.Write2( (I16)((uContextMeanPrev >> 16) & 0xFFFF) );
				byteStream.Write2( (I16)0 );
				byteStream.Write2( (I16)((uContextMeanPrev >> 32) & 0xFFFF) );
				byteStream.Write2( (I16)0 );
				byteStream.Write2( (I16)((uContextMeanPrev >> 48) & 0xFFFF) );
				byteStream.Write2( (I16)0 );
				m_paContextDataPrev = NULL;
			}
		}
	}

	virtual ITGEOM::Quat GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_decompressed[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::QuatArray& samples) const { samples = m_decompressed; }

	virtual U64 GetCompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_compressed[iSample]; }
	virtual U64_Array const *GetCompressedSamples() const { return &m_compressed; }
	virtual U64_Array const *GetContextData() const { return &m_aContextData; }

	virtual float GetMaxError() const { return m_fMaxErrorRadians; }
	virtual float GetRmsError() const { return m_fRmsErrorRadians; }
	virtual size_t GetMaxErrorSampleIndex() const { return m_iSampleMaxError; }

	float	GetMaxCompError() const { return m_fMaxCompError; }

private:
	Vec3BitPackingFormat	m_bitPackingFormat;	//!< the bit-packing format we're using
	U64_Array				m_compressed;		//!< our compressed quaternion stream
	U64_Array				m_aContextData;		//!< our scale and bias range data and mean quaternion
	ITGEOM::QuatArray		m_decompressed;		//!< our decompressed quaternion stream
	bool					m_bValid;			//!< compression succeeded

	float					m_fMaxErrorRadians;
	float					m_fRmsErrorRadians;
	float					m_fMaxCompError;
	float					m_fMaxSrcDenorm;
	float					m_fMaxDstDenorm;
	U32						m_iSampleMaxError;	//!< sample index which produced the maximum absolute error

	static U64_Array const* m_paContextDataPrev;
};
U64_Array const* LogCompressedQuatArray::m_paContextDataPrev = NULL;

///LogPcaCompressedQuatArray : compresses quaternions to range packed 3 vector quaternion logarithms in an optimal space determined by principle component analysis
class LogPcaCompressedQuatArray : public IBitCompressedQuatArray
{
public:
	LogPcaCompressedQuatArray(ITGEOM::QuatArray const& samples, Vec3BitPackingFormat bitPackingFormat, U64_Array const& aContextData, ITGEOM::Vec3Array& avPrevOutputLogValues)
		: IBitCompressedQuatArray(kAcctQuatLogPca), m_bitPackingFormat(bitPackingFormat), m_aContextData(aContextData)
	{
		m_bValid = DoCompress_Quat_LogPca( m_compressed, m_decompressed, m_aContextData, samples, m_bitPackingFormat, avPrevOutputLogValues );
		ITASSERT( m_bValid );
		if (m_bValid)
			CalculateErrorsQuat(samples, m_decompressed, m_fMaxErrorRadians, m_fRmsErrorRadians, m_fMaxCompError, m_fMaxSrcDenorm, m_fMaxDstDenorm, m_iSampleMaxError);
	};
	LogPcaCompressedQuatArray(LogPcaCompressedQuatArray const &c) : IBitCompressedQuatArray(kAcctQuatLogPca),
		m_compressed(c.m_compressed), m_decompressed(c.m_decompressed),
		m_bitPackingFormat(c.m_bitPackingFormat), m_aContextData(c.m_aContextData), m_bValid(c.m_bValid),
		m_fMaxErrorRadians(c.m_fMaxErrorRadians), m_fRmsErrorRadians(c.m_fRmsErrorRadians), m_fMaxCompError(c.m_fMaxCompError), m_fMaxSrcDenorm(c.m_fMaxSrcDenorm), m_fMaxDstDenorm(c.m_fMaxDstDenorm)
	{
	};

	virtual bool IsFixedBitPackingFormat() const { return false; }
	virtual Vec3BitPackingFormat GetBitPackingFormat() const { return m_bitPackingFormat; }
	virtual size_t GetBitSizeOfSample() const { return m_bitPackingFormat.m_numBitsTotal; }

	virtual size_t GetNumRangeData() const { return 2; }
	virtual size_t GetSizeOfRangeData(size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		return 16;	// vec3 range compression range data
		case 1:		return 16;	// log pca compression range data
		default:	return 0;
		}
	}
	virtual size_t GetSizeOfAllRangeData() const { return 32; }

	virtual size_t GetNumSamples() const { return m_compressed.size(); }

	virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const
	{
		ITASSERT(!(GetBitSizeOfSample() & 0x7));
		ITASSERT(iSample < m_compressed.size());
		U64 sample = m_compressed[iSample];
		unsigned numBytes = (unsigned)((GetBitSizeOfSample() + 0x7)>>3);
		for (unsigned iByte = 0; iByte < numBytes; ++iByte) {
			streamWriter.Write1( (U8)(sample >> ((numBytes-iByte-1)*8)) );
		}
	}
	virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& /*streamWriter*/) const {}

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const
	{
		ITASSERT(iSample < m_compressed.size());
		byteStream.WriteBits( m_compressed[iSample], m_bitPackingFormat.m_numBitsTotal );
	}
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const {}

	virtual void WriteRangeData(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		// vec3 range compression range data
			streamWriter.Write8( m_aContextData[0] );
			streamWriter.Write8( m_aContextData[1] );
			break;
		case 1:		// log pca compression range data
			{
				U64 uqPre = m_aContextData[2], uqPost = m_aContextData[3];
				streamWriter.Write2( (I16)((uqPre  >>  0) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPost >>  0) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPre  >> 16) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPost >> 16) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPre  >> 32) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPost >> 32) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPre  >> 48) & 0xFFFF) );
				streamWriter.Write2( (I16)((uqPost >> 48) & 0xFFFF) );
			}
			break;
		}
	}
	virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const {}
	virtual void WriteRangeData(IT::ByteStream& byteStream, size_t iRangeElement) const
	{
		switch (iRangeElement) {
		case 0:		// vec3 range compression range data
			byteStream.Write8( m_aContextData[0] );
			byteStream.Write8( m_aContextData[1] );
			break;
		case 1:		// log pca compression range data
			{
				U64 uqPre = m_aContextData[2], uqPost = m_aContextData[3];
				byteStream.Write2( (I16)((uqPre  >>  0) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPost >>  0) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPre  >> 16) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPost >> 16) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPre  >> 32) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPost >> 32) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPre  >> 48) & 0xFFFF) );
				byteStream.Write2( (I16)((uqPost >> 48) & 0xFFFF) );
			}
			break;
		}
	}
	virtual void WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const {}

	virtual ITGEOM::Quat GetDecompressedSample(size_t iSample) const { ITASSERT(iSample < m_compressed.size()); return m_decompressed[iSample]; }
	virtual void GetDecompressedSamples(ITGEOM::QuatArray& samples) const { samples = m_decompressed; }
	virtual U64_Array const *GetCompressedSamples() const { return &m_compressed; }
	virtual U64_Array const *GetContextData() const { return &m_aContextData; }

	virtual float	GetMaxError() const { return m_fMaxErrorRadians; }
	virtual float	GetRmsError() const { return m_fRmsErrorRadians; }
	virtual size_t	GetMaxErrorSampleIndex() const { return m_iSampleMaxError; }
	float	GetMaxCompError() const { return m_fMaxCompError; }

private:
	Vec3BitPackingFormat	m_bitPackingFormat;	//!< the bit-packing format we're using
	U64_Array				m_compressed;		//!< our compressed quaternion stream
	U64_Array				m_aContextData;		//!< our scale and bias range data and pre and post quaternion (qMean.Conjugate(), qMean * qPCA)
	ITGEOM::QuatArray		m_decompressed;		//!< our decompressed quaternion stream
	bool					m_bValid;			//!< compression succeeded

	float					m_fMaxErrorRadians;
	float					m_fRmsErrorRadians;
	float					m_fMaxCompError;
	float					m_fMaxSrcDenorm;
	float					m_fMaxDstDenorm;
	U32						m_iSampleMaxError;	//!< sample index which produced the maximum absolute error
};

//================================================================================================

void CalculateVec3Range( ITGEOM::Vec3Array const &avValues, Vec3BitPackingFormat bitPackingFormat, U64_Array& aContextData );

// make a bit compressed array given an array of Vec3s
IBitCompressedVec3Array* BitCompressVec3Array(
	const ITGEOM::Vec3Array& avValues,
	AnimChannelCompressionType compressionType,
	Vec3BitPackingFormat bitPackingFormat,
	U64_Array const* paContextData
	)
{
	IBitCompressedVec3Array* pCompressedArray = NULL;
	switch (compressionType) {
	case kAcctConstVec3Uncompressed:	pCompressedArray = new UncompressedVec3Array( avValues, true );			break;
	case kAcctVec3Uncompressed:			pCompressedArray = new UncompressedVec3Array( avValues, false );		break;
	case kAcctConstVec3Float16:			pCompressedArray = new Float16CompressedVec3Array( avValues, true );	break;
	case kAcctVec3Float16:				pCompressedArray = new Float16CompressedVec3Array( avValues, false );	break;
	case kAcctVec3Range:
		{
			U64_Array aContextData;
			if (!paContextData) {
				CalculateVec3Range(avValues, bitPackingFormat, aContextData);
				paContextData = &aContextData;
			}
			pCompressedArray = new RangeCompressedVec3Array( avValues, bitPackingFormat, *paContextData );
		}
		break;
	default:
		IERR("Invalid Vec3 compression type %02x.\n", compressionType);
		break;
	}
	return pCompressedArray;
}

// make a bit compressed array given an array of Quats
IBitCompressedQuatArray* BitCompressQuatArray(
	const ITGEOM::QuatArray& aqValues,
	AnimChannelCompressionType compressionType,
	Vec3BitPackingFormat bitPackingFormat,
	U64_Array const* paContextData,
	ITGEOM::Vec3Array* pavPrevOutputLogValues)
{
	IBitCompressedQuatArray* pCompressedArray = NULL;
	switch (compressionType) {
	case kAcctConstQuatUncompressed:	pCompressedArray = new UncompressedQuatArray( aqValues, true );							break;
	case kAcctQuatUncompressed:			pCompressedArray = new UncompressedQuatArray( aqValues, false	);						break;
	case kAcctConstQuat48SmallestThree:	pCompressedArray = new SmallestThreeCompressedQuatArray( aqValues );					break;
	case kAcctQuatSmallestThree:		pCompressedArray = new SmallestThreeCompressedQuatArray( aqValues, bitPackingFormat );	break;
	case kAcctQuatLog:
		{
			ITGEOM::Vec3Array avNoPrevOutputLogValues;
			ITGEOM::Vec3Array& avPrevOutputLogValues = pavPrevOutputLogValues ? *pavPrevOutputLogValues : avNoPrevOutputLogValues;
			U64_Array aContextData;
			if (!paContextData || paContextData->size()<3) {
				CalculateQuatLogOrientation(aqValues, aContextData);
				paContextData = &aContextData;
			}
			pCompressedArray = new LogCompressedQuatArray( aqValues, bitPackingFormat, *paContextData, avPrevOutputLogValues );
		}
		break;
	case kAcctQuatLogPca:
		{
			ITGEOM::Vec3Array avNoPrevOutputLogValues;
			ITGEOM::Vec3Array& avPrevOutputLogValues = pavPrevOutputLogValues ? *pavPrevOutputLogValues : avNoPrevOutputLogValues;
			U64_Array aContextData;
			if (!paContextData || paContextData->size()<4) {
				CalculateQuatLogPcaOrientation(aqValues, aContextData);
				paContextData = &aContextData;
			}
			pCompressedArray = new LogPcaCompressedQuatArray( aqValues, bitPackingFormat, *paContextData, avPrevOutputLogValues );
		}
		break;
	default:
		IERR("Invalid Quat compression type %02x.\n", compressionType);
		break;
	}
	return pCompressedArray;
}

// make a bit compressed array given an array of floats
IBitCompressedFloatArray* BitCompressFloatArray(
	const std::vector<float>& afValues,
	AnimChannelCompressionType compressionType,
	unsigned /*uBits*/)
{
	IBitCompressedFloatArray* pCompressedArray = NULL;
	switch (compressionType) {
	case kAcctConstFloatUncompressed:	
		pCompressedArray = new UncompressedFloatArray( afValues, true );							
		break;
	case kAcctFloatUncompressed:		
		pCompressedArray = new UncompressedFloatArray( afValues, false );							
		break;
	default:
		IERR("Invalid float compression type %02x.\n", compressionType );
		break;
	}
	return pCompressedArray;
}

//------------------------------------------------------------------------------------------------

ITGEOM::Vec3 CalculateDecompressedConstVec3( AnimChannelCompressionType compressionType, ITGEOM::Vec3 const& value )
{
	switch (compressionType) {
	case kAcctConstVec3Float16:
		{
			Vec3Half cv(value);
			return cv.GetVec();
		}
	default:
		return value;
	}
}

ITGEOM::Quat CalculateDecompressedConstQuat( AnimChannelCompressionType compressionType, ITGEOM::Quat const& value )
{
	switch (compressionType) {
	case kAcctConstQuat48SmallestThree:
		{
			QuatSmallest3::SetBitPackingFormat(kVbpfConstQuat48SmallestThree);
			QuatSmallest3 cq(value);
			return cq.GetVec();
		}
	default:
		return value;
	}
}

float CalculateDecompressedConstFloat( AnimChannelCompressionType compressionType, float value )
{
	(void)compressionType;
//	switch (compressionType) {
//	default:
		return value;
//	}
}

//================================================================================================

float ComputeError_Vec3(ITGEOM::Vec3 const& v, ITGEOM::Vec3 const& vRef)
{
	return Distance(v, vRef);
//	Vec3 dv = v - vRef;
//	return max( max( fabsf(dv.x), fabsf(dv.y) ), fabsf(dv.z) );
}
struct Vec4_double {
	Vec4_double() {}
	Vec4_double(Vec4_double const& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
	explicit Vec4_double(ITGEOM::Vec4 const& v) : x((double)v.x), y((double)v.y), z((double)v.z), w((double)v.w) {}

	Vec4_double const& operator=(Vec4_double const& v) { x=v.x; y=v.y; z=v.z; w=v.w; return *this; }

	Vec4_double const& operator+=(Vec4_double const& v) { x+=v.x; y+=v.y; z+=v.z; w+=v.w; return *this; }
	Vec4_double const& operator-=(Vec4_double const& v) { x-=v.x; y-=v.y; z-=v.z; w-=v.w; return *this; }
	Vec4_double const& operator*=(double f) { x*=f; y*=f; z*=f; w*=f; return *this; }
	Vec4_double const& operator/=(double f) { x/=f; y/=f; z/=f; w/=f; return *this; }

	Vec4_double const operator+(Vec4_double const& v) const { Vec4_double r(*this); r+=v; return r; }
	Vec4_double const operator-(Vec4_double const& v) const { Vec4_double r(*this); r-=v; return r; }
	Vec4_double const operator*(double f) const { Vec4_double r(*this); r*=f; return r; }
	Vec4_double const operator/(double f) const { Vec4_double r(*this); r/=f; return r; }

	double x, y, z, w;
};
inline double Dot4(Vec4_double const& v1, Vec4_double const& v2)
{
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z + v1.w * v2.w;
}
float ComputeError_Quat(ITGEOM::Quat const& q, ITGEOM::Quat const& qRef)
{
	//NOTE: dot4(q, qRef) == (qRef.Conjugate() * q).w == cos( omega/2 )
	Vec4_double v((ITGEOM::Vec4 const&)q), vRef((ITGEOM::Vec4 const&)qRef);
	double fQNormSqr = Dot4(v, v);
	double fQRefNormSqr = Dot4(vRef, vRef);
	v /= sqrt(fQNormSqr);
	vRef /= sqrt(fQRefNormSqr);

	double fCosHalfOmega = Dot4(v, vRef);
	if (fCosHalfOmega < 0.0)
	{
		fCosHalfOmega = -fCosHalfOmega;
		v *= -1.0f;
	}
	if (fCosHalfOmega > 0.99999) {
		Vec4_double dv = v - vRef;
		double fOmegaSqr = 4.0 * Dot4( dv, dv );
		double fOmega = sqrt(fOmegaSqr);
		double fOmega_cmp = 2.0 * acos(fCosHalfOmega); (void)fOmega_cmp;
		return (float)fOmega;
	} else
		return (float)(2.0 * acos(fCosHalfOmega));

//	return max( max( fabsf(dv.x), fabsf(dv.y) ), max( fabsf(dv.z), fabsf(dv.w) ) );
}
float ComputeError_Float(float f, float fRef)
{
	return fabsf(f - fRef);
}

// Calculate the maximum error for any sample value that results from 32-bit floating point representation
float CalculateVec3UncompressedError(ITGEOM::Vec3Array const &avValues)
{
	static const float kfErrorFP32 = 1.0f/16777216.0f;	//1/2^24
	float fMaxErrorSqr = 0.0f;
	ITGEOM::Vec3Array::const_iterator it = avValues.begin(), itEnd = avValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Vec3& v = *it;
		ITGEOM::Vec3 vError( fabsf(v.x * kfErrorFP32), fabsf(v.y * kfErrorFP32), fabsf(v.z * kfErrorFP32) );
		vError.x = U32AsFloat( FloatAsU32(vError.x) & 0x7F800000 );	// set mantissa to zero
		vError.y = U32AsFloat( FloatAsU32(vError.y) & 0x7F800000 );	// set mantissa to zero
		vError.z = U32AsFloat( FloatAsU32(vError.z) & 0x7F800000 );	// set mantissa to zero
		float fErrorSqr = vError.x*vError.x + vError.y*vError.y + vError.z*vError.z;
		if (fMaxErrorSqr < fErrorSqr)
			fMaxErrorSqr = fErrorSqr;
	}
	float fMaxError = sqrtf(fMaxErrorSqr);
	return fMaxError;
}

// Calculate the largest error that will result from compressing avValues using float16 compression
float CalculateVec3Float16Error(ITGEOM::Vec3Array const &avValues)
{
	float fMaxError = 0.0f;
	ITGEOM::Vec3Array::const_iterator it = avValues.begin(), itEnd = avValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Vec3& v = *it;
		Vec3Half cv( v );
		ITGEOM::Vec3 dv = cv.GetVec();
		float fError = ComputeError_Vec3(dv, v);
		if (fMaxError < fError)
			fMaxError = fError;
	}
	return fMaxError;
}

// Calculate the maximum error for any sample value that results from 32-bit floating point representation
float CalculateQuatUncompressedError(ITGEOM::QuatArray const &aqValues)
{
	static const float kfErrorFP32 = 1.0f/16777216.0f;	//1/2^24
	float fMaxErrorSqr = 0.0f;
	ITGEOM::QuatArray::const_iterator it = aqValues.begin(), itEnd = aqValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Quat& q = *it;
		ITGEOM::Vec4 vError( fabsf(q.x * kfErrorFP32), fabsf(q.y * kfErrorFP32), fabsf(q.z * kfErrorFP32), fabsf(q.w * kfErrorFP32) );
		vError.x = U32AsFloat( FloatAsU32(vError.x) & 0x7F800000 );	// set mantissa to zero
		vError.y = U32AsFloat( FloatAsU32(vError.y) & 0x7F800000 );	// set mantissa to zero
		vError.z = U32AsFloat( FloatAsU32(vError.z) & 0x7F800000 );	// set mantissa to zero
		vError.w = U32AsFloat( FloatAsU32(vError.w) & 0x7F800000 );	// set mantissa to zero
		float fErrorSqr = 4.0f * ( vError.x*vError.x + vError.y*vError.y + vError.z*vError.z + vError.w*vError.w );
		if (fMaxErrorSqr < fErrorSqr)
			fMaxErrorSqr = fErrorSqr;
	}
	float fMaxError = sqrtf(fMaxErrorSqr);
	return fMaxError;
}

// Calculate the largest error that will result from compressing aqValues using quat48smallest3 compression
float CalculateQuat48SmallestThreeError(ITGEOM::QuatArray const &aqValues)
{
	if (!QuatSmallest3::SetBitPackingFormat(kVbpfConstQuat48SmallestThree)) {
		ITASSERT(0);
	}
	float fMaxError = 0.0f;
	ITGEOM::QuatArray::const_iterator it = aqValues.begin(), itEnd = aqValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Quat& q = *it;
		QuatSmallest3 cq = QuatSmallest3(q);
		ITGEOM::Quat dq = cq.GetVec();
		// compute linear error
		float fError = ComputeError_Quat(dq, q);
		if (fMaxError < fError)
			fMaxError = fError;
	}
	return fMaxError;
}

// compress a Vec3Array to Vec3HalfArray
bool DoCompress_Vec3_Float16( Vec3HalfArray& aCompressed, ITGEOM::Vec3Array& avDecompressed, const ITGEOM::Vec3Array& avValues )
{
	aCompressed.resize( avValues.size() );
	avDecompressed.resize( avValues.size() );

	ITGEOM::Vec3Array::const_iterator it = avValues.begin(), itEnd = avValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Vec3& v = *it;
		Vec3Half cv( v );
		aCompressed[i] = cv;
		ITGEOM::Vec3 dv = cv.GetVec();
		// compute linear error
		float error = ComputeError_Vec3(dv, v); (void)error;
		avDecompressed[i] = dv;
	}
	return true;
}

void GetVec3Range( U64_Array const& aContextData, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias )
{
	if (aContextData.size() < 2) {
		vScale = vBias = ITGEOM::Vec3(0.0f, 0.0f, 0.0f);
		return;
	}
	U64 uScale = aContextData[0];
	U64 uBias = aContextData[1];
	vScale = ITGEOM::Vec3( U32AsFloat( (U32)((uScale>>33) & 0x7FFFFE00) ), U32AsFloat( (U32)((uScale>>11) & 0x7FFFFC00) ), U32AsFloat( (U32)((uScale<<10) & 0x7FFFFC00) ) );
	vBias = ITGEOM::Vec3( U32AsFloat( (U32)((uBias>>32) & 0xFFFFFC00) ), U32AsFloat( (U32)((uBias>>10) & 0xFFFFF800) ), U32AsFloat( (U32)((uBias<<11) & 0xFFFFF800) ) );
}

void CalculateVec3Range( ITGEOM::Vec3 const &vMin, ITGEOM::Vec3 const &vMax, Vec3BitPackingFormat bitPackingFormat, U64_Array& aContextData )
{
	// estimate our error tolerance conservatively at (vMax-vMin)/(2^(numBits + 1))
	// contract our range by this error tolerance estimate, then
	// round down our contracted min value to generate a bias vector in format 1.8.13 - 1.8.12 - 1.8.12 (float 22-21-21)
	// and round up our contracted range to generate a scale vector in format 0.8.14 - 0.8.13 - 0.8.13 (unsigned float 22-21-21)
	ITGEOM::Vec3 vBias = vMin;
	ITGEOM::Vec3 vUpper = vMax;
	U32 bias_x, bias_y, bias_z;
	ITGEOM::Vec3 vScale(0.0f, 0.0f, 0.0f);
	U32 scale_x = 0, scale_y = 0, scale_z = 0;
	float fET;
	if (bitPackingFormat.m_numBitsX > 0) {
		if (bitPackingFormat.m_numBitsX == 32)
			fET = (vMax.x - vMin.x) * 0.5f / 4294967296.0f;
		else
			fET = (vMax.x - vMin.x) * 0.5f / (float)(1u << bitPackingFormat.m_numBitsX);
		vBias.x += fET;
		vUpper.x -= fET;
		bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? 0 : 0x3FF)) >> 10;
		vBias.x = U32AsFloat( bias_x << 10 );
		U32 mask_x = ((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsX);
		vScale.x = ( vUpper.x - vBias.x ) / (float)mask_x;
		scale_x = ( FloatAsU32( vScale.x ) + 0x1FF ) >> 9;
		vScale.x = U32AsFloat( scale_x << 9 );
	} else {
		vBias.x = (vMin.x + vMax.x) * 0.5f;
		bias_x = (FloatAsU32(vBias.x) + (vBias.x > 0 ? 0x200 : 0x1FF)) >> 10;
		vBias.x = U32AsFloat( bias_x << 10 );
	}
	if (bitPackingFormat.m_numBitsY > 0) {
		if (bitPackingFormat.m_numBitsY == 32)
			fET = (vMax.y - vMin.y) * 0.5f / 4294967296.0f;
		else
			fET = (vMax.y - vMin.y) * 0.5f / (float)(1u << bitPackingFormat.m_numBitsY);
		vBias.y += fET;
		vUpper.y -= fET;
		bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? 0 : 0x7FF)) >> 11;
		vBias.y = U32AsFloat( bias_y << 11);
		U32 mask_y = ((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsY);
		vScale.y = ( vUpper.y - vBias.y ) / (float)mask_y;
		scale_y = ( FloatAsU32( vScale.y ) + 0x3FF ) >> 10;
		vScale.y = U32AsFloat( scale_y << 10 );
	} else {
		vBias.y = (vMin.y + vMax.y) * 0.5f;
		bias_y = (FloatAsU32(vBias.y) + (vBias.y > 0 ? 0x400 : 0x3FF)) >> 11;
		vBias.y = U32AsFloat( bias_y << 11 );
	}
	if (bitPackingFormat.m_numBitsZ > 0) {
		if (bitPackingFormat.m_numBitsZ == 32)
			fET = (vMax.z - vMin.z) * 0.5f / 4294967296.0f;
		else
			fET = (vMax.z - vMin.z) * 0.5f / (float)(1u << bitPackingFormat.m_numBitsZ);
		vBias.z += fET;
		vUpper.z -= fET;
		bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? 0 : 0x7FF)) >> 11;
		vBias.z = U32AsFloat( bias_z << 11);
		U32 mask_z = ((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsZ);
		vScale.z = ( vUpper.z - vBias.z ) / (float)mask_z;
		scale_z = ( FloatAsU32( vScale.z ) + 0x3FF ) >> 10;
		vScale.z = U32AsFloat( scale_z << 10 );
	} else {
		vBias.z = (vMin.z + vMax.z) * 0.5f;
		bias_z = (FloatAsU32(vBias.z) + (vBias.z > 0 ? 0x400 : 0x3FF)) >> 11;
		vBias.z = U32AsFloat( bias_z << 11 );
	}
	if (aContextData.size() < 2)
		aContextData.resize(2);
	aContextData[0] = ((U64)scale_x << 42) | ((U64)scale_y << 21) | (U64)scale_z;
	aContextData[1] = ((U64)bias_x << 42) | ((U64)bias_y << 21) | (U64)bias_z;
}
void CalculateVec3Range( ITGEOM::Vec3Array const &avValues, Vec3BitPackingFormat bitPackingFormat, U64_Array& aContextData )
{
	BDVOL::BoundBox bbox(avValues);
	CalculateVec3Range(bbox.MinPoint(), bbox.MaxPoint(), bitPackingFormat, aContextData);
}
void CalculateVec3Range( ITGEOM::Vec3 const& vMin, ITGEOM::Vec3 const& vMax, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3& vScale, ITGEOM::Vec3& vBias )
{
	U64_Array aContextData;
	CalculateVec3Range(vMin, vMax, bitPackingFormat, aContextData);
	GetVec3Range(aContextData, vScale, vBias);
}
void CalculateVec3Range( ITGEOM::Vec3Array const& avValues, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3& vScale, ITGEOM::Vec3& vBias )
{
	BDVOL::BoundBox bbox(avValues);
	CalculateVec3Range(bbox.MinPoint(), bbox.MaxPoint(), bitPackingFormat, vScale, vBias);
}

void CalculateVec3RangeAndFormat( ITGEOM::Vec3 const& vMin, ITGEOM::Vec3 const& vMax, float fErrorTolerance, Vec3BitPackingFormat& bitPackingFormat, U64_Array& aContextData )
{
	static const float kfSqrt3Inv = 0.57735027f;
	float fErrorTolerancePerComponent = fErrorTolerance * kfSqrt3Inv;
	ITGEOM::Vec3 vErrorTolerance;
	{
		// There's no point to allowing the component error tolerance to be smaller than the floating point error:
		ITGEOM::Vec3 vMaxAbs( max(fabsf(vMax.x), fabsf(vMin.x)), max(fabsf(vMax.y), fabsf(vMin.y)), max(fabsf(vMax.z), fabsf(vMin.z)) );
		ITGEOM::Vec3 vErrorToleranceFP32;
		vErrorToleranceFP32.x = U32AsFloat( FloatAsU32(vMaxAbs.x / 16777216.0f) & 0x7F800000 );	// calculate size of 1/2 mantissa bit
		vErrorToleranceFP32.y = U32AsFloat( FloatAsU32(vMaxAbs.y / 16777216.0f) & 0x7F800000 );	// calculate size of 1/2 mantissa bit
		vErrorToleranceFP32.z = U32AsFloat( FloatAsU32(vMaxAbs.z / 16777216.0f) & 0x7F800000 );	// calculate size of 1/2 mantissa bit
		float fMinETFP32 = max( max(vErrorToleranceFP32.x, vErrorToleranceFP32.y), vErrorToleranceFP32.z );
		if (fErrorTolerancePerComponent < fMinETFP32)
			fErrorTolerancePerComponent = fMinETFP32;
		vErrorTolerance = ITGEOM::Vec3(fErrorTolerancePerComponent, fErrorTolerancePerComponent, fErrorTolerancePerComponent);
	}

	// Find bias by rounding vMin + vErrorTolerance down to next 1.8.13-1.8.12-1.8.12 value.
	ITGEOM::Vec3 vBias = vMin + vErrorTolerance;
	U32 bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? 0 : 0x3FF)) >> 10;
	U32 bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? 0 : 0x7FF)) >> 11;
	U32 bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? 0 : 0x7FF)) >> 11;
	vBias.x = U32AsFloat( bias_x << 10 );
	vBias.y = U32AsFloat( bias_y << 11 );
	vBias.z = U32AsFloat( bias_z << 11 );
	
	U32 scale_x = 0, scale_y = 0, scale_z = 0;
	ITGEOM::Vec3 vScale( 0.0f, 0.0f, 0.0f );
	if (vMax.x - vBias.x > vErrorTolerance.x) {
		// calculate how many bits we need to ensure 0.5f * vScale.x <= vErrorTolerance.x:
		bitPackingFormat.m_numBitsX = 32;
		if (vErrorTolerance.x > 0.0f) {
			float fMaxValRequired = ceilf( (vMax.x - vBias.x - vErrorTolerance.x) / (2.0f * vErrorTolerance.x) );
			if (fMaxValRequired + 0.5f <= (float)0xFFFFFFFF) {
				bitPackingFormat.m_numBitsX = (U8)GetNumberOfBits((unsigned)(fMaxValRequired + 0.5f));
				if (bitPackingFormat.m_numBitsX < 1)
					bitPackingFormat.m_numBitsX = 1;
			}
			// Now that we have the number of bits required, minimize our error by choosing a range for which
			// vBias - vMin ~= 0.5f*vScale ~= vMax - vBias+vScale*(2^N-1)
			vScale.x = ldexpf( (vMax.x - vMin.x), -bitPackingFormat.m_numBitsX );
			scale_x = ( FloatAsU32( vScale.x ) ) >> 9;	// round down
			vScale.x = U32AsFloat( scale_x << 9 );
			vBias.x = vMin.x + vScale.x*0.5f;
			bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? 0 : 0x3FF)) >> 10;	// round down
			vBias.x = U32AsFloat( bias_x << 10 );
			if (vBias.x - vErrorTolerance.x > vMin.x) {
				vBias.x = vMin.x + vErrorTolerance.x;
				bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? -1 : 0x400)) >> 10;	// round down
				vBias.x = U32AsFloat( bias_x << 10 );
			}
			// It is possible that rounding may have resulted in non-compliant vBias, vScale.  
			// If so, tweak scale up and reevaluate bias until we find a compliant solution or have 
			// to increase the number of bits we use.
			float fMaxValueX = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsX));
			while (vBias.x + vScale.x*fMaxValueX + vErrorTolerance.x < vMax.x)
			{
				if (U32AsFloat( (scale_x+1) << 9 )*0.5f <= vErrorTolerance.x) {
					scale_x++;
					vScale.x = U32AsFloat( scale_x << 9 );
				} else {
					if (bitPackingFormat.m_numBitsX == 32)
						break;
					bitPackingFormat.m_numBitsX++;
					fMaxValueX = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsX));
					vScale.x = ldexpf( (vMax.x - vMin.x), -bitPackingFormat.m_numBitsX );
					scale_x = ( FloatAsU32( vScale.x ) ) >> 9;	// round down
					vScale.x = U32AsFloat( scale_x << 9 );
				}
				vBias.x = vMin.x + vScale.x*0.5f;
				bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? 0 : 0x3FF)) >> 10;	// round down
				vBias.x = U32AsFloat( bias_x << 10 );
				if (vBias.x - vErrorTolerance.x > vMin.x) {
					vBias.x = vMin.x + vErrorTolerance.x;
					bias_x = (FloatAsU32(vBias.x) + ((vBias.x >= 0.0f) ? -1 : 0x400)) >> 10;	// round down
					vBias.x = U32AsFloat( bias_x << 10 );
				}
			}
			ITASSERT(bitPackingFormat.m_numBitsX == 32 || (vScale.x <= 2.0f*vErrorTolerance.x && vBias.x - vErrorTolerance.x <= vMin.x && vBias.x + vScale.x*fMaxValueX + vErrorTolerance.x >= vMax.x));
		}
	} else {
		// if vMax is within error tolerance of vBias, recenter bias to minimize error:
		vBias.x = 0.5f*(vMin.x + vMax.x);
		bias_x = (FloatAsU32(vBias.x) + ((vBias.x < 0.0f) ? 0x1FF : 0x200)) >> 10;
		vBias.x = U32AsFloat( bias_x << 10 );
		bitPackingFormat.m_numBitsX = 0;
	}
	if (vMax.y - vBias.y > vErrorTolerance.y) {
		// calculate how many bits we need to ensure 0.5f * vScale.y <= vErrorTolerance.y:
		bitPackingFormat.m_numBitsY = 32;
		if (vErrorTolerance.y > 0.0f) {
			float fMaxValRequired = ceilf( (vMax.y - vBias.y - vErrorTolerance.y) / (2.0f * vErrorTolerance.y) );
			if (fMaxValRequired + 0.5f <= (float)0xFFFFFFFF) {
				bitPackingFormat.m_numBitsY = (U8)GetNumberOfBits((unsigned)(fMaxValRequired + 0.5f));
				if (bitPackingFormat.m_numBitsY < 1)
					bitPackingFormat.m_numBitsY = 1;
			}
			// Now that we have the number of bits required, minimize our error by choosing a range for which
			// vBias - vMin ~= 0.5f*vScale ~= vMax - vBias+vScale*(2^N-1)
			vScale.y = ldexpf( (vMax.y - vMin.y), -bitPackingFormat.m_numBitsY );
			scale_y = ( FloatAsU32( vScale.y ) ) >> 10;	// round down
			vScale.y = U32AsFloat( scale_y << 10 );
			vBias.y = vMin.y + vScale.y*0.5f;
			bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? 0 : 0x7FF)) >> 11;	// round down
			vBias.y = U32AsFloat( bias_y << 11 );
			if (vBias.y - vErrorTolerance.y > vMin.y) {
				vBias.y = vMin.y + vErrorTolerance.y;
				bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? -1 : 0x800)) >> 11;	// round down
				vBias.y = U32AsFloat( bias_y << 11 );
			}
			// It is possible that rounding may have resulted in non-compliant vBias, vScale.  
			// If so, tweak scale up and reevaluate bias until we find a compliant solution or have 
			// to increase the number of bits we use.
			float fMaxValueY = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsY));
			while (vBias.y + vScale.y*fMaxValueY + vErrorTolerance.y < vMax.y)
			{
				if (U32AsFloat( (scale_y+1) << 10 )*0.5f <= vErrorTolerance.y) {
					scale_y++;
					vScale.y = U32AsFloat( scale_y << 10 );
				} else {
					if (bitPackingFormat.m_numBitsY == 32)
						break;
					bitPackingFormat.m_numBitsY++;
					fMaxValueY = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsY));
					vScale.y = ldexpf( (vMax.y - vMin.y), -bitPackingFormat.m_numBitsY );
					scale_y = ( FloatAsU32( vScale.y ) ) >> 10;	// round down
					vScale.y = U32AsFloat( scale_y << 10 );
				}
				vBias.y = vMin.y + vScale.y*0.5f;
				bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? 0 : 0x7FF)) >> 11;	// round down
				vBias.y = U32AsFloat( bias_y << 11 );
				if (vBias.y - vErrorTolerance.y > vMin.y) {
					vBias.y = vMin.y + vErrorTolerance.y;
					bias_y = (FloatAsU32(vBias.y) + ((vBias.y >= 0.0f) ? -1 : 0x800)) >> 11;	// round down
					vBias.y = U32AsFloat( bias_y << 11 );
				}
			}
			ITASSERT(bitPackingFormat.m_numBitsY == 32 || (vScale.y <= 2.0f*vErrorTolerance.y && vBias.y - vErrorTolerance.y <= vMin.y && vBias.y + vScale.y*fMaxValueY + vErrorTolerance.y >= vMax.y));
		}
	} else {
		// if vMax is within error tolerance of vBias, recenter bias to minimize error:
		vBias.y = 0.5f*(vMin.y + vMax.y);
		bias_y = (FloatAsU32(vBias.y) + ((vBias.y < 0.0f) ? 0x3FF : 0x400)) >> 11;
		vBias.y = U32AsFloat( bias_y << 11 );
		bitPackingFormat.m_numBitsY = 0;
	}
	if (vMax.z - vBias.z > vErrorTolerance.z) {
		// calculate how many bits we need to ensure 0.5f * vScale.z <= vErrorTolerance.z:
		bitPackingFormat.m_numBitsZ = 32;
		if (vErrorTolerance.z > 0.0f) {
			float fMaxValRequired = ceilf( (vMax.z - vBias.z - vErrorTolerance.z) / (2.0f * vErrorTolerance.z) );
			if (fMaxValRequired + 0.5f <= (float)0xFFFFFFFF) {
				bitPackingFormat.m_numBitsZ = (U8)GetNumberOfBits((unsigned)(fMaxValRequired + 0.5f));
				if (bitPackingFormat.m_numBitsZ < 1)
					bitPackingFormat.m_numBitsZ = 1;
			}
			// Now that we have the number of bits required, minimize our error by choosing a range for which
			// vBias - vMin ~= 0.5f*vScale ~= vMax - vBias+vScale*(2^N-1)
			vScale.z = ldexpf( (vMax.z - vMin.z), -bitPackingFormat.m_numBitsZ );
			scale_z = ( FloatAsU32( vScale.z ) ) >> 10;	// round down
			vScale.z = U32AsFloat( scale_z << 10 );
			vBias.z = vMin.z + vScale.z*0.5f;
			bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? 0 : 0x7FF)) >> 11;	// round down
			vBias.z = U32AsFloat( bias_z << 11 );
			if (vBias.z - vErrorTolerance.z > vMin.z) {
				vBias.z = vMin.z + vErrorTolerance.z;
				bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? -1 : 0x800)) >> 11;	// round down
				vBias.z = U32AsFloat( bias_z << 11 );
			}
			// It is possible that rounding may have resulted in non-compliant vBias, vScale.  
			// If so, tweak scale up and reevaluate bias until we find a compliant solution or have 
			// to increase the number of bits we use.
			float fMaxValueZ = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsZ));
			while (vBias.z + vScale.z*fMaxValueZ + vErrorTolerance.z < vMax.z)
			{
				if (U32AsFloat( (scale_z+1) << 10 )*0.5f <= vErrorTolerance.z) {
					scale_z++;
					vScale.z = U32AsFloat( scale_z << 10 );
				} else {
					if (bitPackingFormat.m_numBitsZ == 32)
						break;
					bitPackingFormat.m_numBitsZ++;
					fMaxValueZ = (float)(((U32)-1) >> (32 - (U32)bitPackingFormat.m_numBitsZ));
					vScale.z = ldexpf( (vMax.z - vMin.z), -bitPackingFormat.m_numBitsZ );
					scale_z = ( FloatAsU32( vScale.z ) ) >> 10;	// round down
					vScale.z = U32AsFloat( scale_z << 10 );
				}
				vBias.z = vMin.z + vScale.z*0.5f;
				bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? 0 : 0x7FF)) >> 11;	// round down
				vBias.z = U32AsFloat( bias_z << 11 );
				if (vBias.z - vErrorTolerance.z > vMin.z) {
					vBias.z = vMin.z + vErrorTolerance.z;
					bias_z = (FloatAsU32(vBias.z) + ((vBias.z >= 0.0f) ? -1 : 0x800)) >> 11;	// round down
					vBias.z = U32AsFloat( bias_z << 11 );
				}
			}
			ITASSERT(bitPackingFormat.m_numBitsZ == 32 || (vScale.z <= 2.0f*vErrorTolerance.z && vBias.z - vErrorTolerance.z <= vMin.z && vBias.z + vScale.z*fMaxValueZ + vErrorTolerance.z >= vMax.z));
		}
	} else {
		// if vMax is within error tolerance of vBias, recenter bias to minimize error:
		vBias.z = 0.5f*(vMin.z + vMax.z);
		bias_z = (FloatAsU32(vBias.z) + ((vBias.z < 0.0f) ? 0x3FF : 0x400)) >> 11;
		vBias.z = U32AsFloat( bias_z << 11 );
		bitPackingFormat.m_numBitsZ = 0;
	}

	if (aContextData.size() < 2)
		aContextData.resize(2);
	aContextData[0] = ((U64)scale_x << 42) | ((U64)scale_y << 21) | (U64)scale_z;
	aContextData[1] = ((U64)bias_x << 42) | ((U64)bias_y << 21) | (U64)bias_z;
}
void CalculateVec3RangeAndFormat( ITGEOM::Vec3Array const& avValues, float fErrorTolerance, Vec3BitPackingFormat& bitPackingFormat, U64_Array& aContextData )
{
	BDVOL::BoundBox bbox(avValues);
	CalculateVec3RangeAndFormat( bbox.MinPoint(), bbox.MaxPoint(), fErrorTolerance, bitPackingFormat, aContextData);
}
void CalculateVec3RangeAndFormat( ITGEOM::Vec3 const& vMin, ITGEOM::Vec3 const& vMax, float fErrorTolerance, Vec3BitPackingFormat& bitPackingFormat, ITGEOM::Vec3& vScale, ITGEOM::Vec3& vBias )
{
	U64_Array aContextData;
	CalculateVec3RangeAndFormat( vMin, vMax, fErrorTolerance, bitPackingFormat, aContextData);
	GetVec3Range(aContextData, vScale, vBias);
}
void CalculateVec3RangeAndFormat( ITGEOM::Vec3Array const& avValues, float fErrorTolerance, Vec3BitPackingFormat& bitPackingFormat, ITGEOM::Vec3& vScale, ITGEOM::Vec3& vBias )
{
	BDVOL::BoundBox bbox(avValues);
	U64_Array aContextData;
	CalculateVec3RangeAndFormat( bbox.MinPoint(), bbox.MaxPoint(), fErrorTolerance, bitPackingFormat, aContextData);
	GetVec3Range(aContextData, vScale, vBias);
}

bool DoCompress_Vec3_Range( U64_Array& aCompressed, ITGEOM::Vec3Array& avDecompressed, U64_Array& aContextData, const ITGEOM::Vec3Array& avValues, Vec3BitPackingFormat bitPackingFormat )
{
	if (bitPackingFormat.m_numBitsX + bitPackingFormat.m_numBitsY + bitPackingFormat.m_numBitsZ != bitPackingFormat.m_numBitsTotal)
		return false;
	if (bitPackingFormat.m_numBitsX > 32 || bitPackingFormat.m_numBitsY > 32 || bitPackingFormat.m_numBitsZ > 32)
		return false;
	if (bitPackingFormat.m_numBitsY + (bitPackingFormat.m_numBitsX & 0x7) > 32)
		return false;

	if (aContextData.size() < 2 || aContextData[0] == (U64)-1) 
	{
		// calculate a bounding box around the points if we haven't already
		CalculateVec3Range(avValues, bitPackingFormat, aContextData);
	}

	ITGEOM::Vec3 vScale, vBias;
	GetVec3Range(aContextData, vScale, vBias);
	Vec3RangeBitPackingFormat format( bitPackingFormat, vScale, vBias );

	aCompressed.resize( avValues.size() );
	avDecompressed.resize( avValues.size() );

	ITGEOM::Vec3Array::const_iterator it = avValues.begin(), itEnd = avValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Vec3& v = *it;
		Vec3Range cv = Vec3Range(v, format);
		aCompressed[i] = cv.m_v.m_v;
		ITGEOM::Vec3 dv = cv.GetVec(format);
		// compute linear error
		float error = ComputeError_Vec3(dv, v); (void)error;
		avDecompressed[i] = dv;
	}
	return true;
}

#if TEST_BLOCK_PREDICTOR_COMPRESSION
static unsigned GetNumberOfBits(U32 value);
static bool DoCompress_Vec3_BlockPredictorTest( IT::ByteStream& stream, ITGEOM::Vec3Array& decompressed, const ITGEOM::Vec3Array& values, Vec3BitPackingFormat bitPackingFormat, unsigned numFramesPerBlock )
{
	if (bitPackingFormat.m_numBitsX + bitPackingFormat.m_numBitsY + bitPackingFormat.m_numBitsZ != bitPackingFormat.m_numBitsTotal)
		return false;
	if (bitPackingFormat.m_numBitsX > 32 || bitPackingFormat.m_numBitsY > 32 || bitPackingFormat.m_numBitsZ > 32)
		return false;
	if (bitPackingFormat.m_numBitsY + (bitPackingFormat.m_numBitsX & 0x7) > 32)
		return false;

	// first calculate a bounding box around the points
	U64_Array aContextData;
	CalculateVec3Range(values, bitPackingFormat, aContextData);
	stream.Write8(aContextData[0]);
	stream.Write8(aContextData[1]);

	ITGEOM::Vec3 vScale, vBias;
	GetVec3Range(aContextData, vScale, vBias);
	ITGEOM::Vec3 vScaleInv(1.0f / vScale.x, 1.0f / vScale.y, 1.0f / vScale.z );

	Vec3Range::SetBitPackingFormat( bitPackingFormat, vScale, vBias );

	unsigned const numFramesTotal = (unsigned)values.size();
	decompressed.resize( numFramesTotal );

	// collect range of predictor corrections for middle frames in each block:
	ITGEOM::Vec3 vCorrectionMaxAbs(0.0f, 0.0f, 0.0f);
	ITGEOM::Vec3 vFirstCorrectionMaxAbs(0.0f, 0.0f, 0.0f);
	unsigned firstFrame;
	for (firstFrame = 0; firstFrame < numFramesTotal-1; firstFrame += numFramesPerBlock-1) {
		unsigned numFrames = (numFramesTotal - firstFrame);
		if (numFrames > numFramesPerBlock)
			numFrames = numFramesPerBlock;
		unsigned lastFrame = firstFrame + numFrames-1;
		ITGEOM::Vec3 v_prev = Vec3Range(values[firstFrame]).GetVec();
		ITGEOM::Vec3 v_last = Vec3Range(values[lastFrame]).GetVec();
		ITGEOM::Vec3 v_predict = ITGEOM::Lerp(v_prev, v_last, 1.0f / (float)(numFrames-1));
		for (unsigned i = firstFrame+1; i < lastFrame; ++i) {
			ITGEOM::Vec3 const& v1 = values[i];
			ITGEOM::Vec3 vCorrection = v1 - v_predict;
			int x = (int)floorf( vCorrection.x * vScaleInv.x + 0.5f );
			int y = (int)floorf( vCorrection.y * vScaleInv.y + 0.5f );
			int z = (int)floorf( vCorrection.z * vScaleInv.z + 0.5f );
			vCorrection = ITGEOM::Vec3( x * vScale.x, y * vScale.y, z * vScale.z );

			float x_abs = fabsf(vCorrection.x), y_abs = fabsf(vCorrection.y), z_abs = fabsf(vCorrection.z);
			if (vCorrectionMaxAbs.x < x_abs) vCorrectionMaxAbs.x = x_abs;
			if (vCorrectionMaxAbs.y < y_abs) vCorrectionMaxAbs.y = y_abs;
			if (vCorrectionMaxAbs.z < z_abs) vCorrectionMaxAbs.z = z_abs;
			if (i == firstFrame+1) {
				if (vFirstCorrectionMaxAbs.x < x_abs) vFirstCorrectionMaxAbs.x = x_abs;
				if (vFirstCorrectionMaxAbs.y < y_abs) vFirstCorrectionMaxAbs.y = y_abs;
				if (vFirstCorrectionMaxAbs.z < z_abs) vFirstCorrectionMaxAbs.z = z_abs;
			}
			v_predict = (v_predict + vCorrection)*2.0f - v_prev;
			v_prev = v1;
		}
	}
	U32 correction_halfrange_x = (U32)floorf( vCorrectionMaxAbs.x * vScaleInv.x + 0.5f );
	U32 correction_halfrange_y = (U32)floorf( vCorrectionMaxAbs.y * vScaleInv.y + 0.5f );
	U32 correction_halfrange_z = (U32)floorf( vCorrectionMaxAbs.z * vScaleInv.z + 0.5f );
	U32 correction_numbits_x = correction_halfrange_x ? GetNumberOfBits(correction_halfrange_x)+1 : 0;
	U32 correction_numbits_y = correction_halfrange_y ? GetNumberOfBits(correction_halfrange_y)+1 : 0;
	U32 correction_numbits_z = correction_halfrange_z ? GetNumberOfBits(correction_halfrange_z)+1 : 0;

	// format data
	stream.Write1( (U8)correction_numbits_x );
	stream.Write1( (U8)correction_numbits_y );
	stream.Write1( (U8)correction_numbits_z );
	stream.Write1( (U8)0 );

	int x_min = 0, x_max = 0, y_min = 0, y_max = 0, z_min = 0, z_max = 0;
	if (correction_numbits_x)
		x_min = -(1<<(correction_numbits_x-1)), x_max = -x_min-1;
	if (correction_numbits_y)
		y_min = -(1<<(correction_numbits_y-1)), y_max = -y_min-1;
	if (correction_numbits_z)
		z_min = -(1<<(correction_numbits_z-1)), z_max = -z_min-1;

	// compression
	for (firstFrame = 0; firstFrame < numFramesTotal-1; firstFrame += numFramesPerBlock-1) {
		unsigned numFrames = (numFramesTotal - firstFrame);
		if (numFrames > numFramesPerBlock)
			numFrames = numFramesPerBlock;
		unsigned lastFrame = firstFrame + numFrames-1;

		// first frame
		unsigned i = firstFrame;
		ITGEOM::Vec3 v_prev, v_last;
		{
			const ITGEOM::Vec3& v = values[i];
			Vec3Range cv = Vec3Range(v);
			stream.WriteBits( cv.m_v.m_v, bitPackingFormat.m_numBitsTotal );
			// compute linear error
			v_prev = cv.GetVec();
			float error = ComputeError_Vec3(v_prev, v); (void)error;
			decompressed[i] = v_prev;
		}
		// last frame
		i = lastFrame;
		{
			const ITGEOM::Vec3& v = values[i];
			Vec3Range cv = Vec3Range(v);
			stream.WriteBits( cv.m_v.m_v, bitPackingFormat.m_numBitsTotal );
			// compute linear error
			v_last = cv.GetVec();
			float error = ComputeError_Vec3(v_last, v); (void)error;
			decompressed[i] = v_last;
		}
		// middle frames
		ITGEOM::Vec3 v_predict = ITGEOM::Lerp(v_prev, v_last, 1.0f/(float)(numFrames-1));
		for (i = firstFrame+1; i < lastFrame; ++i) {
			ITGEOM::Vec3 const& v1 = values[i];
			ITGEOM::Vec3 vCorrection = v1 - v_predict;
			int x = (int)floorf( vCorrection.x * vScaleInv.x + 0.5f );
			int y = (int)floorf( vCorrection.y * vScaleInv.y + 0.5f );
			int z = (int)floorf( vCorrection.z * vScaleInv.z + 0.5f );
			if (x < x_min)
				x = x_min;
			else if (x > x_max)
				x = x_max;
			if (y < y_min)
				y = y_min;
			else if (y > y_max)
				y = y_max;
			if (z < z_min)
				z = z_min;
			else if (z > z_max)
				z = z_max;
			stream.WriteBits((U64)x, correction_numbits_x);
			stream.WriteBits((U64)y, correction_numbits_y);
			stream.WriteBits((U64)z, correction_numbits_z);
			vCorrection = ITGEOM::Vec3( x * vScale.x, y * vScale.y, z * vScale.z );
			ITGEOM::Vec3 dv = v_predict + vCorrection;
			float error = ComputeError_Vec3(dv, v1); (void)error;
			decompressed[i] = dv;
			v_predict = dv*2.0f - v_prev;
			v_prev = dv;
		}
	}
	return true;
}
#endif //TEST_BLOCK_PREDICTOR_COMPRESSION

bool DoCompress_Quat_Smallest3( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat )
{
	if (!QuatSmallest3::SetBitPackingFormat(bitPackingFormat))
		return false;
	if (bitPackingFormat.m_numBitsX > 32 || bitPackingFormat.m_numBitsY > 32 || bitPackingFormat.m_numBitsZ > 30)
		return false;
	if (bitPackingFormat.m_numBitsY + (bitPackingFormat.m_numBitsX & 0x7) > 32)
		return false;
	if (bitPackingFormat.m_numBitsX < 1 || bitPackingFormat.m_numBitsY < 1 || bitPackingFormat.m_numBitsZ < 1)
		return false;

	aCompressed.resize( aqValues.size() );
	aqDecompressed.resize( aqValues.size() );

	ITGEOM::QuatArray::const_iterator it = aqValues.begin(), itEnd = aqValues.end();
	for (unsigned i = 0; it != itEnd; ++it, ++i) {
		const ITGEOM::Quat& q = *it;
		QuatSmallest3 cq = QuatSmallest3(q);
		aCompressed[i] = cq.m_v.m_v;
		ITGEOM::Quat dq = cq.GetVec();
		// compute linear error
		float error = ComputeError_Quat(dq, q); (void)error;
		aqDecompressed[i] = dq;
	}
	return true;
}

void CalculateRawQuatLogOrientation( ITGEOM::QuatArray const& aqValues, ITGEOM::Quat& qMean )
{
	size_t numQuats = aqValues.size();

	TNT::Array2D<double> data(4, (int)numQuats);
	for (int j = 0; j < (int)numQuats; ++j)
		for (int i = 0; i < 4; ++i)
			data[i][j] = (double)aqValues[j][i];

	TNT::Array2D<double> S = TNT::covariance(data);
	JAMA::SVD<double> svd(S);
	TNT::Array1D<double> s;
	TNT::Array2D<double> U;

	svd.getSingularValues(s);
	svd.getU(U);

	qMean = ITGEOM::Quat((float)U[0][0], (float)U[1][0], (float)U[2][0], (float)U[3][0]);
	float fLengthSqr = ITGEOM::Dot4(qMean, qMean);
	if (fLengthSqr < 0.00001f)
		qMean = ITGEOM::Quat(0.0f, 0.0f, 0.0f, 1.0f);
	else
		qMean.Normalize();
}

inline int ClampInt(int v, int min, int max) { return (v <= min) ? min : (v >= max) ? max : v; }

static const float f1_div_x8000 = 1.0f / (float)0x8000;
static const float f1_plus_epsilon = 1.00000012f;	// floating point error
// in the worst case scenario, quatization can only change the length of a normalized quaternion by the following amount:
static const float kfMaxQuatQuantizationDenormFactor = 2.0f /*sqrt(1^2+1^2+1^2+1^2)*/ * 0.5f * f1_div_x8000 * f1_plus_epsilon;

static inline ITGEOM::Quat QuatExp_RuntimeApproximation( ITGEOM::Vec3 const &v )
{
	// NOTE: Actual algorithm used by run-time code, not as precise as QuatExp ...
	float fThetaSqr = (v.x*v.x + v.y*v.y + v.z*v.z);
	float fCos = 1.0f + fThetaSqr * (-0.5f + fThetaSqr * (0.041574799f + fThetaSqr * -0.0012921074f));
	float fSinc = 1.0f + fThetaSqr * (-0.16666667f + fThetaSqr * (0.0083230548f + fThetaSqr * -0.00018759677f));
	return ITGEOM::Quat( v.x * fSinc, v.y * fSinc, v.z * fSinc, fCos );
}

void CalculateQuatLogOrientation( ITGEOM::QuatArray const& aqValues, ITGEOM::Quat& qMean )
{
	CalculateRawQuatLogOrientation( aqValues, qMean );
	//HACK: unless we have a quaternion with -1.0 as a component, negate it, since it will often have +1.0f as a component
	// and +1.0f can not be represented well with our fixed point compression.
	if ((qMean.x > -1.0f + f1_div_x8000) && (qMean.y > -1.0f + f1_div_x8000) && (qMean.z > -1.0f + f1_div_x8000) && (qMean.w > -1.0f + f1_div_x8000))
		qMean *= -1.0f;
	for (int i = 0; i < 4; ++i) {
		int ic = ClampInt( (int)((qMean[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		qMean[i] = (float)ic * f1_div_x8000;
	}
	ITASSERT(fabsf(qMean.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
}

void CalculateQuatLogOrientation( ITGEOM::QuatArray const &aqValues, U64_Array &aContextData )
{
	ITGEOM::Quat qMean;
	CalculateRawQuatLogOrientation( aqValues, qMean );

	//HACK: unless we have a quaternion with -1.0 as a component, negate it, since it will often have +1.0f as a component
	// and +1.0f can not be represented well with our fixed point compression.
	if ((qMean.x > -1.0f + f1_div_x8000) && (qMean.y > -1.0f + f1_div_x8000) && (qMean.z > -1.0f + f1_div_x8000) && (qMean.w > -1.0f + f1_div_x8000))
		qMean *= -1.0f;
	U64 uContextMean = 0;
	for (int i = 0; i < 4; ++i) {
		short ic = (short)ClampInt( (int)((qMean[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		uContextMean |= ((U64)((unsigned short)ic) << (i*16));
	}
	if (aContextData.size() < 3)
		aContextData.resize(3, (U64)-1);
	aContextData[2] = uContextMean;
}

void GetQuatLogOrientation( U64_Array const& aContextData, ITGEOM::Quat &qMean )
{
	if (aContextData.size() < 3) {
		qMean = ITGEOM::Quat(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}
	U64 uContextMean = aContextData[2];
	for (int i = 0; i < 4; ++i) {
		int ic = (short)((uContextMean >> (i*16)) & 0xFFFF);
		qMean[i] = (float)ic * f1_div_x8000;
	}
	ITASSERT(fabsf(qMean.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
}

void CalculateRawQuatLogPcaOrientation( ITGEOM::QuatArray const& aqValues, ITGEOM::Quat& qPre, ITGEOM::Quat& qPost )
{
	// precompression pass through values to generate qMean:
	ITGEOM::Quat qMean;
	CalculateRawQuatLogOrientation( aqValues, qMean );

	// second precompression pass to calculate qPCA:
	size_t numQuats = aqValues.size();
	ITGEOM::Quat qMeanConjugate = qMean.Conjugate();

	// Calculate the principle axes rotation
	TNT::Array2D<double> data(3, (int)numQuats);
	for (size_t i = 0; i < numQuats; ++i) {
		ITGEOM::Quat qRef = aqValues[i];
		if (Dot4(qRef, qMean) < 0)
			qRef *= -1.0f;
		ITGEOM::Quat qRel = qMeanConjugate * qRef;
		ITGEOM::Vec3 v = QuatLog(qRel);
		for (int j = 0; j < 3; ++j)
			data[j][i] = v[j];
	}
	TNT::Array2D<double> W = TNT::covariance(data);
	double const dfNumQuatsInv = 1.0 / numQuats;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			W[i][j] *= dfNumQuatsInv;
	JAMA::SVD<double> svd(W);
	TNT::Array1D<double> s;
	TNT::Array2D<double> U;
	svd.getSingularValues(s);
	svd.getU(U);

	int numDOF = 0;
	for (int i = 0; i < s.dim(); ++i)
		if (s[i] > 0.000001)
			numDOF++;
	if (numDOF < 1)
		numDOF = 1;
	ITGEOM::Matrix3x3 mat(
		(float)U[0][0], (float)U[0][1], (float)U[0][2],
		(float)U[1][0], (float)U[1][1], (float)U[1][2],
		(float)U[2][0], (float)U[2][1], (float)U[2][2] );
	{
		// sort columns (axes) of mat to make it as diagonal as possible:
		int iLargestRow = 0, iLargestCol = 0;
		float fLargestVal = -1.0f;
		for (int iRow = 0; iRow < 3; iRow++) {
			for (int iCol = 0; iCol < 3; iCol++) {
				if (fabsf( mat[iRow][iCol] ) > fLargestVal) {
					fLargestVal = fabsf( mat[iRow][iCol] );
					iLargestRow = iRow; iLargestCol = iCol;
				}
			}
		}
		// negate column if largest value is negative
		if (mat[iLargestRow][iLargestCol] < 0) {
			for (int iRow = 0; iRow < 3; iRow++)
				mat[iRow][iLargestCol] *= -1.0f;
		}
		// move largest value onto diagonal by swapping columns:
		if (iLargestCol != iLargestRow) {
			for (int iRow = 0; iRow < 3; iRow++)
				std::swap( mat[iRow][iLargestRow], mat[iRow][iLargestCol] );
			iLargestCol = iLargestRow;
		}
		// find largest value in remaining 2x2 matrix:
		fLargestVal = -1.0f;
		int iFirstRowCol = iLargestRow;
		for (int iRow = 0; iRow < 3; iRow++) {
			if (iRow == iFirstRowCol) continue;
			for (int iCol = 0; iCol < 3; iCol++) {
				if (iCol == iFirstRowCol) continue;
				if (fabsf( mat[iRow][iCol] ) > fLargestVal) {
					fLargestVal = fabsf( mat[iRow][iCol] );
					iLargestRow = iRow; iLargestCol = iCol;
				}
			}
		}
		// negate column if largest value is negative
		if (mat[iLargestRow][iLargestCol] < 0) {
			for (int iRow = 0; iRow < 3; iRow++)
				mat[iRow][iLargestCol] *= -1.0f;
		}
		// swap next largest column onto diagonal by swapping columns:
		if (iLargestCol != iLargestRow) {
			for (int iRow = 0; iRow < 3; iRow++)
				std::swap( mat[iRow][iLargestRow], mat[iRow][iLargestCol] );
			iLargestCol = iLargestRow;
		}
		int iSecondRowCol = iLargestCol;
		// negate last column if left-handed
		if (mat.Determinant() < 0) {
			int iLastRowCol = 3 - iSecondRowCol - iFirstRowCol;
			for (int iRow = 0; iRow < 3; iRow++)
				mat[iRow][iLastRowCol] *= -1.0f;
		}
	}
	ITGEOM::QuatFromMatrix3x3(&qPost, &mat);
	float fLengthSqr = ITGEOM::Dot4(qPost, qPost);
	if (fLengthSqr < 0.00001f)
		qPost = ITGEOM::Quat(0.0f, 0.0f, 0.0f, 1.0f);
	else
		qPost.Normalize();
	qPre = qMean * qPost.Conjugate();
	qPre.Normalize();
}

void CalculateQuatLogPcaOrientation( ITGEOM::QuatArray const& aqValues, ITGEOM::Quat& qPre, ITGEOM::Quat& qPost )
{
	CalculateRawQuatLogPcaOrientation( aqValues, qPre, qPost );
	//HACK: unless we have a quaternion with -1.0 as a component, negate it, since it will often have +1.0f as a component
	// and +1.0f can not be represented well with our fixed point compression.
	if ((qPre.x > -1.0f + f1_div_x8000) && (qPre.y > -1.0f + f1_div_x8000) && (qPre.z > -1.0f + f1_div_x8000) && (qPre.w > -1.0f + f1_div_x8000))
		qPre *= -1.0f;
	if ((qPost.x > -1.0f + f1_div_x8000) && (qPost.y > -1.0f + f1_div_x8000) && (qPost.z > -1.0f + f1_div_x8000) && (qPost.w > -1.0f + f1_div_x8000))
		qPost *= -1.0f;
	for (int i = 0; i < 4; ++i) {
		int ic = ClampInt( (int)((qPre[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		qPre[i] = (float)ic * f1_div_x8000;
		ic = ClampInt( (int)((qPost[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		qPost[i] = (float)ic * f1_div_x8000;
	}
	ITASSERT(fabsf(qPost.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
	ITASSERT(fabsf(qPre.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
}

void CalculateQuatLogPcaOrientation( ITGEOM::QuatArray const& aqValues, U64_Array& aContextData )
{
	ITGEOM::Quat qPre, qPost;
	CalculateRawQuatLogPcaOrientation( aqValues, qPre, qPost );
	//HACK: unless we have a quaternion with -1.0 as a component, negate it, since it will often have +1.0f as a component
	// and +1.0f can not be represented well with our fixed point compression.
	if ((qPre.x > -1.0f + f1_div_x8000) && (qPre.y > -1.0f + f1_div_x8000) && (qPre.z > -1.0f + f1_div_x8000) && (qPre.w > -1.0f + f1_div_x8000))
		qPre *= -1.0f;
	if ((qPost.x > -1.0f + f1_div_x8000) && (qPost.y > -1.0f + f1_div_x8000) && (qPost.z > -1.0f + f1_div_x8000) && (qPost.w > -1.0f + f1_div_x8000))
		qPost *= -1.0f;
	U64 uContextPre = 0, uContextPost = 0;
	for (int i = 0; i < 4; ++i) {
		short ic = (short)ClampInt( (int)((qPre[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		uContextPre |= ((U64)((unsigned short)ic) << (i*16));
		ic = (short)ClampInt( (int)((qPost[i] + 1.0f) * 0x8000 + 0.5f) - 0x8000, -0x8000, 0x7FFF );
		uContextPost |= ((U64)((unsigned short)ic) << (i*16));
	}
	if (aContextData.size() < 4)
		aContextData.resize(4, (U64)-1);
	aContextData[2] = uContextPre;
	aContextData[3] = uContextPost;
}

void GetQuatLogPcaOrientation( U64_Array const& aContextData, ITGEOM::Quat &qPre, ITGEOM::Quat &qPost )
{
	if (aContextData.size() < 4) {
		qPre = qPost = ITGEOM::Quat(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}
	U64 uContextPre = aContextData[2], uContextPost = aContextData[3];
	for (int i = 0; i < 4; ++i) {
		int ic = (short)((uContextPre >> (i*16)) & 0xFFFF);
		qPre[i] = (float)ic * f1_div_x8000;
		ic = (short)((uContextPost >> (i*16)) & 0xFFFF);
		qPost[i] = (float)ic * f1_div_x8000;
	}
	ITASSERT(fabsf(qPost.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
	ITASSERT(fabsf(qPre.Length() - 1.0f) <= kfMaxQuatQuantizationDenormFactor);
}

bool DoCompress_Quat_Log( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, U64_Array& aContextData, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3Array& avPrevOutputLogValues )
{
	if (bitPackingFormat.m_numBitsX + bitPackingFormat.m_numBitsY + bitPackingFormat.m_numBitsZ != bitPackingFormat.m_numBitsTotal)
		return false;
	if (bitPackingFormat.m_numBitsX > 32 || bitPackingFormat.m_numBitsY > 32 || bitPackingFormat.m_numBitsZ > 32)
		return false;
	if (bitPackingFormat.m_numBitsY + (bitPackingFormat.m_numBitsX & 0x7) > 32)
		return false;
	if (aContextData.size() < 3)
		return false;

	bool bCrossCorrelated = bitPackingFormat.m_xAxis != 0 || bitPackingFormat.m_yAxis != 0 || bitPackingFormat.m_zAxis != 0;
	if (bCrossCorrelated && avPrevOutputLogValues.size() != aqValues.size()) {
		ITASSERT(!bCrossCorrelated || avPrevOutputLogValues.size() == aqValues.size());
		return false;
	}
	unsigned iNumSamples = (unsigned)aqValues.size();

	// extract previously calculated qMean from aContextData:
	ITGEOM::Quat qMean;
	GetQuatLogOrientation(aContextData, qMean);

	// convert values to vector array for compression
	ITGEOM::Quat qMeanConjugate = qMean.Conjugate();

	ITGEOM::QuatArray::const_iterator it = aqValues.begin(), itEnd = aqValues.end();
	ITGEOM::Vec3Array avLogValues;
	for (; it != itEnd; ++it) {
		ITGEOM::Quat q = qMeanConjugate * (*it);
		// Take the log of q and store it as a vector
		ITGEOM::Vec3 v = QuatLog( q );
		avLogValues.push_back(v);
	}

	if (bCrossCorrelated) {
		// calculate log space delta values by subtracting the correlated previous output log value channel:
		ITGEOM::Vec3Array avLogDeltaValues;
		for (unsigned i = 0; i < iNumSamples; ++i) {
			ITGEOM::Vec3 vLogDelta = avLogValues[i];
			ITGEOM::Vec3 const& vPrevOutputLogValue = avPrevOutputLogValues[i];
//			if (bitPackingFormat.m_xAxis >= 5) vLogDelta[0] += vPrevOutputLogValue[bitPackingFormat.m_xAxis-5];
//			else
			if (bitPackingFormat.m_xAxis) vLogDelta[0] -= vPrevOutputLogValue[bitPackingFormat.m_xAxis-1];
//			if (bitPackingFormat.m_yAxis >= 5) vLogDelta[1] += vPrevOutputLogValue[bitPackingFormat.m_yAxis-5];
//			else
			if (bitPackingFormat.m_yAxis) vLogDelta[1] -= vPrevOutputLogValue[bitPackingFormat.m_yAxis-1];
//			if (bitPackingFormat.m_zAxis >= 5) vLogDelta[2] += vPrevOutputLogValue[bitPackingFormat.m_zAxis-5];
//			else
			if (bitPackingFormat.m_zAxis) vLogDelta[2] -= vPrevOutputLogValue[bitPackingFormat.m_zAxis-1];
			avLogDeltaValues.push_back(vLogDelta);
		}

		// force reevaluation of bias and scale
		aContextData[0] = aContextData[1] = (U64)-1;
		// compress log deltas
		if (!DoCompress_Vec3_Range(aCompressed, avLogDeltaValues, aContextData, avLogDeltaValues, bitPackingFormat))
			return false;

		// avLogDeltaValues now contains decompressed log space deltas; convert them back to log space:
		for (unsigned i = 0; i < iNumSamples; ++i) {
			ITGEOM::Vec3 vOutputLogValue = avLogDeltaValues[i];
			ITGEOM::Vec3& vPrevOutputLogValue = avPrevOutputLogValues[i];
//			if (bitPackingFormat.m_xAxis >= 5) vOutputLogValue[0] -= vPrevOutputLogValue[bitPackingFormat.m_xAxis-5];
//			else
			if (bitPackingFormat.m_xAxis) vOutputLogValue[0] += vPrevOutputLogValue[bitPackingFormat.m_xAxis-1];
//			if (bitPackingFormat.m_yAxis >= 5) vOutputLogValue[1] -= vPrevOutputLogValue[bitPackingFormat.m_yAxis-5];
//			else
			if (bitPackingFormat.m_yAxis) vOutputLogValue[1] += vPrevOutputLogValue[bitPackingFormat.m_yAxis-1];
//			if (bitPackingFormat.m_zAxis >= 5) vOutputLogValue[2] -= vPrevOutputLogValue[bitPackingFormat.m_zAxis-5];
//			else
			if (bitPackingFormat.m_zAxis) vOutputLogValue[2] += vPrevOutputLogValue[bitPackingFormat.m_zAxis-1];
			vPrevOutputLogValue = vOutputLogValue;
		}
	} else {
		// compress source log values
		if (!DoCompress_Vec3_Range( aCompressed, avPrevOutputLogValues, aContextData, avLogValues, bitPackingFormat ))
			return false;
	}

	// calculate the decompressed values from the decompressed log values:
	it = aqValues.begin();
	ITGEOM::Vec3Array::const_iterator itDecompressedLog = avPrevOutputLogValues.begin();
	aqDecompressed.resize( iNumSamples );
	for (unsigned i = 0; it != itEnd; ++it, ++itDecompressedLog, ++i) {
		ITGEOM::Quat const& q = *it;
		ITGEOM::Vec3 dv = *itDecompressedLog;
		ITGEOM::Quat dq = qMean * QuatExp_RuntimeApproximation(dv);
		// compute linear error
		float error_total = ComputeError_Quat(dq, q); (void)error_total;
		float error_exact_exp = ComputeError_Quat(qMean * QuatExp(dv), q);	(void)error_exact_exp;
		ITGEOM::Vec3 const& v = avLogValues[i];
		float error_log = 2.0f * ComputeError_Vec3(dv, v); (void)error_log;
		aqDecompressed[i] = dq;
	}
	return true;
}

bool DoCompress_Quat_LogPca( U64_Array& aCompressed, ITGEOM::QuatArray& aqDecompressed, U64_Array& aContextData, const ITGEOM::QuatArray& aqValues, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3Array& avPrevOutputLogValues )
{
	if (bitPackingFormat.m_numBitsX + bitPackingFormat.m_numBitsY + bitPackingFormat.m_numBitsZ != bitPackingFormat.m_numBitsTotal)
		return false;
	if (bitPackingFormat.m_numBitsX > 32 || bitPackingFormat.m_numBitsY > 32 || bitPackingFormat.m_numBitsZ > 32)
		return false;
	if (bitPackingFormat.m_numBitsY + (bitPackingFormat.m_numBitsX & 0x7) > 32)
		return false;
	if (aContextData.size() < 4)
		return false;

	bool bCrossCorrelated = bitPackingFormat.m_xAxis != 0 || bitPackingFormat.m_yAxis != 0 || bitPackingFormat.m_zAxis != 0;
	if (bCrossCorrelated && avPrevOutputLogValues.size() != aqValues.size()) {
		ITASSERT(!bCrossCorrelated || avPrevOutputLogValues.size() == aqValues.size());
		return false;
	}
	unsigned iNumSamples = (unsigned)aqValues.size();

	// extract previously calculated qPre, qPost from aContextData:
	ITGEOM::Quat qPre, qPost;
	GetQuatLogPcaOrientation(aContextData, qPre, qPost);

	// convert values to vector array for compression
	ITGEOM::Quat qPreConjugate = qPre.Conjugate();
	ITGEOM::Quat qPostConjugate = qPost.Conjugate();

	ITGEOM::QuatArray::const_iterator it = aqValues.begin(), itEnd = aqValues.end();
	ITGEOM::Vec3Array avLogValues;
	for (; it != itEnd; ++it) {
		ITGEOM::Quat q = qPreConjugate * (*it) * qPostConjugate;
		// Take the log of q and store it as a vector
		ITGEOM::Vec3 v = QuatLog( q );
		avLogValues.push_back(v);
	}

	if (bCrossCorrelated) {
		// calculate log space delta values by subtracting the correlated previous output log value channel:
		ITGEOM::Vec3Array avLogDeltaValues;
		for (unsigned i = 0; i < iNumSamples; ++i) {
			ITGEOM::Vec3 vLogDelta = avLogValues[i];
			ITGEOM::Vec3 const& vPrevOutputLogValue = avPrevOutputLogValues[i];
//			if (bitPackingFormat.m_xAxis >= 5) vLogDelta[0] += vPrevOutputLogValue[bitPackingFormat.m_xAxis-5];
//			else
			if (bitPackingFormat.m_xAxis) vLogDelta[0] -= vPrevOutputLogValue[bitPackingFormat.m_xAxis-1];
//			if (bitPackingFormat.m_yAxis >= 5) vLogDelta[1] += vPrevOutputLogValue[bitPackingFormat.m_yAxis-5];
//			else
			if (bitPackingFormat.m_yAxis) vLogDelta[1] -= vPrevOutputLogValue[bitPackingFormat.m_yAxis-1];
//			if (bitPackingFormat.m_zAxis >= 5) vLogDelta[2] += vPrevOutputLogValue[bitPackingFormat.m_zAxis-5];
//			else
			if (bitPackingFormat.m_zAxis) vLogDelta[2] -= vPrevOutputLogValue[bitPackingFormat.m_zAxis-1];
			avLogDeltaValues.push_back(vLogDelta);
		}

		// force reevaluation of bias and scale
		aContextData[0] = aContextData[1] = (U64)-1;
		// compress log deltas
		if (!DoCompress_Vec3_Range(aCompressed, avLogDeltaValues, aContextData, avLogDeltaValues, bitPackingFormat))
			return false;

		// avLogDeltaValues now contains decompressed log space deltas; convert them back to log space:
		for (unsigned i = 0; i < iNumSamples; ++i) {
			ITGEOM::Vec3 vOutputLogValue = avLogDeltaValues[i];
			ITGEOM::Vec3& vPrevOutputLogValue = avPrevOutputLogValues[i];
//			if (bitPackingFormat.m_xAxis >= 5) vOutputLogValue[0] += vPrevOutputLogValue[bitPackingFormat.m_xAxis-5];
//			else
			if (bitPackingFormat.m_xAxis) vOutputLogValue[0] += vPrevOutputLogValue[bitPackingFormat.m_xAxis-1];
//			if (bitPackingFormat.m_yAxis >= 5) vOutputLogValue[1] += vPrevOutputLogValue[bitPackingFormat.m_yAxis-5];
//			else
			if (bitPackingFormat.m_yAxis) vOutputLogValue[1] += vPrevOutputLogValue[bitPackingFormat.m_yAxis-1];
//			if (bitPackingFormat.m_zAxis >= 5) vOutputLogValue[2] += vPrevOutputLogValue[bitPackingFormat.m_zAxis-5];
//			else
			if (bitPackingFormat.m_zAxis) vOutputLogValue[2] += vPrevOutputLogValue[bitPackingFormat.m_zAxis-1];
			vPrevOutputLogValue = vOutputLogValue;
		}
	} else {
		// compress source log values
		if (!DoCompress_Vec3_Range( aCompressed, avPrevOutputLogValues, aContextData, avLogValues, bitPackingFormat ))
			return false;
	}

#if TEST_BLOCK_PREDICTOR_COMPRESSION
	{	// Block predictor test
		ByteBlock data_bp;
		IT::ByteStream stream_bp(data_bp, ICETOOLS::kBigEndian);
		ITGEOM::Vec3Array log_values_decompressed_bp;
		DoCompress_Vec3_BlockPredictorTest( stream_bp, log_values_decompressed_bp, avLogValues, bitPackingFormat, 8 );
		ITGEOM::Vec3Array::const_iterator itDecompressedLogBP = log_values_decompressed_bp.begin();
		it = aqValues.begin();
		for (unsigned i = 0; it != itEnd; ++it, ++itDecompressedLogBP, ++i) {
			ITGEOM::Quat const& q = *it;
			ITGEOM::Vec3 dv = *itDecompressedLogBP;
			ITGEOM::Quat dq = qPre * QuatExp_RuntimeApproximation(dv) * qPost;
			// compute linear error
			float error = ComputeError_Quat(dq, q);
			(void)error;
		}
	}
#endif //TEST_BLOCK_PREDICTOR_COMPRESSION

	// calculate the real errors for the log compression
	it = aqValues.begin();
	ITGEOM::Vec3Array::const_iterator itDecompressedLog = avPrevOutputLogValues.begin();
	aqDecompressed.resize( iNumSamples );
	for (unsigned i = 0; it != itEnd; ++it, ++itDecompressedLog, ++i) {
		ITGEOM::Quat const& q = *it;
		ITGEOM::Vec3 dv = *itDecompressedLog;
		ITGEOM::Quat dq = qPre * QuatExp_RuntimeApproximation(dv) * qPost;
		// compute linear error
		float error_total = ComputeError_Quat(dq, q); (void)error_total;
		float error_exact_exp = ComputeError_Quat(qPre * QuatExp(dv) * qPost, q);	(void)error_exact_exp;
		ITGEOM::Vec3 const& v = avLogValues[i];
		float error_log = 2.0f * ComputeError_Vec3(dv, v); (void)error_log;
		aqDecompressed[i] = dq;
	}
	return true;
}

	}	//namespace Tools
}	//namespace OrbisAnim
