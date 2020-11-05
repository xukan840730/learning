/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-contacts.h"

#define EIGEN_NO_DEBUG 1
#define eigen_assert SYSTEM_ASSERT

#include "corelib/math/matrix3x3.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/netbridge/redisrpc-server.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/dc-types.h"

#include "gamelib/gameplay/effect-group-util.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"
#include "gamelib/script/script-menu.h"

#include <Eigen/LU>
#include <Eigen/QR>
#include <Eigen/SVD>

typedef Eigen::Matrix<float, 4, 4> EMatrix4x4;
typedef Eigen::Matrix<float, 3, 3> EMatrix3x3;
typedef Eigen::Vector3f EVec3;

struct SingleFrameJointSample
{
	Transform m_transforms;
	Locator m_align;
	float m_phase;
	float m_frame;
	float m_sample;
};

static Mat44 Matrix3x3ToMat44(const Matrix3x3& m)
{
	Mat44 m44;
	for (int r = 0; r < 3; r++)
	{
		m44.SetRow(r, Vec4(m.GetRow(r).X(), m.GetRow(r).X(), m.GetRow(r).X(), 0.0f));
	}
	m44.SetRow(3, Vec4(kZero));
	return m44;
}

static Matrix3x3 Mat44ToMatrix3x3(const Mat44& m)
{
	Matrix3x3 t;
	for (int r = 0; r < 3; r++)
	{
		const Vec4 row = m.GetRow(r);
		t.SetRow(r, Vec3(row.X(), row.Y(), row.Z()));
	}
	return t;
}

static void Print(const Mat44& m)
{
	MsgPlayer("[\n");
	for (int r = 0; r < 4; r++)
	{
		for (int c = 0; c < 4; c++)
		{
			MsgPlayer("%f", (float)m.Get(r, c));
			if (c < 3)
				MsgPlayer(", ");
		}
		MsgPlayer(";\n");
	}
	MsgPlayer("]");
}

static EVec3 ToEigen(Vector_arg v)
{
	EVec3 x;
	x[0] = v.X();
	x[1] = v.Y();
	x[2] = v.Z();
	return x;
}

static EVec3 ToEigen(Point_arg v)
{
	EVec3 x;
	x[0] = v.X();
	x[1] = v.Y();
	x[2] = v.Z();
	return x;
}

static void ToEigen(const Mat44& m, EMatrix4x4& em)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			em(r, c) = m.Get(r, c);
		}
}

static Scalar DistSegmentToPlane(const Segment& seg, const Plane p)
{
	const Scalar D0 = p.Dist(seg.a);
	const Scalar D1 = p.Dist(seg.b);
	if (Sign(D0) != Sign(D1))
	{
		return kZero;
	}
	else if (D0 > Scalar(kZero))
	{
		return Min(D0, D1);
	}
	else
		return Max(D0, D1);
}

static Scalar DistBoxToPlane(Aabb_arg box, const Plane p)
{
	const Scalar dist[] = { p.Dist(box.m_min),
							p.Dist(Point(box.m_min.X(), box.m_min.Y(), box.m_max.X())),
							p.Dist(Point(box.m_min.X(), box.m_max.Y(), box.m_max.X())),
							p.Dist(Point(box.m_min.X(), box.m_max.Y(), box.m_min.X())),

							p.Dist(Point(box.m_max.X(), box.m_min.Y(), box.m_max.X())),
							p.Dist(Point(box.m_max.X(), box.m_max.Y(), box.m_max.X())),
							p.Dist(Point(box.m_max.X(), box.m_max.Y(), box.m_min.X())),
							p.Dist(box.m_max) };
	Scalar min = kLargeFloat;
	Scalar max = -kLargeFloat;
	for (const auto& d : dist)
	{
		min = Min(min, d);
		max = Max(max, d);
	}
	if (Sign(min) != Sign(max))
	{
		return kZero;
	}
	else
	{
		return Min(Abs(min), Abs(max));
	}
}

static Scalar ComputeMomentOfInertia(Aabb_arg box, Vector_arg axis)
{
	auto Ix = (Sqr(box.GetSize().Y()) + Sqr(box.GetSize().Z())) / 12.f;
	auto Iy = (Sqr(box.GetSize().X()) + Sqr(box.GetSize().Z())) / 12.f;
	auto Iz = (Sqr(box.GetSize().Y()) + Sqr(box.GetSize().X())) / 12.f;

	auto Ixx = (Iy + Iz - Ix) / 2.f;
	auto Iyy = (Ix + Iz - Iy) / 2.f;
	auto Izz = (Iy + Ix - Iz) / 2.f;

	EMatrix3x3 m = EMatrix3x3::Constant(0.0f);
	m(0, 0) = Ixx;
	m(1, 1) = Iyy;
	m(2, 2) = Izz;
	EVec3 n;
	n[0] = axis.X();
	n[1] = axis.Y();
	n[2] = axis.Z();

	auto Inn = n.transpose() * m * n;
	auto In  = (Ixx + Iyy + Izz) - Inn;
	return In;
}

static Scalar DistBoxToPlane(const OrientedBox& box, const Plane p)
{
	return DistBoxToPlane(box.m_aab,
						  Plane(box.m_loc.UntransformPoint(p.ProjectPoint(box.m_loc.GetTranslation())),
								box.m_loc.UntransformVector(p.GetNormal())));
}

static OrientedBox TransformBox(const OrientedBox& box, const Locator loc)
{
	return OrientedBox{ loc.TransformLocator(box.m_loc), box.m_aab };
}

