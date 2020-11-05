/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/joints.h>

#include "corelib/util/timer.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"

namespace OrbisAnim
{
	namespace CommandBlock
	{

		float euclidian(const Point& p0, const Point& p1)
		{
			float sum = 0.0;
			for (unsigned int i = 0; i < 3; i++)
				sum += (p1[i] - p0[i]) * (p1[i] - p0[i]);
			return Sqrt(sum);
		}

		float getArea0(const Point& p0, const Point& p1, const Point& p2)
		{

			float a = euclidian(p0, p1);
			float b = euclidian(p0, p2);
			float c = euclidian(p1, p2);

			float s = (a + b + c) / 2.0;

			return Sqrt(s * (s - a) * (s - b) * (s - c));
		}

		float getArea(const Point& p0, const Point& p1, const Point& p2)
		{

			Vector d10	  = p0 - p1;
			Vector d12	  = p2 - p1;
			Vector normal = Cross(d10, d12);
			return Length(normal) * 0.5f;
		}

		void GetWachspress(const Point* pPoints, const Point& pnt, float* pBary)
		{
			// 	It's the polygon generalized barycentrics, it is reproductive.
			// 	N pnts making a polygon, they must be ordered, polygon must be planar(or almost) and convex, N >= 3
			// 	pnt is the point within the polygon
			// 
			// 	//  10 ---- 11      3 ---- 2     v            x
			// 	//  .       .       .      .     .            .
			// 	//  .       .       .      .     .            .
			// 	//  00 ---- 10      0 ---- 1     ...... u     ...... z    z-x  because Maya axis are like that (right hand)

			int numP = 4;
			float As[4];
			float Cs[4];
// 			MDoubleArray As, Cs;
// 			As.setLength(numP);
// 			Cs.setLength(numP);
// 			bary.setLength(numP);

			for (unsigned int ii = 0; ii < numP; ii++) {
				As[ii] = getArea(pnt, pPoints[ii], pPoints[(ii + 1) % numP]);
				Cs[ii] = getArea(pPoints[ii], pPoints[(ii + 1) % numP], pPoints[(ii - 1) % numP]);
			}

			float w_sum = 0.0;

			for (unsigned int ii = 0; ii < numP; ii++) {
				pBary[ii] = Cs[ii] * As[(ii + 1) % numP] * As[(ii + 2) % numP];
				w_sum += pBary[ii];
			}

			for (unsigned int ii = 0; ii < numP; ii++)
				pBary[ii] = pBary[ii] / w_sum;
		}

		void GetWachspressASBilerp(const Point* pPoints, const Point& pnt, float* pBary)
		{
			// 	It's the polygon generalized barycentrics, it is reproductive.
			// 	N pnts making a polygon, they must be ordered, polygon must be planar(or almost) and convex, N >= 3
			// 	pnt is the point within the polygon
			// 
			// 	//  10 ---- 11      3 ---- 2     v            x
			// 	//  .       .       .      .     .            .
			// 	//  .       .       .      .     .            .
			// 	//  00 ---- 10      0 ---- 1     ...... u     ...... z    z-x  because Maya axis are like that (right hand)
			float dx = pPoints[1][0] - pPoints[0][0];
			float dy = pPoints[3][1] - pPoints[0][1];
			float x = pnt.X();
			float y = pnt.Y();
			float oodx = 1.0f / dx;
			float oody = 1.0f / dy;
			float rightW	= (x - pPoints[0][0]) * oodx;
			float topW		= (y - pPoints[0][1]) * oody;

			float bary[4];
			bary[0] = fabsf((1.0f - topW) * (1.0f - rightW));
			bary[1] = fabsf((1.0f - topW) * rightW);
			bary[2] = fabsf(topW * rightW);
			bary[3] = fabsf(topW * (1.0f - rightW));
			float oosum = 1.0f / (bary[0] + bary[1] + bary[2] + bary[3]);
			pBary[0] = bary[0] * oosum;
			pBary[1] = bary[1] * oosum;
			pBary[2] = bary[2] * oosum;
			pBary[3] = bary[3] * oosum;
		}


