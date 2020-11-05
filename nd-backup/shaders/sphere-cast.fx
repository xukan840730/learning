/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "global-funcs.fxi"

void closestPointLineSeg(in float3 c, in float3 a, in float3 b, out float t, out float3 result)
{
	float3 ab = b - a;
	t = dot(c - a, ab) / dot(ab, ab);

	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	result = a + t * ab;
}

void SetupClosestPointTriangleCache(in float3 tri[3], out float QQ, out float RR, out float QR)
{
	//PROFILE(Havok, SetupCache);

	float3 Q = tri[0] - tri[1];
	float3 R = tri[2] - tri[1];

	QQ = dot(Q, Q);
	RR = dot(R, R);
	QR = dot(R, Q);

	float QQRR = QQ * RR;
	float QRQR = QR * QR;
	float Det = (QQRR - QRQR);

	//ASSERTF(Det != 0, ("possible degenerate triangle encountered"));

	float invDet = 1.0f / Det;

	QQ = QQ * invDet;
	RR = RR * invDet;
	QR = QR * invDet;
}

int vertexToEdge(int index)
{
	//const int vertexToEdgeLut[] = { 2, 0, 1, 2, 0 };
	// return vertexToEdgeLut[index];

	return (index + 2) % 3;
}

void ClosestPointSphereTriangle(in float3 position, in float3 tri[3], in float QQ, in float RR, in float QR, out float3 hitDirection, out float distance)
{
	float3 relPos = tri[1] - position;

	float3 Q = tri[0] - tri[1];
	float sq = dot(relPos, Q);

	float3 R = tri[2] - tri[1];
	float sr = dot(relPos, R);

	float q = (sr * QR - RR * sq);
	float r = (sq * QR - QQ * sr);

	// Make sure, we are really outside, before moving to the edge edge cases
	float relEps = 0.001f;

	// MAYBE baricentric coords of sphere center
	float3 mask3 = float3(0, 0, 0) < float3(
		q + relEps,
		(1.0f + relEps) - q - r,
		r + relEps);
	//relEps);

	int3 imask3 = float3(0, 0, 0) < float3(
		q + relEps,
		(1.0f + relEps) - q - r,
		r + relEps);
	//relEps);

	//hkVector4Comparison mask = hkVector4::getZero().compareLessThan4( proj );

	//Simd::Mask mask = Simd::CompareLT(Vector(kZero).GetVec4(), proj.GetVec4());

	//float4 mask = float4(float(0.0f < proj.x), float(0.0f < proj.y), float(0.0f < proj.z), float(0.0f < proj.w));

	//
	//	Completely inside
	//

	int imask = (imask3.z << 2) | (imask3.y << 1) | (imask3.x);

	if (imask == 0x07)
	{
		//const hkRotation& triMatrix = reinterpret_cast<const hkRotation&>(tri[0]);
		hitDirection = cross(Q, R);
		hitDirection = normalize(hitDirection);

		distance = dot(hitDirection, relPos);

		if (distance > 0)
		{
			hitDirection = -hitDirection;;
		}
		else
		{
			distance = -distance;
		}

		return;
	}


	// now do the three edges
	//int index = maskToIndex(mask3.xyz);
	const int maskToIndexLut[] = { -1,  0, 1, 2 - 8, 2, 1 - 8, 0 - 8, -1 };
	int index = maskToIndexLut[imask];


	// check for a single positive value, only one edge needed
	if (index < 0)
	{
		index += 8;
		//ASSERTF( index >=0 && index < 3, ("Degenerate case found in point - triangle collision detection algorithm. This can result from a degenerate input triangle") );

		float3 pointOnEdge;
		float closestPtTime;
		/*int whereOnLine = */closestPointLineSeg(position, tri[vertexToEdge(index + 2)], tri[vertexToEdge(index)], closestPtTime, pointOnEdge);

		hitDirection = position - pointOnEdge;
		distance = length(hitDirection);
		hitDirection = hitDirection / distance; //  normalize(hitDirection);
	}
	else
		// check two edges and search the closer one
	{
		//ASSERT( index >=0 && index < 3 );

		float3 pointOnEdgeA;
		float closestPtTimeA;

		/*int whereOnLineA = */closestPointLineSeg(position, tri[index], tri[vertexToEdge(index + 2)], closestPtTimeA, pointOnEdgeA);

		float3 pointOnEdgeB;
		float closestPtTimeB;
		/*int whereOnLineB = */closestPointLineSeg(position, tri[vertexToEdge(index)], tri[index], closestPtTimeB, pointOnEdgeB);

		float3 t0 = position - pointOnEdgeA;
		float3 t1 = position - pointOnEdgeB;
		float distA = dot(t0, t0);
		float distB = dot(t1, t1);

		if (distA < distB)
		{
			float inv = 1.0f / sqrt(distA);
			distance = distA * inv;
			float invS = inv;
			hitDirection = invS * t0;
		}
		else
		{
			float inv = 1.0f / sqrt(distB);
			distance = distB * inv;
			float invS = inv;
			hitDirection = invS * t1;
		}
	}
}