pair<Segment, float> IntersectLineBox(const Line& l, const Aabb& box)
{
	//	g_prim.Draw(DebugBox(box.m_min, box.m_max, kColorWhite, PrimAttrib(kPrimEnableWireframe)));
	//	g_prim.Draw(DebugLine(l.m_p - l.m_v, l.m_p + l.m_v, kColorBlue));
	Point		 closestPointOnLine;
	const Scalar distToine = DistPointLine(box.GetCenter(), l.m_p, l.m_p + l.m_v, &closestPointOnLine);
	const Scalar radius	= box.ToSphere().W();

	Segment testSeg(closestPointOnLine + SafeNormalize(l.m_v, kZero) * radius * 2.0f,
					closestPointOnLine - SafeNormalize(l.m_v, kZero) * radius * 2.0f);
	if (box.SegmentOverlaps(testSeg.a, testSeg.b))
	{
		Segment result(testSeg);
		box.ClipLineSegment(result.a, result.b);

		Scalar t[2];
		bool isect = box.IntersectSegment(testSeg.a, testSeg.b, t[0], t[1]);

		// 		g_prim.Draw(DebugSphere(Lerp(testSeg.a, testSeg.b, t[0]), 0.02f, kColorOrange));
		// 		g_prim.Draw(DebugSphere(Lerp(testSeg.a, testSeg.b, t[1]), 0.02f, kColorOrange));
		//
		// 		g_prim.Draw(DebugCross(result.a, 0.02f, kColorCyan));
		// 		g_prim.Draw(DebugCross(result.b, 0.02f, kColorCyan));
		make_pair(Segment(Lerp(testSeg.a, testSeg.b, t[0]), Lerp(testSeg.a, testSeg.b, t[1])), 0.0f);
	}

	Point verts[] = {
		Point(box.m_min.X(), box.m_min.Y(), box.m_min.Z()), Point(box.m_min.X(), box.m_min.Y(), box.m_max.Z()),
		Point(box.m_min.X(), box.m_max.Y(), box.m_max.Z()), Point(box.m_min.X(), box.m_max.Y(), box.m_min.Z()),

		Point(box.m_max.X(), box.m_min.Y(), box.m_min.Z()), Point(box.m_max.X(), box.m_min.Y(), box.m_max.Z()),
		Point(box.m_max.X(), box.m_max.Y(), box.m_max.Z()), Point(box.m_max.X(), box.m_max.Y(), box.m_min.Z()),

	};
	Segment segments[] = {
		// Top face
		Segment(verts[0], verts[1]),
		Segment(verts[1], verts[2]),
		Segment(verts[2], verts[3]),
		Segment(verts[3], verts[0]),

		// Bottom face
		Segment(verts[4], verts[5]),
		Segment(verts[5], verts[6]),
		Segment(verts[6], verts[7]),
		Segment(verts[7], verts[4]),

		Segment(verts[0], verts[4]),
		Segment(verts[1], verts[5]),
		Segment(verts[2], verts[6]),
		Segment(verts[3], verts[7]),
	};

	Scalar closestDist = kLargeFloat;
	Point  closestPointOnBox;
	for (const auto& s : segments)
	{
		Point		 sPoint;
		Point		 testPoint;
		const Scalar dist = DistSegmentSegment(s, testSeg, sPoint, testPoint);
		//		g_prim.Draw(DebugCross(sPoint, 0.02f, kColorYellow));
		//		g_prim.Draw(DebugCross(testPoint, 0.02f, kColorRed));
		if (dist < closestDist)
		{
			closestPointOnBox = sPoint;
			closestDist		  = dist;
		}
	}
	return make_pair(Segment(closestPointOnBox, closestPointOnBox), closestDist);
}

static void DrawBody(const OrientedBox& jointBody, const Transform& xform, Color drawColor)
{
	g_prim.Draw(DebugBox(xform * jointBody.m_loc.AsTransform(), jointBody.m_aab.m_min, jointBody.m_aab.m_max, drawColor, PrimAttrib(kPrimEnableWireframe)));
}

static Point ComputeClosestPoint(Aabb_arg box, Point_arg pos)
{
	return Max(Min(pos, box.m_max), box.m_min);
}

Scalar ComputeRotationalEnergy(const Aabb& box, const Locator& bodyLoc0, const Locator& bodyLoc1, const float dt)
{
	Quat		 deltaRot = bodyLoc0.UntransformLocator(bodyLoc1).GetRotation();
	const Vector axis	 = SafeNormalize(Imaginary(deltaRot), kZero);
	if (LengthSqr(axis) > 0.f)
	{
		auto I		 = ComputeMomentOfInertia(box, bodyLoc1.TransformVector(axis));
		auto rot_vel = Acos(MinMax(deltaRot.W(), Scalar(-1.0f), Scalar(1.0f))) * 2.0f / dt;
		return Sqr(rot_vel) * I;
	}
	return kZero;
}

