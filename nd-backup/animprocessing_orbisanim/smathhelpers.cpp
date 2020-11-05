/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "smathhelpers.h"

namespace OrbisAnim {
	namespace Tools {

float Matrix3x3Error(SMath::Mat44 const& m)
{
	double fErrorSqr = 0.0;
	for (int iRow = 0; iRow < 3; ++iRow) {
		for (int iCol = 0; iCol < 3; ++iCol) {
			double fMij = m[iRow][iCol];
			fErrorSqr += fMij*fMij;
		}
	}
	return (float)sqrt(fErrorSqr);
}

void SMathMat44_DecomposeAffine(SMath::Mat44 const& m, SMath::Vector *pvScale, SMath::Quat *pqRotation, SMath::Point *pvTranslation, SMath::Mat44 *pmSkew)
{
	static const float kfEpsilon = 0.000001f;

	// decompose matrix m into an orthonormal (rotation) part U and a stretch (scale and skew) part H
	SMath::Mat44 H, H_inv;
	SMath::Mat44 A = m;
	A.SetRow(3, SMath::Vec4(0.0f, 0.0f, 0.0f, 1.0f));

	SMath::Mat44 C = SMath::Transpose(A) * A;

	if (pvTranslation)
		*pvTranslation = SMath::Point( m.GetRow(3) );

	// Calculate matrix square root H and inverse square root H_inv of C by Denman-Beavers iteration:
	float det_C = SMath::Determinant3( C );
	if (det_C <= kfEpsilon) {
		// A is degenerate, so C = (A~ * A) has no inverse -> H_inv does not exist.
		// We just leave it up to SMath's matrix to quaternion conversion:
		SMath::Quat qRotation = SMath::Quat( A );
		//NOTE: SMath::SafeNormalize does not compile under gcc-3.2.3 due to a bug in the _mm_andnot_ps intrinsic, which
		// generates an SSE assembly instruction 'pnand' that is not recognized by the gcc assembler:
//		qRotation = SMath::SafeNormalize(qRotation, SMath::Quat(SMath::kIdentity));
		qRotation = ( SMath::Length4( qRotation.GetVec4() ) <= 1.0e-30f) ? SMath::Quat(SMath::kIdentity) : SMath::Normalize( qRotation );
		if (pqRotation)
			*pqRotation = qRotation;

		H = A * SMath::BuildTransform( SMath::Conjugate( qRotation ), SMath::Vec4(SMath::kUnitWAxis) );
	} else {
		SMath::Mat44 I(SMath::kIdentity);
		H = C;
		H_inv = I;
		static const float kfIterationError = 0.0001f;
		SMath::Mat44 H_error = H * H - C;
		float fError = Matrix3x3Error(H_error);
		for (unsigned i = 0; fError > kfIterationError; i++) {
			SMath::Mat44 H_prev = H, H_inv_prev = H_inv;
			H = ( H_prev + Inverse( H_inv_prev) ) * 0.5f;
			H_inv = ( H_inv_prev + Inverse( H_prev) ) * 0.5f;
			H_error = H * H - C;
			fError = Matrix3x3Error(H_error);
		}
# if 1
		{
			// check our final error in H_*H_inv vs. I
			H_error = H * H_inv - I;
			fError = Matrix3x3Error(H_error);
			if (fError > kfIterationError)
				H_inv = Inverse(H);
		}
# endif
		// Calculate our polar decomposition: H = sqrt( transpose(A) * A );  U = A * inverse(H);
		if (pqRotation)
			*pqRotation = SMath::Quat( A * H_inv );
	}

	// extract scale and skew from H_
	SMath::Vector vScale = SMath::Vector( SMath::Length3(H.GetRow(0)), SMath::Length3(H.GetRow(1)), SMath::Length3(H.GetRow(2)) );
	if (pvScale)
		*pvScale = vScale;
	if (pmSkew) {
		SMath::Mat44 mSkew;
		std::vector<unsigned> comps;
		if (vScale.X() >= kfEpsilon) comps.push_back(0);
		if (vScale.Y() >= kfEpsilon) comps.push_back(1);
		if (vScale.Z() >= kfEpsilon) comps.push_back(2);
		switch (comps.size()) {
		case 3:
			mSkew.SetRow(0, H.GetRow(0) / vScale[0]);
			mSkew.SetRow(1, H.GetRow(1) / vScale[1]);
			mSkew.SetRow(2, H.GetRow(2) / vScale[2]);
			mSkew.SetRow(3, SMath::Vec4(SMath::kUnitWAxis));
			break;
		case 2:
			{
				mSkew.SetRow(comps[0], H.GetRow(comps[0]) / vScale[ comps[0] ]);
				mSkew.SetRow(comps[1], H.GetRow(comps[1]) / vScale[ comps[1] ]);
				SMath::Vec4 vCross = SMath::Cross( mSkew.GetRow(comps[0]), mSkew.GetRow(comps[1]) );
				float fCrossLen = SMath::Length3(vCross);
				if (fCrossLen >= kfEpsilon) {
					mSkew.SetRow(3 - comps[0] - comps[1], vCross / fCrossLen);
				} else {
					// we need to generate any vector perpendicular to comps[0]
					if (fabsf( mSkew.Get(comps[0], 0) ) < 1.0f - kfEpsilon)
						vCross = SMath::Normalize3( SMath::Cross( mSkew.GetRow(comps[0]), SMath::Vec4(SMath::kUnitXAxis) ) );
					else
						vCross = SMath::Normalize3( SMath::Cross( mSkew.GetRow(comps[0]), SMath::Vec4(SMath::kUnitYAxis) ) );
					mSkew.SetRow(3 - comps[0] - comps[1], vCross);
				}
				mSkew.SetRow(3, SMath::Vec4(SMath::kUnitWAxis));
				break;
			}
		case 1:
			{
				mSkew.SetRow(comps[0], H.GetRow(comps[0]) / vScale[ comps[0] ]);
				// we need to generate any vector perpendicular to comps[0]
				SMath::Vec4 vCross;
				if (fabsf( mSkew.Get(comps[0], 0) ) < 1.0f - kfEpsilon)
					vCross = SMath::Normalize3( SMath::Cross( mSkew.GetRow(comps[0]), SMath::Vec4(SMath::kUnitXAxis) ) );
				else
					vCross = SMath::Normalize3( SMath::Cross( mSkew.GetRow(comps[0]), SMath::Vec4(SMath::kUnitYAxis) ) );
				unsigned iComp1 = (comps[0] + 1)%3;
				mSkew.SetRow(iComp1, vCross);
				mSkew.SetRow(3 - comps[0] - iComp1, SMath::Cross(mSkew.GetRow(comps[0]), vCross));
				mSkew.SetRow(3, SMath::Vec4(SMath::kUnitWAxis));
				break;
			}
		case 0:
			mSkew = SMath::Mat44(SMath::kIdentity);
			break;
		}
		*pmSkew = mSkew;
	}
}

	}	//namespace Tools
}	//namespace OrbisAnim
