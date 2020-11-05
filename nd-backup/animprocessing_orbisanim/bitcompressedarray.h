/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "icelib/icesupport/typeconv.h"		// for Vec3BitPackingFormat
#include "icelib/common/error.h"

namespace ICETOOLS
{
	class StreamWriter;
}

namespace IT
{
	class ByteStream;
}

namespace OrbisAnim {
	namespace Tools {

	const Vec3BitPackingFormat kVbpfNone(0, 0, 0, 0);

	/// Enumeration of types of channels that may be compressed
	enum AnimChannelType {
		kActInvalid			= 0x00,
		kActVec3			= 0x10,		//!< a stream of Vec3s
		kActQuat			= 0x20,		//!< a stream of Quat
		kActFloat			= 0x30,		//!< a stream of floats
	};

	/// Enumeration of the type of compression applied to a channel, 
	/// which is dependent on the type of channel.
	enum AnimChannelCompressionType {
		kAcctInvalid						= 0x00,
		// 3-vector compression formats
		kAcctVec3Uncompressed				= 0x10,	//!< stored as 3 vectors (fp32, fp32, fp32)			(12 bytes per element)
		kAcctVec3Float16					= 0x11,	//!< stored as (fp16, fp16, fp16)					( 6 bytes per element)
		kAcctVec3Range						= 0x12,	//!< stored as (Bx-By-Bz)*scale + bias				( 1 to 8 bytes per element + 12 bytes range data per joint )
		kAcctVec3Auto						= 0x13,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctVec3NumCompressionTypes		= 3,
		kAcctVec3NumMetadataCompressionTypes		= 4,
		// Quaternion compression formats
		kAcctQuatUncompressed				= 0x20,	//!< stored as 4 vectors (fp32, fp32, fp32, fp32)	(16 bytes per element)
		kAcctQuatSmallestThree				= 0x21,	//!< stored as (Bx-By-Bz-2)*sqrt(2)-sqrt(1/2)		( 1 to 8 bytes per element)
		kAcctQuatLog						= 0x22,	//!< stored as qMean*exp((Bx-By-Bz)*PI-PI/2)		( 1 to 8 bytes per element + 4 bytes qMean per joint)
		kAcctQuatLogPca						= 0x23,	//!< stored as qPre*exp((Bx-By-Bz)*PI-PI/2)*qPost	( 1 to 8 bytes per element + 12 bytes qPre, qPost per joint)
		kAcctQuatAuto						= 0x24,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctQuatNumCompressionTypes		= 4,
		kAcctQuatNumMetadataCompressionTypes		= 4,
		// Float compression formats
		kAcctFloatUncompressed				= 0x30,	//!< stored as fp32												( 4 bytes per element)
		kAcctFloatAuto						= 0x31,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctFloatSplineUncompressed		= 0x32,	//!< stored as 6 vectors (fp32, fp32, fp32, fp32, fp32, fp32)	(24 bytes per element)
		kAcctFloatNumCompressionTypes		= 2,
		kAcctFloatNumMetadataCompressionTypes		= 2,
		// 3-vector constant compression formats
		kAcctConstVec3Uncompressed			= 0x90,	//!< stored as 3 vectors (fp32, fp32, fp32)			(12 bytes per element)
		kAcctConstVec3Float16				= 0x91,	//!< stored as (fp16, fp16, fp16)					( 6 bytes per element)
		kAcctConstVec3Auto					= 0x92,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctConstVec3NumCompressionTypes	= 2,
		kAcctConstVec3NumMetadataCompressionTypes	= 3,
		// Quaternion constant compression formats
		kAcctConstQuatUncompressed			= 0xa0,	//!< stored as 4 vectors (fp32, fp32, fp32, fp32)	(16 bytes per element)
		kAcctConstQuat48SmallestThree		= 0xa1,	//!< stored as (16-15-15-2)*sqrt(2)-sqrt(1/2)		( 6 bytes per element)
		kAcctConstQuatAuto					= 0xa2,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctConstQuatNumCompressionTypes	= 2,
		kAcctConstQuatNumMetadataCompressionTypes	= 3,
		// Float constant compression formats
		kAcctConstFloatUncompressed			= 0xb0,	//!< stored as fp32									( 4 bytes per element)
		kAcctConstFloatAuto					= 0xb1,	//!< choose highest compression from available types that will meet error tolerance requirements - NOTE: not a valid compression type outside of AnimMetaData
		kAcctConstFloatNumCompressionTypes	= 1,
		kAcctConstFloatNumMetadataCompressionTypes	= 2,
		// Other constants
		kAcctChannelTypeMask				= 0x70,	//!< mask to extract the channel type from the channel compression type
		kAcctConstMask						= 0x80,	//!< mask to extract the constant bit from the channel compression type
		kAcctCompressionTypeMask			= 0x0F,	//!< mask to extract the compression type index from the channel compression type
		kAcctCompressionTypeUncompressed	= 0x00,	//!< all channel types have an uncompressed value with (type & kAcctCompressionTypeMask) == kAcctTypeUncompressed
	};