static ContactInfo ComputeContact(const Transform&   w0Param,
								  const Transform&   w1Param,
								  Color				 drawColor,
								  const OrientedBox& jointBody,
								  const float		 dt,
								  const Plane		 ground,
								  const bool		 draw)
{
	if (draw)
	{
		g_prim.Draw(DebugCoordAxes(w0Param));
		g_prim.Draw(DebugCoordAxes(w1Param));

		DrawBody(jointBody, w1Param, kColorYellow);
	}

	float distToGround = DistBoxToPlane(TransformBox(jointBody, Locator(w1Param)), ground);

	const Point com1 = jointBody.m_aab.GetCenter() * w1Param * jointBody.m_loc.AsTransform();
	const Point com0 = jointBody.m_aab.GetCenter() * w0Param * jointBody.m_loc.AsTransform();

	const float linearKineticEnergy	= 0.5f * LengthSqr((com1 - com0) / dt);
	const float potentialKineticEnergy = com1.Y() * 9.8f;

	const float rotationalEnergy = ComputeRotationalEnergy(jointBody.m_aab,
														   Locator(w0Param * jointBody.m_loc.AsTransform()),
														   Locator(w1Param * jointBody.m_loc.AsTransform()),
														   dt);
	const float totalEnergy = linearKineticEnergy + potentialKineticEnergy + rotationalEnergy;

	EMatrix4x4 w0, w1;

	ToEigen(Transpose(w0Param.GetMat44()), w0);
	ToEigen(Transpose(w1Param.GetMat44()), w1);

	EMatrix4x4 T = w1 * w0.inverse();
	EMatrix3x3 t = T.block<3, 3>(0, 0) - EMatrix3x3::Identity();
	EVec3	  b = -T.block<3, 1>(0, 3);

	Eigen::JacobiSVD<EMatrix3x3> svd(t, Eigen::ComputeFullV | Eigen::ComputeFullU);
	EVec3						 x = svd.solve(b);

	if (draw)
	{
		MsgCon("Frame %3d: %6.6f %6.6f %6.6f\n", 0, svd.singularValues()[0], svd.singularValues()[1], svd.singularValues()[2]);
	}
	float		 totalS	= svd.singularValues().sum();
	EVec3		 nS		   = svd.singularValues() / totalS;
	static float threshold = 0.1f;

	// 	EMatrix4x4					 TminusI = T - EMatrix4x4::Identity();
	// 	Eigen::JacobiSVD<EMatrix4x4> svdT(T, Eigen::ComputeFullV | Eigen::ComputeFullU);
	// 	Eigen::FullPivLU<EMatrix4x4> lu_decomp44(TminusI);
	// 	if (draw)
	// 	{
	// 		MsgCon("Rank4x4: %d\n", lu_decomp44.rank());
	// 	}

	Eigen::FullPivLU<EMatrix3x3> lu_decomp(t);
	lu_decomp.setThreshold(0.001f);
	if (draw)
	{
		MsgCon("Rank3x3: %d\n", lu_decomp.rank());
	}

	Point result(x(0), x(1), x(2));

	if (nS(2) < threshold)
	{
		if (draw)
		{
			MsgCon("Naive Rank: 2\n");
		}
		EVec3  linVec = svd.matrixV().col(2);
		Vector linv(linVec(0), linVec(1), linVec(2));

		// Find point on the line closest to the joint;
		Point jointPos = w0Param.GetTranslation();

		Point closestPtToJoint = Dot((jointPos - result), linv) * linv + result;

		// DebugDrawCross(result, 0.2f, kColorRed);

		// DebugDrawLine(closestPtToJoint - linv*10.f, closestPtToJoint + linv*10.0f, drawColor);

		auto line = Line{ closestPtToJoint, linv };

		float distToJoint = DistPointLine(w1Param.GetTranslation(), line.m_p, line.m_p + line.m_v);
		float distToBody  = distToJoint;
		{
			{
				auto totalXform = w1Param * jointBody.m_loc.AsTransform();
				auto resultSegDistPair
					= IntersectLineBox(Line{ line.m_p * Inverse(totalXform), line.m_v * Inverse(totalXform) }, jointBody.m_aab);
				distToBody = resultSegDistPair.second;
				Segment wsSeg(resultSegDistPair.first.a * totalXform, resultSegDistPair.first.b * totalXform);
				auto	totalXform2 = w0Param * jointBody.m_loc.AsTransform();
				Segment wsSeg2(resultSegDistPair.first.a * totalXform2, resultSegDistPair.first.b * totalXform2);
				if (draw)
				{
					MsgCon("Dist contact to body:%f\n", resultSegDistPair.second);
					g_prim.Draw(DebugLine(wsSeg.a, wsSeg.b, kColorMagenta));
					g_prim.Draw(DebugSphere(wsSeg.a, 0.02f, kColorMagenta));
					g_prim.Draw(DebugSphere(wsSeg.b, 0.02f, kColorMagenta));					

					g_prim.Draw(DebugLine(wsSeg.a, wsSeg.b, kColorCyan));
					g_prim.Draw(DebugSphere(wsSeg.a, 0.01f, kColorCyan));
					g_prim.Draw(DebugSphere(wsSeg.b, 0.01f, kColorCyan));

					DrawBody(jointBody, w0Param, kColorOrange);
				}
				Point		 sPoint[2];				
				const float closePointDist = DistSegmentSegment(wsSeg, wsSeg2, sPoint[0], sPoint[1]);
				
				return ContactInfo{ line, distToJoint, distToBody, wsSeg, totalEnergy, distToGround, closePointDist };
			}
		}
	}
	else
	{
		if (draw)
		{
			DebugDrawCross(result, 0.2f, drawColor);
		}
		auto  totalXform = w1Param * jointBody.m_loc.AsTransform();
		float distToJoint = Dist(result, w1Param.GetTranslation());
		Point closestPtWs = ComputeClosestPoint(jointBody.m_aab, result*Inverse(totalXform))*totalXform;
		float distToBody  = Dist(closestPtWs, result);		
		distToBody		  = Sqrt(jointBody.m_aab.SquareDistanceToPoint(result * Inverse(totalXform)));
		
		float closePointDist = Dist(closestPtWs*Inverse(w1Param)*w0Param, closestPtWs);
		
		return ContactInfo{ Line{ result, Vector(kZero) }, distToJoint, distToBody,
			Segment(closestPtWs, closestPtWs), totalEnergy, distToGround, closePointDist };
	}
}

//--------------------------------------------------------------------------------------------------------------------
static void SampleJointDelta(const ArtItemAnim* pAnim,
							 const ArtItemSkeleton* pArtSkeleton,
							 StringId64 worldGroundApRef,
							 const Locator firstFrameAlignLoc,
							 const U32* jointIndices,
							 const I32 numJoints,
							 ListArray<SingleFrameJointSample>& samples)
{
	const bool bLooping = pAnim->m_flags & ArtItemAnim::kLooping;

	// pre-cache all joint transforms.
	for (U32F iSample = 0; iSample < samples.Size(); iSample++)
	{
		Locator align;
		float   phase = (float)iSample / Max(static_cast<float>(samples.Size() - 1), 1.0f);

		bool valid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), phase, &align);
		ANIM_ASSERT(valid);
		align = firstFrameAlignLoc.TransformLocator(align);

		SingleFrameJointSample& sample = samples[iSample];

		const float frameRateScale = pAnim->m_pClipData->m_framesPerSecond / 30.f;
		const float frameSample = iSample * frameRateScale;

		sample.m_align = align;
		sample.m_frame = iSample;
		sample.m_phase = phase;
		sample.m_sample = frameSample;

		// g_prim.Draw(DebugCoordAxes(align));
		AnimateJoints(Transform(align.GetRotation(), align.GetTranslation()),
					  pArtSkeleton,
					  pAnim,
					  frameSample,
					  jointIndices,
					  numJoints,
					  &sample.m_transforms,
					  nullptr,
					  nullptr,
					  nullptr);
	}
}