struct ContactEvent
{
	float3 position;
	float3 separatingNormal;
	float separatingNormalDist;
};

bool GetSphereTriClosestPoints(in float3 position, in float r, in float3 tri[3], in float tolerance, in float QQ, in float RR, in float QR, out ContactEvent startPointEvent)
{
	// PROFILE(Havok, SphereTri);
	startPointEvent = ContactEvent(0);

	float3 posA = position;

	float3 hitDirection;
	float dist;
	ClosestPointSphereTriangle(posA, tri, QQ, RR, QR, hitDirection, dist);

	float radiusSum = r;

	if (dist < radiusSum + tolerance)
	{
		startPointEvent.position = posA - hitDirection * dist;

		// note we might need to combine this in vector
		startPointEvent.separatingNormal = hitDirection;
		startPointEvent.separatingNormalDist = dist - radiusSum;


		//hkpCdPoint event( bodyA, bodyB, contact );
		//collector.addCdPoint( event );

		//g_prim.Draw(DebugLine(startPointEvent.position, hitDirection, kColorRed), kPrimDuration1FramePauseable);
		return true;
	}
	return false;
}

bool SphereTriLinearCastPostCache(in float3 position, in float r, in float3 inPath, in float3 tri[3], in float QQ, in float RR, in float QR,
	in float cachedPathLength,
	in int iterativeLinearCastMaxIterations,
	//, void* startCollector
	out float3 resContact,
	out float3 resNormal,
	out float resTime
)
{
	resNormal = float3(0.0, 0.0, 1.0);
	resTime = 0.0;
	resContact = float3(0.0, 0.0, 0.0);

	const float tolerance = 0.001;
	const float maxExtraPenetration = 0.01;
	const float iterativeLinearCastEarlyOutDistance = 0.01;
	const float earlyOutDistance = 1.0f;

	float earlyOutFraction = min(earlyOutDistance, 1.0f);

	float tolerance2 = tolerance + earlyOutFraction * cachedPathLength;

	ContactEvent startPointEvent;

	bool hit = GetSphereTriClosestPoints(position, r, tri, tolerance2, QQ, RR, QR, startPointEvent);

	if (!hit)
	{
		return false;
	}

	float3 contact;
	float contactDistance;
	const float3 startPoint = startPointEvent.position;
	{
		contact = startPoint;
		contactDistance = startPointEvent.separatingNormalDist;
		resNormal = startPointEvent.separatingNormal;
		if (contactDistance < tolerance)
		{
			//if ( startCollector )
			{
				//ASSERTF(false, ("TODO"));
				//hkpCdPoint event(bodyA, bodyB, contact);
				//startCollector->addCdPoint( event );
			}
		}
	}

	float3 path = inPath * earlyOutFraction;

	float currentFraction = 0.0f;
	{
		const float startDistance = startPointEvent.separatingNormalDist;
		const float pathProjected = dot(startPointEvent.separatingNormal, path);
		const float endDistance = startDistance + pathProjected;

		//
		//	Check whether we could move the full distance
		//
		{
			if (endDistance > 0.0f)
			{

				//!!!!!!!!!!!!!!!!!!
				//resTime = -0.24;
				//return true;

				// not hitting at all
				return false;
			}

			//ASSERTF(maxExtraPenetration >= 0.f, ("You have to set the  m_maxExtraPenetration to something bigger than 0"));
			if (pathProjected + maxExtraPenetration >= 0.f)
			{
				//!!!!!!!!!!!!!!!!!!
				//resTime = -0.25;
				//return true;

				// we are not moving closer than m_maxExtraPenetration
				return false;
			}
		}

		//
		// check for early outs
		//
		if (startDistance <= iterativeLinearCastEarlyOutDistance)
		{
			if (startDistance > 0.f)
			{
				// early out if our endpoint is overshooting our early out distance
				//if ( startDistance > ( collector.getEarlyOutDistance() * ( startDistance - endDistance) ) )
				//{
				//	return;
				//}
			}
			contactDistance = 0.0f;

			// early out, because we are already very close

			resContact = position;
			resTime = currentFraction;
			return true;
		}

		// now endDistance is negative and startDistance position, so this division is allowed
		currentFraction = startDistance / (startDistance - endDistance);
	}

	//
	// now find precise collision point
	//
	//hkpClosestCdPointCollector checkPointCollector;
	float3 bodyACopyTransform = position;
	ContactEvent checkpointContactEvent;
	float bodyARCopy = r;

	{

		for (int i = iterativeLinearCastMaxIterations - 1; i >= 0; i--)
		{
			//
			//	Move bodyA along the path and recheck the collision 
			//
			{
				//checkPointCollector.reset();
				//
				// Move the object along the path
				//
				const float3 oldPosition = position;
				float3 newPosition = oldPosition + path * currentFraction;
				bodyACopyTransform = newPosition;

				//g_prim.Draw(DebugSphere(newPosition, r, kColorCyan), kPrimDuration1FramePauseable);
				hit = GetSphereTriClosestPoints(newPosition, r, tri, tolerance2, QQ, RR, QR, checkpointContactEvent);

				if (!hit)
				{
					//!!!!!!!!!!!!!!!
					//resTime = - i;
					//return true;

					return false;
				}
				//ASSERTF(IsFinite(checkpointContactEvent.separatingNormal), ("Invalid unwelded normal"));
			}

			//
			// redo the checks
			//
			{
				const float3 checkPoint = checkpointContactEvent.position;
				resNormal = checkpointContactEvent.separatingNormal;
				contact = checkPoint;

				contactDistance = currentFraction * earlyOutFraction;

				float pathProjected2 = dot(resNormal, path);
				if (pathProjected2 >= 0)
				{

					//!!!!!!!!!!!!!!
					//resTime = -20 - i;
					//return true;

					return false;	// normal points away
				}
				pathProjected2 = -pathProjected2;


				float startDistance2 = checkpointContactEvent.separatingNormalDist;

				//
				//	pre distance is the negative already traveled distance relative to the new normal
				//
				float preDistance = pathProjected2 * currentFraction;
				//ASSERTF(preDistance >= 0.0f, ("Numerical accuracy problem in linearCast"));

				if (startDistance2 + preDistance > pathProjected2)
				{
					// endDistance + preDistance = realEndDistance;
					// if realEndDistance > 0, than endplane is not penetrated, so no hit

					//!!!!!!!!!!!!!!
					//resTime = -40 - i;
					//return true;
					return false;
				}

				if (startDistance2 <= iterativeLinearCastEarlyOutDistance)
				{
					// early out, because we are already very close
					break;
				}

				// now we know that pathProjected2 < 0
				// so the division is safe
				currentFraction += (startDistance2 / pathProjected2);
				//if ( currentFraction > collector.getEarlyOutDistance() )
				//{
				//	// the next currentFraction would already be beyond the earlyOutDistance, so no more hits possible
				//	return;
				//}
			}
		}
	}

	resContact = contact;
	resTime = currentFraction;
	return true;
}

bool SphereTriIntersect(
	in float3 opos[3], // triangle
	in float3 probeStart, in float3 probeDirNorm, in float rayMaxT, in float probeRadius, // probe
	out float time, out float3 outContact, out float3 outNormal)
{
	float QQ, RR, QR;
	SetupClosestPointTriangleCache(opos, QQ, RR, QR);

	const float cachedPathLength = rayMaxT;
	const float3 inPath = probeDirNorm * rayMaxT;
	const int iterativeLinearCastMaxIterations = 20;

	return SphereTriLinearCastPostCache(probeStart, probeRadius, inPath, opos, QQ, RR, QR,
		cachedPathLength,
		iterativeLinearCastMaxIterations,
		//, void* startCollector
		outContact, outNormal, time);
}
