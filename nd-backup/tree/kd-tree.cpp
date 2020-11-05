/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "common/tree/kd-tree.h"

#include "common/common.h"
#include "common/libmath/commonmath.h"
#include "common/hashes/ingamehash.h"
#include "common/imsg/msg.h"
#include "common/imsg/msg_macro_defs.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

// ----------------------------------------------------------------------------------------------------
// KdTree
// ----------------------------------------------------------------------------------------------------

KdTreeOptions g_kdTreeOptions;
Scalar KdTree::kSplitTolerance(0.0001f);

StringId64 KdTree::GetTreeType() const
{
	return StringToStringId64("KdTree");
}

KdTree::~KdTree()
{
	NDI_DELETE[] m_aNodes;
	NDI_DELETE[] m_aPrimitives;

	m_aNodes = NULL;
	m_aPrimitives = NULL;
	m_totalNodes = m_totalPrimitives = 0;
}