void GetContactsFromAnim(const ArtItemAnim* pAnim,
						 const StringId64 jointId,
						 const Locator firstFrameAlignLoc,
						 int frameIndex,
						 const OrientedBox& jointBody,
						 ListArray<Maybe<ContactInfo>>& outContacts,
						 ListArray<Locator>* pOutTransforms)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);
	outContacts.Clear();
	if (pOutTransforms)
	{
		pOutTransforms->Clear();
	}
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();
	const bool			   bLooping = pAnim->m_flags & ArtItemAnim::kLooping;

	const U32F numTotalFrames = (pAnim->m_pClipData->m_numTotalFrames - 1) * 30.f * pAnim->m_pClipData->m_secondsPerFrame;
	if (numTotalFrames <= 0)
		return;

	MsgCon("Draw Contacts for joints '%s' on frame %d of %d\n", DevKitOnly_StringIdToString(jointId), frameIndex, numTotalFrames);

	const U32F numTotalJoints = pArtSkeleton->m_numTotalJoints;
	const I32F jointIndex = FindJoint(pArtSkeleton->m_pJointDescs, numTotalJoints, jointId);

	if (jointIndex < 0)
		return;

	static const I32F kMaxNumJoints = 1;

	const U32 jointIndices[kMaxNumJoints] = { static_cast<U32>(jointIndex) };
	const I32 numJoints = sizeof(jointIndices) / sizeof(jointIndices[0]);
	ANIM_ASSERT(numJoints == kMaxNumJoints);

	const int numSamples = numTotalFrames + 1;
	ListArray<SingleFrameJointSample> samples;
	{
		AllocateJanitor debug(kAllocDebug, FILE_LINE_FUNC);
		samples.Init(numSamples, FILE_LINE_FUNC);
		samples.Resize(numSamples);
	}	
	
	SampleJointDelta(pAnim, pArtSkeleton, SID("align"), firstFrameAlignLoc, jointIndices, numJoints, samples);

	if (pOutTransforms)
	{
		ANIM_ASSERT(pOutTransforms->Capacity() >= samples.Size());
		pOutTransforms->Resize(samples.Size());
		for (int i = 0; i < samples.Size(); i++)
		{
			pOutTransforms->At(i) = Locator(samples[i].m_transforms);
		}
	}
	ANIM_ASSERT(outContacts.Capacity() >= samples.Size());
	
	Color		colors[] = { kColorGreen, kColorRed, kColorBlue };
	const float dt		 = 1.0f / 30.f;
	Plane		ground(kOrigin, kUnitYAxis);
	for (int i = 0; i < samples.Size(); i++)
	{
		if (i < 1)
		{
			outContacts.PushBack(MAYBE::kNothing);
		}
		else
		{
			int colorIndex = ((frameIndex - i) + 1) % ARRAY_COUNT(colors);
			if (Abs(frameIndex - i) < 2)
			{
				auto contact = ComputeContact(samples[i - 1].m_transforms,
											  samples[i].m_transforms,
											  colors[colorIndex],
											  jointBody,
											  dt,
											  ground,
											  false);
				outContacts.PushBack(contact);
			}
			else
			{
				auto contact = ComputeContact(samples[i - 1].m_transforms,
											  samples[i].m_transforms,
											  colors[colorIndex],
											  jointBody,
											  dt,
											  ground,
											  false);
				outContacts.PushBack(contact);
			}
		}
	}

	if (0)
	{
		int i = 0;

		for (const auto& mc : outContacts)
		{
			if (mc.Valid())
			{
				const ContactInfo& c = mc.Get();
				if (c.m_distContactToBody < 0.06f && c.m_bodyDistToGround < 0.02f)
				{
					MsgCon("Frame %4d: %.4f %.4f  E:%f\n", i, c.m_distContactToBody, c.m_bodyDistToGround, c.m_bodyEnergy);
					DrawBody(jointBody, samples[i].m_transforms, kColorGreen);
					g_prim.Draw(DebugLine(c.m_contactOnBody.a, c.m_contactOnBody.b, kColorMagenta));
					g_prim.Draw(DebugCross(c.m_contactOnBody.Interpolate(0.5f), 0.02f, kColorMagenta));
					if (i > 0 && outContacts[i - 1].Valid())
					{
						g_prim.Draw(DebugLine(c.m_contactOnBody.Interpolate(0.5f),
											  outContacts[i - 1].Get().m_contactOnBody.Interpolate(0.5f),
											  kColorMagenta));
					}
				}
				if (i == frameIndex)
				{
					MsgCon("***Frame %4d: %f %f  E:%f\n", i, c.m_distContactToBody, c.m_bodyDistToGround, c.m_bodyEnergy);
					DrawBody(jointBody, samples[i].m_transforms, kColorYellow);
					g_prim.Draw(DebugLine(c.m_contactOnBody.a, c.m_contactOnBody.b, kColorRed));
					g_prim.Draw(DebugCross(c.m_contactOnBody.Interpolate(0.5f), 0.04f, kColorRed));
				}
			}
			i++;
		}
	}

	if (0)
	{
		EMatrix3x3 A = EMatrix3x3::Constant(0.0f);
		EVec3	  b = EVec3::Constant(0.0f);
		for (const auto& lineTup : outContacts)
		{
			if (lineTup.Valid())
			{
				const auto& line = lineTup.Get().m_contact;
				auto		x	= ToEigen(line.m_v);
				EMatrix3x3  m	= x * x.transpose() - EMatrix3x3::Identity();
				A += m;
				b += m * ToEigen(line.m_p);
			}
		}
		EVec3 x	= A.jacobiSvd(Eigen::ComputeFullV | Eigen::ComputeFullU).solve(b);
		float dist = 0.0f;
		for (const auto& lineTup : outContacts)
		{
			if (lineTup.Valid())
			{
				const auto& line = lineTup.Get().m_contact;
				dist += DistPointLine(Point(x[0], x[1], x[2]), line.m_p, line.m_p + line.m_v);
			}
		}
		dist /= outContacts.Size();
		MsgCon("Mean isect dist: %f\n", dist);
		g_prim.Draw(DebugCross(Point(x[0], x[1], x[2]), 0.5f, dist < 0.05f ? kColorMagenta : kColorWhite));
	}

	if (0)
	{
		MsgOut("Joint %s:\n", DevKitOnly_StringIdToString(jointId));
		MsgOut("Dist to joint:\n [");
		for (int i = 0; i < outContacts.Size(); ++i)
		{
			if (i != 0)
			{
				MsgOut(",");
			}
			if (outContacts[i].Valid())
			{
				MsgOut("%f", outContacts[i].Get().m_distContactToJoint);
			}
		}
		MsgOut("]\n");
		MsgOut("Dist to body:\n [");
		for (int i = 0; i < outContacts.Size(); ++i)
		{
			if (i != 0)
			{
				MsgOut(",");
			}
			if (outContacts[i].Valid())
			{
				MsgOut("%f", outContacts[i].Get().m_distContactToBody);
			}
		}
		MsgOut("]\n");
	}

	{
		AllocateJanitor debug(kAllocDebug, FILE_LINE_FUNC);
		samples.Reset();
	}
}