	/// Returns true if the given format has a variable bit packing format
	inline bool IsVariableBitPackedFormat(AnimChannelCompressionType compressionType)
	{
		switch (compressionType) {
		case kAcctVec3Range:
		case kAcctQuatSmallestThree:
		case kAcctQuatLog:
		case kAcctQuatLogPca:
			return true;
		default:
			return false;
		}
	}

	/// Returns the total size of all bit format and range data associated with the given compression type
	inline unsigned GetFormatDataSize(AnimChannelCompressionType compressionType)
	{
		switch (compressionType) {
		case kAcctVec3Range:		return 20;	// 32 bit bitformat, 64 bit min, 64 bit max
		case kAcctQuatSmallestThree:return 4;	// 32 bit bitformat
		case kAcctQuatLog:			return 28;	// 32 bit bitformat, 64 bit min, 64 bit max, 64 bit qMean
		case kAcctQuatLogPca:		return 36;	// 32 bit bitformat, 64 bit min, 64 bit max, 64 bit qPre, 64 bit qPost
		default:
			return 0;
		}
	}

	/// Returns true if the given type is auto
	inline bool IsAuto(AnimChannelCompressionType compressionType)
	{
		switch (compressionType) {
		case kAcctVec3Auto:
		case kAcctQuatAuto:
		case kAcctFloatAuto:
		case kAcctConstVec3Auto:
		case kAcctConstQuatAuto:
		case kAcctConstFloatAuto:
			return true;
		default:
			return false;
		}
	}

	// useful bit packing formats:
	extern const Vec3BitPackingFormat kVbpfVec3Uncompressed;			//!< bit packing format for uncompressed 3 vector (animated or constant)
	extern const Vec3BitPackingFormat kVbpfVec3Float16;					//!< bit packing format for float16 compressed 3 vector (animated or constant)
	extern const Vec3BitPackingFormat kVbpfQuatUncompressed;			//!< bit packing format for uncompressed quaternion (animated or constant)
	extern const Vec3BitPackingFormat kVbpfConstQuat48SmallestThree;	//!< bit packing format for 16-15-15-2 48 bit smallest-3 compressed quaternion (constant only)
	extern const Vec3BitPackingFormat kVbpfFloatUncompressed;			//!< bit packing format for uncompressed float (animated or constant)

	/// Returns the fixed bit packing format for any constant compression type.
	extern Vec3BitPackingFormat GetConstCompressionBitFormat(AnimChannelCompressionType constCompressionType);

	//Variable bit-packing compressed streams are stored as U64 arrays:
	typedef std::vector<U64>				U64_Array;

	/// Abstract interface for classes implementing bit compression of an array of values
	class IBitCompressedArray
	{
	public:
		IBitCompressedArray(AnimChannelCompressionType compressionType) : m_compressionType(compressionType) {}
		virtual ~IBitCompressedArray() {}

		/// Returns the type of data compressed in this bit compressed array
		AnimChannelType GetChannelType() const { return (AnimChannelType)(m_compressionType & kAcctChannelTypeMask); }
		/// Returns the compressionType of this compression
		AnimChannelCompressionType GetCompressionType() const { return m_compressionType; }

		/// Returns true if the bit packing format of this compression is fixed for all instances of this compression
		virtual bool IsFixedBitPackingFormat() const = 0;
		/// Returns the bit packing format of this compression
		virtual Vec3BitPackingFormat GetBitPackingFormat() const = 0;
		/// Returns the size of each sample in bits
		virtual size_t GetBitSizeOfSample() const = 0;

