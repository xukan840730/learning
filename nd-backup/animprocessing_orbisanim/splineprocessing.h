/* SCE CONFIDENTIAL
* $PSLibId$
* Copyright (C) 2015 Sony Computer Entertainment Inc.
* All Rights Reserved.
*/

#pragma once

#include <cstdint>
#include "animprocessingstructs.h"
#include "animcompression.h"
#include "bitcompressedarray.h"
#include "icelib/itscene/studiopolicy.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ICETOOLS::StreamWriter;

// Build compressed Hermite spline clip data from source data & 
void BuildCompressedHermiteData(AnimationClipCompressedData& compressedData,	// [out] resultant compressed Hermite spline data
								AnimationBinding const& binding,				// [in] anim curve to joint/float mapping
								AnimationClipSourceData const& sourceData,		// [in] uncompressed Hermite spline data
								ClipCompressionDesc const& compression,			// [in] compression parameters
								AnimationHierarchyDesc const& hierarchyDesc);	// [in] hierarchy descriptor

/// An uncompressed scalar spline
class UncompressedFloatSpline : public IBitCompressedArray
{
public:
	UncompressedFloatSpline(SplineAnim const* spline, bool bConstant, std::string const& name);

	virtual bool IsFixedBitPackingFormat() const;
	virtual Vec3BitPackingFormat GetBitPackingFormat() const;
	virtual size_t GetBitSizeOfSample() const;

	virtual size_t GetNumRangeData() const;
	virtual size_t GetSizeOfRangeData(size_t iRangeElement) const;
	virtual size_t GetSizeOfAllRangeData() const;

	virtual size_t GetNumSamples() const;

	virtual void WriteByteAlignedSample(StreamWriter& streamWriter, size_t iSample) const;
	virtual void WriteByteAlignedSampleEnd(StreamWriter& streamWriter) const;

	virtual void WriteBitPackedSample(IT::ByteStream& byteStream, size_t iSample) const;
	virtual void WriteBitPackedSampleEnd(IT::ByteStream& byteStream) const;

	virtual void WriteRangeData(StreamWriter& streamWriter, size_t iRangeElement) const;
	virtual void WriteRangeDataEnd(StreamWriter& streamWriter, size_t iRangeElement) const;
	virtual void WriteRangeData(IT::ByteStream& byteStream, size_t iRangeElement) const;
	virtual void WriteRangeDataEnd(IT::ByteStream& byteStream, size_t iRangeElement) const;

	virtual float GetMaxError() const;
	virtual float GetRmsError() const;
	virtual size_t	GetMaxErrorSampleIndex() const;

private:
	SplineAnim const*	m_splineAnim;	// not owned by this
	std::string			m_name;
};

inline UncompressedFloatSpline::UncompressedFloatSpline(SplineAnim const* splineAnim, bool bConstant, std::string const& name)
	: IBitCompressedArray(bConstant ? kAcctConstFloatUncompressed : kAcctFloatSplineUncompressed)
	, m_splineAnim(splineAnim)
	, m_name(name)
{
}

inline bool UncompressedFloatSpline::IsFixedBitPackingFormat() const
{
	return true;
}

inline Vec3BitPackingFormat UncompressedFloatSpline::GetBitPackingFormat() const
{
	return kVbpfFloatUncompressed;
}

inline size_t UncompressedFloatSpline::GetBitSizeOfSample() const
{
	return kVbpfFloatUncompressed.m_numBitsTotal;
}

inline size_t UncompressedFloatSpline::GetNumRangeData() const
{
	return 0;
}

inline size_t UncompressedFloatSpline::GetSizeOfRangeData(size_t /*iRangeElement*/) const
{
	return 0;
}

inline size_t UncompressedFloatSpline::GetSizeOfAllRangeData() const
{
	return 0;
}

inline size_t UncompressedFloatSpline::GetNumSamples() const
{
	ITASSERT(m_splineAnim->m_splines.size() == 1);
	HermiteSpline const& spline = m_splineAnim->m_splines[0];
	return spline.getNumKnots();
}

inline void UncompressedFloatSpline::WriteByteAlignedSample(StreamWriter& streamWriter, size_t iSample) const
{
	ITASSERT(m_splineAnim->m_splines.size() == 1);
	HermiteSpline const& spline = m_splineAnim->m_splines[0];
	ITASSERT(iSample < spline.getNumKnots());
	HermiteSpline::Knot const& knot = spline.getKnot(iSample);
	streamWriter.WriteF(knot.m_position.m_x);
	streamWriter.WriteF(knot.m_position.m_y);
	streamWriter.WriteF(knot.m_iVelocity.m_x);
	streamWriter.WriteF(knot.m_iVelocity.m_y);
	streamWriter.WriteF(knot.m_oVelocity.m_x);
	streamWriter.WriteF(knot.m_oVelocity.m_y);
}

inline void UncompressedFloatSpline::WriteByteAlignedSampleEnd(StreamWriter& /*streamWriter*/) const
{
}

inline void UncompressedFloatSpline::WriteBitPackedSample(IT::ByteStream& /*byteStream*/, size_t /*iSample*/) const
{
	//ITASSERT(iSample < m_samples.size());
	//byteStream.WriteBits((U64)F32ToI32(m_samples[iSample]), 32);
}

inline void UncompressedFloatSpline::WriteBitPackedSampleEnd(IT::ByteStream& /*byteStream*/) const
{
}

inline void UncompressedFloatSpline::WriteRangeData(StreamWriter& streamWriter, size_t iRangeElement) const
{
	// only supporting float channels for now
	ITASSERT(m_splineAnim->m_splines.size() == 1);

	switch(iRangeElement) {
		case 0: {
			// spline data
			HermiteSpline const& spline = m_splineAnim->m_splines[0];
			streamWriter.Write1((uint8_t)spline.getPreInfinity());
			streamWriter.Write1((uint8_t)spline.getPostInfinity());
			streamWriter.Write2((uint16_t)spline.getNumKnots());
			break;
		}
		case 1: {
			// channel id (name hash)
			uint32_t floatChannelId = g_theStudioPolicy->MakeIdFromName(m_name, StudioPolicy::kNameTypeFloatChannel);
			streamWriter.Write4(floatChannelId);
			break;
		}
	}
}

inline void UncompressedFloatSpline::WriteRangeDataEnd(StreamWriter& /*streamWriter*/, size_t /*iRangeElement*/) const 
{
}

inline void UncompressedFloatSpline::WriteRangeData(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const 
{
}

inline void UncompressedFloatSpline::WriteRangeDataEnd(IT::ByteStream& /*byteStream*/, size_t /*iRangeElement*/) const 
{
}

inline float UncompressedFloatSpline::GetMaxError() const
{ 
	return 0.0f; 
}

inline float UncompressedFloatSpline::GetRmsError() const 
{ 
	return 0.0f; 
}

inline size_t UncompressedFloatSpline::GetMaxErrorSampleIndex() const 
{ 
	return 0; 
}

} // AnimProcessing
} // Tools
} // OrbisAnim