enum EffGen
{
	kRAnkle,
	kRBall,
	kLAnkle,
	kLBall,
	kEffJointCount
};

static const StringId64 s_lAnkle = SID("l_ankle");
static const StringId64 s_rAnkle = SID("r_ankle");
static const StringId64 s_lHeel  = SID("l_heel");
static const StringId64 s_rHeel  = SID("r_heel");
static const StringId64 s_lBall  = SID("l_ball");
static const StringId64 s_rBall  = SID("r_ball");

JointEffGenerator::JointEffGenerator(ArtItemAnimHandle anim, const StringId64 jointId, const OrientedBox& jointBody)
	: m_jointBody(jointBody), m_animHandle(anim), m_jointId(jointId)
{
	ANIM_ASSERT(anim.ToArtItem() != nullptr);
	const U32F numTotalFrames = anim.ToArtItem()->m_pClipData->m_numTotalFrames * 30.f / anim.ToArtItem()->m_pClipData->m_framesPerSecond;
	m_contacts.Init(numTotalFrames, FILE_LINE_FUNC);
	m_jointTransformsWs.Init(numTotalFrames, FILE_LINE_FUNC);

	GetContactsFromAnim(anim.ToArtItem(), jointId, Locator(kIdentity), 0, m_jointBody, m_contacts, &m_jointTransformsWs);
}

JointEffGenerator::JointEffGenerator(ArtItemAnimHandle anim, const DC::EffJointGenerator& settings, const DC::EffThresholds& thresholds)
	: m_settings(settings)
	, m_animHandle(anim)
	, m_jointId(settings.m_jointId)
	, m_distToGroundThresh(thresholds.m_distToGround)
	, m_distToBodyThresh(thresholds.m_distToBody)
	, m_energyThresh(thresholds.m_energy)
	, m_closestPtDistThresh(thresholds.m_closestPtDist)
	, m_windowFilterSize(thresholds.m_windowFilter)
{
	m_jointBody = OrientedBox{ settings.m_body.m_offset,
							   Aabb(Min(settings.m_body.m_minPoint, settings.m_body.m_maxPoint),
									Max(settings.m_body.m_minPoint, settings.m_body.m_maxPoint)) };

	ANIM_ASSERT(anim.ToArtItem() != nullptr);
	const U32F numTotalFrames = anim.ToArtItem()->m_pClipData->m_numTotalFrames * 30.f / anim.ToArtItem()->m_pClipData->m_framesPerSecond;
	m_contacts.Init(numTotalFrames, FILE_LINE_FUNC);
	m_jointTransformsWs.Init(numTotalFrames, FILE_LINE_FUNC);

	GetContactsFromAnim(anim.ToArtItem(), m_jointId, Locator(kIdentity), 0, m_jointBody, m_contacts, &m_jointTransformsWs);
}

JointEffGenerator::~JointEffGenerator()
{
	m_contacts.Reset();
	m_jointTransformsWs.Reset();
}

void JointEffGenerator::Draw(const Locator firstFrameAlignLoc, int frameIndex) const
{
	int i = 0;

	auto contactState = GetContactStates();
	for (const auto& mc : m_contacts)
	{
		if (mc.Valid())
		{
			const ContactInfo& c = mc.Get();
			if (i == frameIndex)
			{
				MsgCon("***Joint %8s, Frame: %4d Dist To Contact: %8.3f Dist To Ground: %8.3f  Energy:%8.3f Closest Pt Dist: %8.3f %s\n",
					   DevKitOnly_StringIdToString(m_jointId),
					   i,
					   c.m_distContactToBody,
					   c.m_bodyDistToGround,
					   c.m_bodyEnergy,
					   c.m_closestPointDist,
					   contactState.IsBitSet(i) ? "Contact" : "No Contact");
								
				DrawBody(m_jointBody, (firstFrameAlignLoc.TransformLocator(m_jointTransformsWs[i])).AsTransform(), kColorYellow);
				g_prim.Draw(DebugLine(firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.a),
									  firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.b),
									  kColorRed));
				g_prim.Draw(DebugCross(firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.Interpolate(0.5f)), 0.04f, kColorRed));
			}
			if (contactState.IsBitSet(i))
			{
				Color color = kColorOrangeTrans;
				// 				MsgCon("Frame %4d: %.4f %.4f  E:%f\n", i, c.m_distContactToBody, c.m_bodyDistToGround,
				// 					c.m_bodyEnergy);
				if (i > 0 && !contactState.IsBitSet(i - 1) && m_contacts[i - 1].Valid())
				{
					color = kColorRed;
				}
				else
				{
					i++;
					continue;
				}
				DrawBody(m_jointBody, firstFrameAlignLoc.TransformLocator(m_jointTransformsWs[i]).AsTransform(), color);
				g_prim.Draw(DebugLine(firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.a),
									  firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.b),
									  kColorMagenta));
				g_prim.Draw(DebugCross(firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.Interpolate(0.5f)), 0.02f, kColorMagenta));
				if (i > 0 && m_contacts[i - 1].Valid())
				{
					g_prim.Draw(DebugLine(firstFrameAlignLoc.TransformPoint(c.m_contactOnBody.Interpolate(0.5f)),
										  firstFrameAlignLoc.TransformPoint(m_contacts[i - 1].Get().m_contactOnBody.Interpolate(0.5f)),
										  kColorMagenta));
				}
			}
			else
			{
				//MsgCon("Frame %4d: %.4f %.4f  E:%f\n", i, c.m_distContactToBody, c.m_bodyDistToGround, c.m_bodyEnergy);
			}
		}
		i++;
	}
}