		/// --------------------------------------------------------------------------------------------------------------- ///
		template <typename ComputeCoordsFunT>
		void ExecuteCommandWachspressImpl(const HierarchyHeader* pHierarchyHeader,
										  const WachspressParams* pParams,
										  const WachspressParams::Entry* pEntries,
										  ComputeCoordsFunT ComputeCoords)
		{
			RIG_NODE_TIMER_START();

			// https://www.mn.uio.no/math/english/people/aca/michaelf/papers/wach_mv.pdf
			// https://uu.diva-portal.org/smash/get/diva2:400706/FULLTEXT01.pdf
			// Extract the parameters
			for (U32 iParam = 0; iParam != pParams->m_numEntries; ++iParam)
			{
				const WachspressParams::Entry* pEntry = pEntries + iParam;
				float dInputs[3];
				dInputs[0] = pEntry->m_inputLoc[0] == Location::kInvalid ? pEntry->m_inputDefaults[0] : *HierarchyFloat(pEntry->m_inputLoc[0], pHierarchyHeader);
				dInputs[1] = pEntry->m_inputLoc[1] == Location::kInvalid ? pEntry->m_inputDefaults[1] : *HierarchyFloat(pEntry->m_inputLoc[1], pHierarchyHeader);
				dInputs[2] = pEntry->m_inputLoc[2] == Location::kInvalid ? pEntry->m_inputDefaults[2] : *HierarchyFloat(pEntry->m_inputLoc[2], pHierarchyHeader);
				Point inputPoint = Point(dInputs[0], dInputs[1], dInputs[2]);
				Point points[4];
				points[0] = Point(pEntry->m_pointValues[0][0], pEntry->m_pointValues[0][1], pEntry->m_pointValues[0][2]);
				points[1] = Point(pEntry->m_pointValues[1][0], pEntry->m_pointValues[1][1], pEntry->m_pointValues[1][2]);
				points[2] = Point(pEntry->m_pointValues[2][0], pEntry->m_pointValues[2][1], pEntry->m_pointValues[2][2]);
				points[3] = Point(pEntry->m_pointValues[3][0], pEntry->m_pointValues[3][1], pEntry->m_pointValues[3][2]);

				// get wachspress cordinates
				float bary[4];
				ComputeCoords(points, inputPoint, bary);

				// update outputs 
				*HierarchyFloat(pEntry->m_outputLoc[0], pHierarchyHeader) = bary[0];
				*HierarchyFloat(pEntry->m_outputLoc[1], pHierarchyHeader) = bary[1];
				*HierarchyFloat(pEntry->m_outputLoc[2], pHierarchyHeader) = bary[2];
				*HierarchyFloat(pEntry->m_outputLoc[3], pHierarchyHeader) = bary[3];
			}
			RIG_NODE_TIMER_END(RigNodeType::kWachspress);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("Wachspress\n");
				for (U32 iParam = 0; iParam != pParams->m_numEntries; ++iParam)
				{
					const WachspressParams::Entry* pEntry = pEntries + iParam;
					MsgAnim("   out[%d]: %.4f %.4f %.4f %.4f\n", iParam,
						*HierarchyFloat(pEntry->m_outputLoc[0], pHierarchyHeader),
						*HierarchyFloat(pEntry->m_outputLoc[1], pHierarchyHeader),
						*HierarchyFloat(pEntry->m_outputLoc[2], pHierarchyHeader),
						*HierarchyFloat(pEntry->m_outputLoc[3], pHierarchyHeader));
				}
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandWachspress(DispatcherFunctionArgs_arg_const param_qw0,
									  LocationMemoryMap_arg_const memoryMap,
									  OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			WachspressParams const* pParams = (WachspressParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
//			ExecuteCommandWachspressImpl(pHierarchyHeader, pParams, (const WachspressParams::Entry*)(pParams + 1), GetWachspress);
			ExecuteCommandWachspressImpl(pHierarchyHeader, pParams, (const WachspressParams::Entry*)(pParams + 1), GetWachspressASBilerp);
		}

		void ExecuteCommandWachspressImpl(const HierarchyHeader* pHierarchyHeader,
										  const WachspressParams* pParams,
										  const WachspressParams::Entry* pEntries)
		{
			ExecuteCommandWachspressImpl(pHierarchyHeader, pParams, pEntries, GetWachspressASBilerp);
		}

	} // namespace CommandBlock
}	//namespace OrbisAnim