		/// Returns the number of range data elements
		virtual size_t GetNumRangeData() const = 0;
		/// Returns the byte size of range data element iRangeElement
		virtual size_t GetSizeOfRangeData(size_t iRangeElement) const = 0;
		/// Returns the byte size of all range data
		virtual size_t GetSizeOfAllRangeData() const = 0;

		/// Returns the number of samples compressed in this array
		virtual size_t GetNumSamples() const = 0;

		/// Write the BYTE ALIGNED data for sample iSample into the given streamwriter in TARGET ENDIAN order.
		/// NOTE: this will likely assert if (GetBitSizeOfSample() & 0x7) != 0.
		virtual void WriteByteAlignedSample(ICETOOLS::StreamWriter& streamWriter, size_t iSample) const = 0;
		/// Terminate the sample data after a series of WriteSample calls.
		/// Note that this is required to allow implementations to interleave their data across calls.
		virtual void WriteByteAlignedSampleEnd(ICETOOLS::StreamWriter& streamWriter) const = 0;

		/// Write the data for sample iSample into the given byte stream as a BIG-ENDIAN order stream of bits.
		virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const = 0;
		/// Terminate the sample data after a series of WriteBitPackedSample calls.
		/// Note that this is required to allow implementations to interleave their data across calls.
		virtual void WriteBitPackedSampleEnd(IT::ByteStream& byteStream) const = 0;

		/// Write the data for range data element iRangeElement into the given streamWriter.
		virtual void WriteRangeData(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const = 0;
		/// Terminate the range data stream after a series of WriteRangeData calls.
		/// Note that this is required to allow implementations to interleave their data across calls.
		virtual void WriteRangeDataEnd(ICETOOLS::StreamWriter& streamWriter, size_t iRangeElement) const = 0;

		/// Write the data for range data element iRangeElement into the given byte stream.
		virtual void WriteRangeData(IT::ByteStream& byteStream, size_t iRangeElement) const = 0;
		/// Terminate the range data stream after a series of WriteRangeData calls.
		/// Note that this is required to allow implementations to interleave their data across calls.
		virtual void WriteRangeDataEnd(IT::ByteStream& byteStream, size_t iRangeElement) const = 0;

		/// Returns the maximum error introduced by compression
		virtual float GetMaxError() const = 0;
		/// Returns the average (RMS) error introduced by compression
		virtual float GetRmsError() const = 0;
		/// Returns the sample index that produced the maximum error
		virtual size_t GetMaxErrorSampleIndex() const = 0;

	protected:
		AnimChannelCompressionType m_compressionType;
	};

	/// Specialization of IBitCompressedArray interface for arrays of ITGEOM::Vec3
	class IBitCompressedVec3Array : public IBitCompressedArray
	{
	public:
		IBitCompressedVec3Array(AnimChannelCompressionType compressionType) : IBitCompressedArray(compressionType) 
		{
			ITASSERT(GetChannelType() == kActVec3);
		}
		virtual ~IBitCompressedVec3Array() {}
		
		virtual ITGEOM::Vec3 GetDecompressedSample(size_t iSample) const = 0;
		virtual void GetDecompressedSamples(ITGEOM::Vec3Array &samples) const = 0;
		virtual U64_Array const *GetCompressedSamples() const = 0;
		virtual U64_Array const *GetContextData() const = 0;
	};

	/// Specialization of IBitCompressedArray interface for arrays of ITGEOM::Quat
	class IBitCompressedQuatArray : public IBitCompressedArray
	{
	public:
		IBitCompressedQuatArray(AnimChannelCompressionType compressionType) : IBitCompressedArray(compressionType) 
		{
			ITASSERT(GetChannelType() == kActQuat);
		}
		virtual ~IBitCompressedQuatArray() {}

		virtual ITGEOM::Quat GetDecompressedSample(size_t iSample) const = 0;
		virtual void GetDecompressedSamples(ITGEOM::QuatArray &samples) const = 0;
		virtual U64_Array const *GetCompressedSamples() const = 0;
		virtual U64_Array const *GetContextData() const = 0;
	};