void JointEffGenerator::GenerateEFFs(const StringId64 jointId,
									 const StringId64 effectName,
									 ListArray<EffectAnimEntry>& outEffList) const
{
	auto contactState = GetContactStates();
	// TODO Support looping
	for (int i = 1; i < m_contacts.Size(); ++i)
	{
		if (contactState.IsBitSet(i) && !contactState.IsBitSet(i - 1) && m_contacts[i - 1].Valid())
		{
			if (!outEffList.IsFull())
			{
				// The effs generated should look like they were defined as follows
				// foot-effect { frame "22.0"                  name "heel"                         joint "r_heel"  vol
				// "-06.0" }
				// foot-effect { frame "8.0"                   name "toe"                          joint "l_ball"  vol
				// "-06.0" }
				int					numTags = 3;
				EffectAnimEntryTag* pTags   = NDI_NEW EffectAnimEntryTag[numTags];
				pTags[0].Construct(SID("name"), effectName);
				pTags[1].Construct(SID("joint"), jointId);
				pTags[2].Construct(SID("vol"), -6.0f);

				// End frame can be useful for debugging
				// 				int endFrame = i;
				// 				while (contactState[endFrame] && (endFrame + 1) < m_contacts.Size())
				// 				{
				// 					endFrame++;
				// 				}
				// 				pTags[3].Construct(SID("frame-end"), static_cast<float>(endFrame));
				outEffList.PushBack(EffectAnimEntry(SID("foot-effect"), i, pTags, numTags));
			}
		}
	}
}

ArtItemAnimHandle JointEffGenerator::Anim() const
{
	return m_animHandle;
}

bool JointEffGenerator::SetGroundDistThreshold(const float groundThresh)
{

	if (groundThresh != m_distToGroundThresh)
	{
		m_distToGroundThresh = groundThresh;
		return true;
	}
	return false;
}

bool JointEffGenerator::SetContactDistThreshold(const float contactThresh)
{

	if (contactThresh != m_distToBodyThresh)
	{
		m_distToBodyThresh = contactThresh;
		return true;
	}
	return false;
}

bool JointEffGenerator::SetWindowFilter(const int w)
{
	if (m_windowFilterSize != w)
	{
		m_windowFilterSize = w;
		return true;
	}
	return false;
}

bool JointEffGenerator::IsInContact(const ContactInfo& c) const
{
	return (c.m_distContactToBody < m_distToBodyThresh || c.m_bodyEnergy < m_energyThresh)
		   && c.m_closestPointDist < m_closestPtDistThresh
		   && c.m_bodyDistToGround < m_distToGroundThresh;
}

static JointEffGenerator::BoolList Shift(const JointEffGenerator::BoolList& src, int shift)
{
	if (shift == 0)
		return src;
	JointEffGenerator::BoolList result;
	result.ClearAllBits();
	if (shift < 0)
	{
		JointEffGenerator::BoolList::ShortShiftLeft(&result, src, -shift);
	}
	else
	{
		JointEffGenerator::BoolList::ShortShiftRight(&result, src, shift);
	}
	return result;
}

static JointEffGenerator::BoolList Dilate(const JointEffGenerator::BoolList& src, int width)
{
	JointEffGenerator::BoolList result;
	result = src;
	for (int s = -width; s <= width; s++)
	{
		if (s != 0)
		{
			JointEffGenerator::BoolList::BitwiseOr(&result, result, Shift(src, s));
		}
	}

	return result;
}

static JointEffGenerator::BoolList Erode(const JointEffGenerator::BoolList& src, int width)
{
	JointEffGenerator::BoolList result;
	result = src;
	for (int s = -width; s <= width; s++)
	{
		if (s != 0)
		{
			JointEffGenerator::BoolList::BitwiseAnd(&result, result, Shift(src, s));
		}
	}
	return result;
}

static JointEffGenerator::BoolList Close(const JointEffGenerator::BoolList& src, int width)
{
	return Erode(Dilate(src, width), width);
}

JointEffGenerator::BoolList JointEffGenerator::GetContactStates() const
{
	JointEffGenerator::BoolList inContact;
	inContact.ClearAllBits();

	ANIM_ASSERT(m_contacts.Size() < inContact.GetMaxBitCount());
	for (int i = 0; i < m_contacts.Size(); ++i)
	{
		if (m_contacts[i].Valid())
		{
			if (IsInContact(m_contacts[i].Get()))
			{
				inContact.SetBit(i);
			}
		}
	}
	JointEffGenerator::BoolList inContactFiltered = Close(inContact, m_windowFilterSize);
	return inContactFiltered;
}

EffGenerator::EffGenerator(ArtItemAnimHandle anim)
{
	const DC::EffGeneratorSet* pGeneratorSet = LookupSettings(anim.ToArtItem());
	
	ANIM_ASSERT(pGeneratorSet);
	if (pGeneratorSet)
	{
		InitFromDc(anim, pGeneratorSet);
	}
	else
	{
		auto rAnkleBody = OrientedBox{ Locator(kIdentity), Aabb(Point(-0.04f, -0.05f, -0.08f), Point(0.06f, 0.12f, 0.0f)) };
		auto lAnkleBody
			= OrientedBox{ Locator(kIdentity),
			Aabb(Point(rAnkleBody.m_aab.m_min.X(), -rAnkleBody.m_aab.m_max.Y(), -rAnkleBody.m_aab.m_max.Z()),
			Point(rAnkleBody.m_aab.m_max.X(), -rAnkleBody.m_aab.m_min.Y(), -rAnkleBody.m_aab.m_min.Z())) };
		auto rToeBody = OrientedBox{ Locator(kIdentity), Aabb(Point(-0.04f, -0.0175f, -0.08f), Point(0.06f, 0.05f, 0.00f)) };
		auto lToeBody = OrientedBox{ Locator(kIdentity),
			Aabb(Point(rToeBody.m_aab.m_min.X(), -rToeBody.m_aab.m_max.Y(), -rToeBody.m_aab.m_max.Z()),
			Point(rToeBody.m_aab.m_max.X(), -rToeBody.m_aab.m_min.Y(), -rToeBody.m_aab.m_min.Z())) };

		std::tuple<EffGen, StringId64, OrientedBox, StringId64, StringId64> params[] = {
			std::make_tuple(kRAnkle, s_rAnkle, rAnkleBody, s_rHeel, SID("heel")),
			std::make_tuple(kRBall, s_rBall, rToeBody, s_rBall, SID("toe")),
			std::make_tuple(kLAnkle, s_lAnkle, lAnkleBody, s_lHeel, SID("heel")),
			std::make_tuple(kLBall, s_lBall, lToeBody, s_lBall, SID("toe")),
		};

		for (const auto& param : params)
		{
			EffGen		index;
			StringId64  jointId;
			OrientedBox body;
			StringId64  effectJointId;
			StringId64  effectId;
			std::tie(index, jointId, body, effectJointId, effectId) = param;
			m_apJoints[index] = NDI_NEW JointEffGenerator(anim, jointId, body);
		}
	}
}

