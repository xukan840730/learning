/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

class BuildTransformContext;
class GeometryExportTransform;

TransformOutput GeometryExportTransform_Configure(const BuildTransformContext *const pContext, 
												const libdb2::Actor *const dbactor);
void BuildModuleGeometryExport_Configure(const BuildTransformContext* pContext, const std::vector<const libdb2::Actor*>& actorList);