	/// Specialization of IBitCompressedArray interface for arrays of float values
	class IBitCompressedFloatArray : public IBitCompressedArray
	{
	public:
		IBitCompressedFloatArray(AnimChannelCompressionType compressionType) : IBitCompressedArray(compressionType) 
		{
			ITASSERT(GetChannelType() == kActFloat);
		}
		virtual ~IBitCompressedFloatArray() {}

		virtual float GetDecompressedSample(size_t iSample) const = 0;
		virtual void GetDecompressedSamples(std::vector<float> &samples) const = 0;
		virtual U64_Array const *GetCompressedSamples() const = 0;
		virtual U64_Array const *GetContextData() const = 0;
	};

	/// Make compressed data from of an array of Vec3's using the given compression type:
	///		\returns				the resulting bit compressed data
	///		\param avValues			the vector of input quaternions to compress
	///		\param compressionType	the compression type (one of kAcctVec3* or kAcctConstVec3*)
	///		\param bitPackingFormat	the bit packing format to use (if variable bit packed compression compressionType)
	///		\param paContextData	previously calculated context data (scale, bias), if available.
	///								Scale and bias can be calculated more accurately with a specific
	///								error tolerance than without.
	IBitCompressedVec3Array* BitCompressVec3Array(
		const ITGEOM::Vec3Array& avValues,
		AnimChannelCompressionType compressionType,
		Vec3BitPackingFormat bitPackingFormat = kVbpfNone,
		U64_Array const* paContextData = NULL
		);

	/// Make compressed data from of an array of Quat's using the given compression type:
	///		\returns				the resulting bit compressed data
	///		\param aqValues			the vector of input quaternions to compress
	///		\param compressionType	the compression type (one of kAcctQuat* or kAcctConstQuat*)
	///		\param bitPackingFormat	the bit packing format to use (if variable bit packed compression compressionType)
	///		\param paContextData	previously calculated context data (scale, bias, qMean) for log, (scale, bias, qPre, qPost) for logpca compression, if available.
	///								Due to the instability of log/logpca compression orientation 
	///								data, recalculating these values for an adjusted set of values
	///								(for instance key frame reduced) may result in unpredictable output.
	///		\param pavPrevOutputLogValues	if log compression and bitPackingFormat includes cross correlation axes,
	///								pavPrevOutputLogValues is used as both input (holding the previous log values)
	///								and output (holding the resulting output log values to be passed to the following call)
	IBitCompressedQuatArray* BitCompressQuatArray(
		const ITGEOM::QuatArray& aqValues,
		AnimChannelCompressionType compressionType,
		Vec3BitPackingFormat bitPackingFormat = kVbpfNone,
		U64_Array const* paContextData = NULL,
		ITGEOM::Vec3Array* pavPrevOutputLogValues = NULL
		);

	/// Make compressed data from of an array of float's using the given compression type:
	///		\returns				the resulting bit compressed data
	///		\param afValues			the vector of input floats to compress
	///		\param compressionType	the compression type (one of kAcctFloat* or kAcctConstFloat*)
	///		\param uBits			the number of bits to use (if variable bit packed compression compressionType)
	IBitCompressedFloatArray* BitCompressFloatArray(
		const std::vector<float>& afValues,
		AnimChannelCompressionType compressionType,
		unsigned uBits = 0
		);

	/// Calculate the result of a constant compression compressionType applied to the given value
	ITGEOM::Vec3 CalculateDecompressedConstVec3( AnimChannelCompressionType compressionType, ITGEOM::Vec3 const &value );
	ITGEOM::Quat CalculateDecompressedConstQuat( AnimChannelCompressionType compressionType, ITGEOM::Quat const &value );
	float CalculateDecompressedConstFloat( AnimChannelCompressionType compressionType, float value );

	// Calculate the maximum error for any sample value that results from 32-bit floating point representation
	float CalculateVec3UncompressedError(ITGEOM::Vec3Array const &avValues);
	/// Calculate the largest error that will result from compressing avValues using float16 compression
	float CalculateVec3Float16Error(ITGEOM::Vec3Array const &avValues);
	// Calculate the maximum error for any sample value that results from 32-bit floating point representation
	float CalculateQuatUncompressedError(ITGEOM::QuatArray const &avValues);
	/// Calculate the largest error that will result from compressing aqValues using quat48smallest3 compression
	float CalculateQuat48SmallestThreeError(ITGEOM::QuatArray const &aqValues);