EffGenerator::~EffGenerator()
{
	for (auto& pJoint : m_apJoints)
	{
		NDI_DELETE pJoint;
		pJoint = nullptr;
	}
}

ArtItemAnimHandle EffGenerator::Anim() const
{
	return m_apJoints[0]->Anim();
}


void EffGenerator::Update()
{
	if (const DC::EffGeneratorSet* pSettings = LookupSettings(Anim().ToArtItem()))
	{		
		if (memcmp(pSettings, &m_dcSettings, sizeof(m_dcSettings)) != 0)
		{
			InitFromDc(Anim(), pSettings);
		}
	}
}

void EffGenerator::Draw(const Locator firstFrameAlignLoc, int frameIndex) const
{
	for (const auto& pJoint : m_apJoints)
	{
		pJoint->Draw(firstFrameAlignLoc, frameIndex);
	}
}

void EffGenerator::GenerateDebug() const
{
	AllocateJanitor alloc(kAllocDebug, FILE_LINE_FUNC);

	DebugEffectFile* pFile   = NDI_NEW DebugEffectFile;
	pFile->m_anims			 = NDI_NEW		   EffectAnim;
	pFile->m_numAnims		 = 1;
	pFile->m_anims->m_nameId = Anim().ToArtItem()->GetNameId();

	const int		 allocEffects	 = 128;
	EffectAnimEntry* paEffectsEntries = NDI_NEW EffectAnimEntry[allocEffects];

	ListArray<EffectAnimEntry> effectList(allocEffects, paEffectsEntries);

	std::tuple<EffGen, StringId64, StringId64> params[] = {
		std::make_tuple(kRAnkle, s_rHeel, SID("heel")),
		std::make_tuple(kRBall, s_rBall, SID("toe")),
		std::make_tuple(kLAnkle, s_lHeel, SID("heel")),
		std::make_tuple(kLBall, s_lBall, SID("toe")),
	};

	for (const auto& param : params)
	{
		EffGen	 index;
		StringId64 effectJointId;
		StringId64 effectId;
		std::tie(index, effectJointId, effectId) = param;

		m_apJoints[index]->GenerateEFFs(effectJointId, effectId, effectList);
	}

	pFile->m_anims->m_pEffects   = paEffectsEntries;
	pFile->m_anims->m_numEffects = effectList.Size();

	ResourceTable::DevKitOnly_UpdateEffectAnimPointersForSkel(Anim().ToArtItem()->m_skelID, pFile);
}

static void PrintEff(FILE* pFile, const EffectAnimEntry& eff)
{
	fprintf(pFile, "    %s { ", DevKitOnly_StringIdToString(eff.GetNameId()));
	fprintf(pFile, " frame \"%f\" ", eff.GetFrame());
	for (int i = 0; i < eff.GetNumTags();  ++i)
	{
		if (auto pTag = eff.GetTagByIndex(i))
		{
			switch (pTag->GetNameId().GetValue())
			{
			case SID_VAL("name"):
			case SID_VAL("joint"):
				fprintf(pFile, " %s \"%s\" ", DevKitOnly_StringIdToString(pTag->GetNameId()), DevKitOnly_StringIdToString(pTag->GetValueAsStringId()));
				break;
			case SID_VAL("vol"):
				fprintf(pFile, " %s \"%f\" ", DevKitOnly_StringIdToString(pTag->GetNameId()), pTag->GetValueAsF32());
				break;
			default:
				fprintf(pFile, " %s \"%f\" ", DevKitOnly_StringIdToString(pTag->GetNameId()), pTag->GetValueAsU32());
			}
		}
	}
	fprintf(pFile, " }\n");
}

void EffGenerator::GenerateTxt(FILE* pFile) const
{
	MsgOut("Generating Effs for anim: %s\n", Anim().ToArtItem()->GetName());

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	std::tuple<EffGen, StringId64, StringId64> params[] = {
		std::make_tuple(kRAnkle, s_rHeel, SID("heel")),
		std::make_tuple(kRBall, s_rBall, SID("toe")),
		std::make_tuple(kLAnkle, s_lHeel, SID("heel")),
		std::make_tuple(kLBall, s_lBall, SID("toe")),
	};

	const int		 allocEffects = 128;
	EffectAnimEntry* paEffectsEntries = NDI_NEW EffectAnimEntry[allocEffects];

	ListArray<EffectAnimEntry> effectList(allocEffects, paEffectsEntries);

	for (const auto& param : params)
	{
		EffGen	 index;
		StringId64 effectJointId;
		StringId64 effectId;
		std::tie(index, effectJointId, effectId) = param;

		m_apJoints[index]->GenerateEFFs(effectJointId, effectId, effectList);
	}

	QuickSort(effectList.Begin(), effectList.Size(), [](const EffectAnimEntry& a, const EffectAnimEntry& b) -> int{
		return a.GetFrame() - b.GetFrame();
	});

	fprintf(pFile, "\nanim \"%s\"\n", Anim().ToArtItem()->GetName());
	fprintf(pFile, "{\n");
	for (const auto& entry : effectList)
	{
		PrintEff(pFile, entry);
	}
	fprintf(pFile, "}\n");
}