	/// Calculate a scale and bias to use in compressing and decompression vectors by range compression.
	/// The scale returned scale already includes the bit range divide by 2^numBits.
	/// Note that the vectors returned are reduced in precision to match the output data format.
	void CalculateVec3Range( ITGEOM::Vec3 const &vMin, ITGEOM::Vec3 const &vMax, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias );
	void CalculateVec3Range( ITGEOM::Vec3Array const &values, Vec3BitPackingFormat bitPackingFormat, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias );
	/// Calculate the scale and range as above, but return in contextData[0,1] as U64 packed values
	void CalculateVec3Range( ITGEOM::Vec3 const &vMin, ITGEOM::Vec3 const &vMax, Vec3BitPackingFormat bitPackingFormat, U64_Array& contextData );
	void CalculateVec3Range( ITGEOM::Vec3Array const &values, Vec3BitPackingFormat bitPackingFormat, U64_Array& contextData );
	/// Convert the U64 packed values in contextData[0,1] back into scale and bias vectors 
	void GetVec3Range( U64_Array const& contextData, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias );

	/// Calculate a scale, bias, and bitformat to use in compressing and decompression vectors by range compression, based on the given error tolerance.
	/// The scale returned scale already includes the bit range divide by 2^numBits.
	/// Note that the format returned has only m_numBitsX, Y, and Z set (i.e. m_numBytes is not set)
	/// and that the vectors returned are reduced in precision to match the output data format.
	void CalculateVec3RangeAndFormat( ITGEOM::Vec3 const &vMin, ITGEOM::Vec3 const &vMax, float fErrorTolerance, Vec3BitPackingFormat &bitPackingFormat, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias );
	void CalculateVec3RangeAndFormat( ITGEOM::Vec3Array const &avValues, float fErrorTolerance, Vec3BitPackingFormat &bitPackingFormat, ITGEOM::Vec3 &vScale, ITGEOM::Vec3 &vBias );
	/// Calculate the bitformat, scale, and bias and return scale and bias as packed U64 values in contextData[0,1]
	void CalculateVec3RangeAndFormat( ITGEOM::Vec3 const &vMin, ITGEOM::Vec3 const &vMax, float fErrorTolerance, Vec3BitPackingFormat &bitPackingFormat, U64_Array& contextData );
	void CalculateVec3RangeAndFormat( ITGEOM::Vec3Array const &avValues, float fErrorTolerance, Vec3BitPackingFormat &bitPackingFormat, U64_Array& contextData );

	/// Calculate a mean quaternion to use in compressing and decompression quaternions by log compression.
	/// Note that the quaternion returned is reduced in precision to match the output data format.
	void CalculateQuatLogOrientation( ITGEOM::QuatArray const &values, ITGEOM::Quat &qMean );
	/// Calculate the mean quaternion and store as a U64 packed value in contextData[2] (0,1 are used for scale, bias)
	void CalculateQuatLogOrientation( ITGEOM::QuatArray const &values, U64_Array &contextData );
	/// Convert contextData[2] back into quaternion qMean
	void GetQuatLogOrientation( U64_Array const& contextData, ITGEOM::Quat &qMean );

	/// Calculate quaternions to use in compressing and decompression quaternions by log compression with PCA.
	/// Note that the quaternions returned are reduced in precision to match the output data format.
	void CalculateQuatLogPcaOrientation( ITGEOM::QuatArray const &values, ITGEOM::Quat &qPre, ITGEOM::Quat &qPost );
	/// Calculate the mean quaternion and store as a U64 packed value in contextData[2,3] (0,1 are used for scale, bias)
	void CalculateQuatLogPcaOrientation( ITGEOM::QuatArray const &values, U64_Array &contextData );
	/// Convert contextData[2,3] back into quaternions qPre, qPost
	void GetQuatLogPcaOrientation( U64_Array const& contextData, ITGEOM::Quat &qPre, ITGEOM::Quat &qPost );

	}	//namespace Tools
}	//namespace OrbisAnim