static bool AnimHasJoint(const ArtItemAnim* pAnim, StringId64 jointId)
{
	if (pAnim->m_flags & ArtItemAnim::kAdditive) return false;

	auto pSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();
	if (!pSkeleton)
		return false;

	I32 jointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, jointId);
	if (jointIndex >= 128)
		return false;
	const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pAnim->m_pClipData, 0);
	return pValidBits->IsBitSet(jointIndex);
}

bool EffGenerator::CanGenerateForAnim(const ArtItemAnim* pAnim)
{
	return AnimHasJoint(pAnim, s_lAnkle) && AnimHasJoint(pAnim, s_rAnkle) && AnimHasJoint(pAnim, s_lBall)
		   && AnimHasJoint(pAnim, s_rBall);
}

static void GenerateEffsForAnim(FILE* pFile, ArtItemAnimHandle anim)
{
	if (EffGenerator::CanGenerateForAnim(anim.ToArtItem()))
	{
		AllocateJanitor alloc(kAllocDebug, FILE_LINE_FUNC);
		EffGenerator generator(anim);
		generator.GenerateTxt(pFile);
	}
}

bool EffGenerator::GenerateEffFileForActor(const char* pActorName, const char* pSkelActor)
{
	StringBuilder<256> actorName("%s-auto.eff", pActorName);

	auto pSkeleton = ResourceTable::LookupSkel(StringToStringId64(pSkelActor)).ToArtItem();
	if (!pSkeleton)
		return false;

	const Level* pActorLevel = EngineComponents::GetLevelMgr()->GetActor(StringToStringId64(pActorName));
	if (!pActorLevel)
		return false;

	Err dirResult = EngineComponents::GetFileSystem()->CreateDirectorySync(StringBuilder<512>("%s%s/eff-auto", FILE_SYSTEM_ROOT, EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir).c_str());	
	if (!dirResult.Succeeded())
	{
		MsgErr("Error making dir: '%s'\n", dirResult.GetMessageText());
		return false;
	}
	dirResult = EngineComponents::GetFileSystem()->CreateDirectorySync(StringBuilder<512>("%s%s/eff-auto/%s", FILE_SYSTEM_ROOT, EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir, pSkelActor).c_str());
	if (!dirResult.Succeeded())
	{
		MsgErr("Error making dir: '%s'\n", dirResult.GetMessageText());
		return false;
	}
	auto pFile = fopen(StringBuilder<512>("%s%s/eff-auto/%s/%s", FILE_SYSTEM_ROOT, EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir, pSkelActor, actorName.c_str()).c_str(), "w");
	if (!pFile)
		return false;

// 	const Packages& packages = pActorLevel->GetPackages();
// 	for (Packages::const_iterator jt = packages.begin(); jt != packages.end(); ++jt)
// 	{
// 		Package* pPackage = const_cast<Package*>(*jt);
// 		const ResItem* pItem;
// 		for (pItem = pPackage->GetNextItemByTypeId(SID("ANIM"), nullptr); pItem != nullptr; pItem = pPackage->GetNextItemByTypeId(SID("ANIM"), pItem))
// 		{
// 			auto pAnim = static_cast<const ArtItemAnim*>(pItem);
// 			if (pAnim->m_flags & ArtItemAnim::kAdditive)
// 			{
// 				continue;
// 			}
// 			const ArtItemSkeleton* pAnimSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();
// 			if (pAnimSkeleton == pSkeleton)
// 			{
// 				GenerateEffsForAnim(pFile, anim);
// 			}
// 		}
// 	}

	fclose(pFile);
	return true;
}


DMENU::Menu* EffGenerator::CreateMenu()
{
	static ScriptPointer<DC::Map> s_skelToSettingsMap(SID("*eff-skel-generator-map*"));
	const DC::Map* pMap = s_skelToSettingsMap;
	auto pMenu = NDI_NEW DMENU::Menu("Eff Generator");
	for (int i = 0; i < pMap->m_count; ++i)
	{
		auto pSubMenu = ScriptMenu::MakeDCMenu(DevKitOnly_StringIdToString(pMap->m_keys[i]), const_cast<void*>(pMap->m_data[i].m_ptr));
		pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu(DevKitOnly_StringIdToString(pMap->m_keys[i]), pSubMenu));
	}	
	return pMenu;
}

const DC::EffGeneratorSet* EffGenerator::LookupSettings(const ArtItemAnim* pAnim)
{
	static ScriptPointer<DC::Map> s_skelToSettingsMap(SID("*eff-skel-generator-map*"));

	const DC::EffGeneratorSet* pGeneratorSet = nullptr;
	if (s_skelToSettingsMap.Valid())
	{
		const StringId64 skelActorId = ResourceTable::LookupSkelNameId(pAnim->m_skelID);
		pGeneratorSet = ScriptManager::MapLookup<DC::EffGeneratorSet>(s_skelToSettingsMap, skelActorId);
		if (!pGeneratorSet)
		{
			pGeneratorSet = ScriptManager::MapLookup<DC::EffGeneratorSet>(s_skelToSettingsMap, SID("default"));
		}
	}
	return pGeneratorSet;
}


void EffGenerator::InitFromDc(ArtItemAnimHandle anim, const DC::EffGeneratorSet* pSet)
{
	ANIM_ASSERT(pSet);
	memcpy(&m_dcSettings, pSet, sizeof(m_dcSettings));	
	
	for (auto& pGen : m_apJoints)
	{
		NDI_DELETE pGen;
		pGen = nullptr;
	}
	m_apJoints[kRAnkle] = NDI_NEW JointEffGenerator(anim, pSet->m_rightAnkle, pSet->m_thresholds);
	m_apJoints[kRBall] = NDI_NEW JointEffGenerator(anim, pSet->m_rightToe, pSet->m_thresholds);
	m_apJoints[kLAnkle] = NDI_NEW JointEffGenerator(anim, pSet->m_leftAnkle, pSet->m_thresholds);
	m_apJoints[kLBall] = NDI_NEW JointEffGenerator(anim, pSet->m_leftToe, pSet->m_thresholds);
}

REGISTER_RPC_NAMESPACE_FUNC(bool, EffGenerator, GenerateEffFileForActor, (const char* pActorName, const char* pSkelActor));

void ForceLinkAnimContacts()
{}
