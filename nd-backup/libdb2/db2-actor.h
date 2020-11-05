/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/libs/libdb2/db2-facade.h"
#include "tools/libs/libdb2/db2-anim.h"
#include "tools/libs/libdb2/db2-bundle.h"
#include "tools/libs/libdb2/db2-commontypes.h"

#include "tools/libs/libdb2/db2-level.h"     // for LevelTagList
#include "tools/libs/libdb2/db2-motion-match-set.h"

namespace libdb2
{

/// --------------------------------------------------------------------------------------------------------------- ///
class Bone: public ElementFacade
{
public:
	std::string m_src;  //supposed to be const be had to
	std::string m_dst;  //change to mutable to write the = operator that the compiler insisted on 

	Bone(const QueryElement& queryElement):
	ElementFacade(queryElement),  // we do not search for bone element because we are already given a bone level element
		m_src(queryElement.Value("src").second),
		m_dst(queryElement.Value("dst").second)
	{


	}
	Bone(const Bone &model):	
	ElementFacade(model),
		m_src(model.m_src),
		m_dst(model.m_dst)
	{
	}

protected:
	virtual std::string Prefix() const{return  "Bones." + ElementFacade::Prefix();}

};
typedef  ListFacade<Bone> Bones;

/// --------------------------------------------------------------------------------------------------------------- ///
class Set : public ElementFacade
{
public:
	std::string m_value;
	Set(const QueryElement& queryElement):
	ElementFacade(queryElement), // we do not search for set element because we are already given a set level element
		m_value(queryElement.Value("value").second)
	{
	}

	Set(const Set &model):	
	ElementFacade(model),
		m_value(model.m_value)
	{
	}
/*	Set &operator = (const Set & rhs)
	{
		this->pQueryElement = rhs.pQueryElement;
		this->m_value =  rhs.m_value;
		return *this;
	};*/
protected:
	virtual std::string Prefix() const{return  "Set." + ElementFacade::Prefix();}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BoundingBox : public ElementFacade
{
public:
	enum EBoundingBox{ kAutomatic, kManual, kDynamic};
	EnumeratedFacade<float, 3> m_min;
	EnumeratedFacade<float, 3> m_max;
	EnumeratedFacade<float, 1> m_dynamicPadding;
	EBoundingBox m_type;
	BoundingBox(const QueryElement& queryElement):	
		ElementFacade(queryElement),
		m_min(queryElement, "min"),
		m_max(queryElement, "max"),
		m_dynamicPadding(queryElement, "dynamic_padding"),
		m_type(queryElement.Value("type").first ?  GetType(queryElement.Value("type").second) : kAutomatic)
	{
	}

	BoundingBox(const BoundingBox &rhs)	: 
		ElementFacade(rhs)
		,m_min(rhs.m_min)
		,m_max(rhs.m_max)
		,m_dynamicPadding(rhs.m_dynamicPadding)
		,m_type(rhs.m_type)
	{
	}
protected:
	virtual std::string Prefix() const {return  "CompressionBox." + ElementFacade::Prefix();}

private:
	static EBoundingBox GetType(const std::string& str)
	{
		if (str == "runtime" || str == "dynamic")  // the "dynamic" flag has been replaced by "runtime"
			return kDynamic;
		if (str == "manual")
			return kManual;
		return kAutomatic;
	}
};

typedef ListFacade<Set> SetList;

/// --------------------------------------------------------------------------------------------------------------- ///
class Skeleton : public ElementFacade
{
public:
	Bones m_bones;
	CanonicPath m_sceneFile;
	std::string m_ragdoll;
	std::string m_defaultAnimExportNamespace;
	std::string m_skelExportNamespace;
	SetList      m_sets;
	std::string      m_set;
	bool m_exportSkeleton;

	Skeleton(const QueryElement& queryElement):
	ElementFacade(queryElement),
		m_bones(queryElement.Element("bones"), "bonepair"),
		m_sceneFile(queryElement.Value("sceneFile").second),
		m_ragdoll(queryElement.Value("ragdoll").second),
		m_defaultAnimExportNamespace(queryElement.Value("prefix").second),
		m_skelExportNamespace(queryElement.Value("skelNamespace").second),
		m_sets(queryElement, "set"),
		m_set(queryElement.Element("set").Value("value").second), 
		m_exportSkeleton(queryElement.Value("exportSkeleton").first == false || queryElement.Value("exportSkeleton").second == std::string("true"))
	{
	}

	

	Skeleton(const Skeleton& other, bool tag)		// copy constructor to be used for lodactors(in the constructor) tag is only here so that this constructor is not called by accident
	: ElementFacade(other),
		m_bones(other.m_bones),
		m_sceneFile(other.m_sceneFile),
		m_ragdoll(other.m_ragdoll),
		m_defaultAnimExportNamespace(other.m_defaultAnimExportNamespace),
		m_skelExportNamespace(other.m_skelExportNamespace),
		m_sets(other.m_sets), 
		m_set(other.m_set), 
		m_exportSkeleton(false)					//explictly prevents from exporting skeleton as this skel is only to be used in lods...
	{
	}
protected:
	virtual std::string Prefix() const {return  "Skeleton." + ElementFacade::Prefix();}

};
   	
/// --------------------------------------------------------------------------------------------------------------- ///
class GeometryTag_Set : public ElementFacade
{
public:
	std::string m_value;
	GeometryTag_Set(const QueryElement& queryElement):
	ElementFacade(queryElement), // we do not search for set element because we are already given a GeomteryTag_Set element
		m_value(queryElement.Value("value").second)
	{
	}

	GeometryTag_Set(const GeometryTag_Set &model):	
	ElementFacade(model),
		m_value(model.m_value)
	{
	}
/*	GeometryTag_Set &operator = (const GeometryTag_Set & rhs)
	{
		this->pQueryElement = rhs.pQueryElement;
		this->m_value =  rhs.m_value;
		return *this;
	};*/
protected:
	virtual std::string Prefix() const {return  "GeometryTag_Set." + ElementFacade::Prefix();}

};

typedef ListFacade<GeometryTag_Set>  GeometryTag_SetList;

/// --------------------------------------------------------------------------------------------------------------- ///
class GeometryTag_ShaderFeature : public ElementFacade
{
public:
	std::string m_value;
	GeometryTag_ShaderFeature(const QueryElement& queryElement):
	ElementFacade(queryElement), // we do not search for set element because we are already given a ShaderFeature element
		m_value(queryElement.Value("value").second)
	{
	}

	GeometryTag_ShaderFeature(const GeometryTag_ShaderFeature &model):	
	ElementFacade(model),
		m_value(model.m_value)
	{
	}
/*	GeometryTag_ShaderFeature &operator = (const GeometryTag_ShaderFeature & rhs)
	{
		this->pQueryElement = rhs.pQueryElement;
		this->m_value =  rhs.m_value;
		return *this;
	};
	*/
protected:
	virtual std::string Prefix() const{return  "GeometryTag_ShaderFeature." + ElementFacade::Prefix();}
};

typedef ListFacade<GeometryTag_ShaderFeature>  GeometryTag_ShaderFeatureList;

/// --------------------------------------------------------------------------------------------------------------- ///
class Geometry : public ElementFacade
{
public:
#if  defined(LINUX) || _MSC_VER == 1600
#pragma message("compiling with an non c++x11 compliant compiler. The geometry constructor of libd2 will not work")
	Geometry(const QueryElement& elem) :
		ElementFacade(QueryElement::null)
	{
	}

	Geometry(const Geometry &rhs) :
		ElementFacade(QueryElement::null)
	{
	}
#else

	enum ForcedUv
	{
		kForcedUv0 = 1,
		kForcedUv1 = 2,
		// no uv2 because of legacy reasons: on ps3 we only had two set of uvs: 0 and 1, and uv2 was reserved for lightmap uv; then on ps4 we added two more sets, so we had to name them uv3 and uv4
		kForcedUv3 = 4,
		kForcedUv4 = 8
	};

	CanonicPath m_sceneFile;
	EnumeratedFacade<float, 1> m_frame;
	EnumeratedFacade<int, 1> m_vis_joint_index;
	EnumeratedFacade<float, 4> m_vis_sphere;
	std::string m_cubemap_joint_index;
	GeometryTag_ShaderFeatureList m_shaderfeature;
	std::string m_set;
	std::string m_collisionSet;
//	std::string m_geometryCompression;
	bool m_generateFeaturesOnAllSides;
	bool m_exportDestructionData;
	std::string m_parentingJointName;
	EnumeratedFacade<float, 1> m_lodDistance;
	bool m_useFixedCompression; // wants to use fixed compression on this actor instead of the default one
	bool m_useBakedForegroundLighting;
	EnumeratedFacade<unsigned int, 1> m_max_vertex_weights;
	CanonicPath m_collisionSceneFile;
	BoundingBox m_boundingBox;
	bool m_prerollHavokSimulation;
	bool m_allowShadows;	// defaults to true: THIS A HACK (MIGHT STAY HERE FOREVER). When false, it means we want to turn off shadow at the mesh level when we process the scene
	LightmapsOverride m_lightmapsOverride;
	U32 m_forcedUvs;

	Geometry(const QueryElement& queryElement) :
		ElementFacade(queryElement)
		,m_sceneFile	(queryElement.Value("sceneFile").second)
		,m_frame		(queryElement, "frame")
		,m_vis_joint_index(queryElement, "vis_joint_index")
		,m_vis_sphere	 (queryElement, "vis_sphere")
		,m_cubemap_joint_index(queryElement.Value("cubemap_joint_index").second)
		,m_set(queryElement.Element("set").Value("value").second) //elem, "geometry", "set")
		,m_collisionSet(queryElement.Element("collisionSet").Value("value").second)
		,m_shaderfeature(queryElement, "shaderfeature")
	//	,m_geometryCompression(queryElement.Value("geometryCompression").first == true ?  queryElement.Value("geometryCompression").second : "default")
		,m_generateFeaturesOnAllSides(queryElement.Value("generateFeaturesOnAllSides").first == true && queryElement.Value("generateFeaturesOnAllSides").second == std::string("true"))
		,m_parentingJointName(queryElement.Value("parentingJointName").second)
		,m_exportDestructionData(queryElement.Value("exportDestructionData").first == true && queryElement.Value("exportDestructionData").second == std::string("true"))
		,m_lodDistance(queryElement, "lodDistance")
		,m_useFixedCompression(queryElement.Value("useFixedCompression").first == true && queryElement.Value("useFixedCompression").second == std::string("true"))
		,m_useBakedForegroundLighting(queryElement.Value("useBakedForegroundLighting").first == true && queryElement.Value("useBakedForegroundLighting").second == std::string("true"))
		,m_max_vertex_weights(queryElement, "max_vertex_weights")
		,m_collisionSceneFile(queryElement.Value("collisionSceneFile").second)
		,m_boundingBox(queryElement.Element("bounding_box"))
		,m_prerollHavokSimulation(queryElement.Value("prerollHavokSimulation").first == true && queryElement.Value("prerollHavokSimulation").second == std::string("true"))
		,m_allowShadows(queryElement.Value("allowShadows").first == false || queryElement.Value("allowShadows").second == std::string("true"))
		,m_lightmapsOverride(queryElement.Element("lightmapsOverride"))
		,m_forcedUvs(0)
	{
		if (queryElement.Value("mayaUvSet1").first == true && queryElement.Value("mayaUvSet1").second == std::string("true"))
			m_forcedUvs |= kForcedUv0;

		if (queryElement.Value("mayaUvSet2").first == true && queryElement.Value("mayaUvSet2").second == std::string("true"))
			m_forcedUvs |= kForcedUv1;

		if (queryElement.Value("mayaUvSet3").first == true && queryElement.Value("mayaUvSet3").second == std::string("true"))
			m_forcedUvs |= kForcedUv3;

		if (queryElement.Value("mayaUvSet4").first == true && queryElement.Value("mayaUvSet4").second == std::string("true"))
			m_forcedUvs |= kForcedUv4;
	}

	Geometry(const Geometry &rhs) :
		ElementFacade(rhs),
		m_sceneFile(rhs.m_sceneFile),
		m_frame(rhs.m_frame),
		m_vis_joint_index(rhs.m_vis_joint_index),
		m_vis_sphere(rhs.m_vis_sphere),
		m_cubemap_joint_index(rhs.m_cubemap_joint_index),
		m_set(rhs.m_set),
		m_collisionSet(rhs.m_collisionSet),
		m_shaderfeature(rhs.m_shaderfeature),
//		m_geometryCompression(rhs.m_geometryCompression),
		m_generateFeaturesOnAllSides(rhs.m_generateFeaturesOnAllSides),
		m_parentingJointName(rhs.m_parentingJointName),
		m_exportDestructionData(rhs.m_exportDestructionData),
		m_lodDistance(rhs.m_lodDistance),
		m_useFixedCompression(rhs.m_useFixedCompression),
		m_useBakedForegroundLighting(rhs.m_useBakedForegroundLighting),
		m_max_vertex_weights(rhs.m_max_vertex_weights),
		m_collisionSceneFile(rhs.m_collisionSceneFile),
		m_boundingBox(rhs.m_boundingBox),
		m_prerollHavokSimulation(rhs.m_prerollHavokSimulation),
		m_allowShadows(rhs.m_allowShadows),
		m_lightmapsOverride(rhs.m_lightmapsOverride),
		m_forcedUvs(rhs.m_forcedUvs)
	{
	}

#endif
protected:
	virtual std::string Prefix() const{return  "Geometry." + ElementFacade::Prefix();}

};


// This is the version of the geometry that can be put in a List facade.
// to be consistent with the behaviour that is expected by the list facade
// we need a constructor that accepts to be passed a geom object directly rather that its container
// this class adapts the Geometry class by passing false to it's constructor to prevent the 'in container lookup' behaviour of the default Geometry class
// 

/// --------------------------------------------------------------------------------------------------------------- ///
class ListedGeometry : public Geometry
{
public :
	ListedGeometry(const QueryElement& queryElement)
		: Geometry(queryElement)
	{
	}

	ListedGeometry(const ListedGeometry &rhs)
		: Geometry(rhs)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class Collision : public ElementFacade
{
public:
	CanonicPath m_sceneFile;

	EnumeratedFacade<float, 1> m_physEnvelope;
	EnumeratedFacade<float, 1> m_physFriction;
	EnumeratedFacade<float, 1> m_physElasticity;
	EnumeratedFacade<float, 1> m_physDensity;
	EnumeratedFacade<float, 1> m_physFluidFriction0;
	EnumeratedFacade<float, 1> m_physFluidFriction1;
	EnumeratedFacade<float, 1> m_physFluidFriction2;

	Collision(const QueryElement& queryElement):
	ElementFacade(queryElement),
		m_sceneFile(queryElement.Value("sceneFile").second),
		m_physEnvelope(queryElement, "physEnvelope"),
		m_physFriction(queryElement, "physFriction"),
		m_physElasticity(queryElement, "physElasticity"),
		m_physDensity(queryElement, "physDensity"),
		m_physFluidFriction0(queryElement,"physFluidFriction0"),
		m_physFluidFriction1(queryElement,"physFluidFriction1"),
		m_physFluidFriction2(queryElement,"physFluidFriction2")
	{
	}
protected:
	virtual std::string Prefix() const{return  "Collision." + ElementFacade::Prefix();}

};

/// --------------------------------------------------------------------------------------------------------------- ///
class CinematicBindingInfo : public ElementFacade
{
public:
	std::string m_animAlias;
	std::string m_lookName;
    std::string m_audioStem;
    std::string m_parentToAlias;
    std::string m_parentJointOrAttach;
    std::string m_attachSysId;
    std::string m_tintRandomizer;

	CinematicBindingInfo(const QueryElement& queryElement) :
		ElementFacade(queryElement)
		,m_animAlias(queryElement.Value("animAlias").second)
		,m_lookName(queryElement.Value("lookName").second)
		,m_audioStem(queryElement.Value("audioStem").second)
		,m_parentToAlias(queryElement.Value("parentToAlias").second)
		,m_parentJointOrAttach(queryElement.Value("parentJointOrAttach").second)
		,m_attachSysId(queryElement.Value("attachSysId").second)
		,m_tintRandomizer(queryElement.Value("tintRandomizer").second)
	{
	}
protected:
	virtual std::string Prefix() const{return  "CinematicBindingInfo." + ElementFacade::Prefix();}
};

typedef ListFacade<CinematicBindingInfo>  CinematicBindingInfoList;

/// --------------------------------------------------------------------------------------------------------------- ///
class Cinematic : public ElementFacade
{
public:
	std::string		m_audio;
	std::string		m_dialogAudio;
	std::string		m_transitionTypeStart;
    U32				m_transitionFramesStart;
	std::string		m_transitionTypeEnd;
    U32				m_transitionFramesEnd;
	LevelTagList	m_levels;

	bool Loaded() const { return m_loaded && Name().substr(0, 4) == "cin-"; }

	Cinematic(const QueryElement& queryElement) :
		ElementFacade(queryElement)
		,m_audio(queryElement.Value("audio").second)
		,m_dialogAudio(queryElement.Value("dialog").second)
		,m_levels(ParseList<LevelTag>(queryElement, "level"))
		,m_transitionTypeStart(queryElement.Value("transitionTypeStart").first == true ?  queryElement.Value("transitionTypeStart").second : "default")
		,m_transitionFramesStart((U32)atoi(queryElement.Value("transitionFramesStart").second.c_str()))
		,m_transitionTypeEnd(queryElement.Value("transitionTypeEnd").first == true ?  queryElement.Value("transitionTypeEnd").second : "default")
		,m_transitionFramesEnd((U32)atoi(queryElement.Value("transitionFramesEnd").second.c_str()))
	{
	}
protected:
	virtual std::string Prefix() const{return  "Cinematic." + ElementFacade::Prefix();}

};

/// --------------------------------------------------------------------------------------------------------------- ///
class CinematicSequence : public ElementFacade
{
public:
	CinematicBindingInfoList m_bindingInfoList;
	EnumeratedFacade<unsigned int, 1> m_cameraIndex;
	std::string m_cameraCut;

	bool  Loaded() const { return m_loaded && Name().substr(0, 4) == "seq-"; }

	CinematicSequence(const QueryElement& queryElement)
		:ElementFacade(queryElement)
		,m_bindingInfoList(queryElement, "bindingInfo")
		,m_cameraIndex(queryElement, "cameraIndex")
		,m_cameraCut(queryElement.Value("cameraCut").first == true ? queryElement.Value("cameraCut").second : "default")
	{
	}
protected:
	virtual std::string Prefix() const{return  "CinematicSequence." + ElementFacade::Prefix();}

};

/// --------------------------------------------------------------------------------------------------------------- ///
struct CinematicBinding
{
	std::string m_aliasName;
	std::string m_animName;
	std::string m_lookName;
    std::string m_audioStem;
    std::string m_parentToAlias;
    std::string m_parentJointOrAttach;
    std::string m_attachSysId;
    U32 m_tintRandomizer;

	static bool Compare(const CinematicBinding& a, const CinematicBinding& b)
	{
		return (a.m_aliasName < b.m_aliasName);
	}

	CinematicBinding() {} // do-nothing ctor
	CinematicBinding(const CinematicSequence& sequence, const Anim* pAnim);
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ActorRef : public AttributeFacade
{
public:
	ActorRef(const QueryElement& queryElement) :
		AttributeFacade(queryElement, std::string("path")) {}
		
	std::string GetRefTargetName() const
	{
		std::string value  = Value();
		std::size_t xmlPos   = value.rfind(".xml");
		std::size_t typePos  = value.rfind(".", xmlPos-1);
		std::string typeName = value.substr(typePos+1, xmlPos - typePos -1);
		std::size_t namePos  = value.rfind("/", typePos-1);
		std::string valueName = value.substr(namePos+1, typePos-namePos -1);
		return valueName;
	}		
};

typedef ListFacade<ActorRef>  ActorRefList;
typedef ListFacade<NameString>  PartModuleList;

/// --------------------------------------------------------------------------------------------------------------- ///
struct Gui2Resolutions 
{
	enum EResolution
	{
		kAll,
		k1080p,
		k1440p,
	};

	Gui2Resolutions(const std::string& base)
	{
		if (base == "1080p")
			m_res = k1080p;
		else if (base == "1440p")
			m_res = k1440p;
		else
			m_res = kAll;
	}
	Gui2Resolutions() : m_res(kAll) {}

	bool  operator==(Gui2Resolutions&rhs) const { return m_res == rhs.m_res; }
	bool  operator!=(Gui2Resolutions&rhs) const { return m_res != rhs.m_res; }

	EResolution m_res;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class Actor : public ElementFacade
{
public:
	GameFlagsInfo m_gameFlagsInfo;		// this does not belong to the builder DB	
										// but is somehow tied to the actor through its own db 
										// under $GAMEDBDIR/data/db/gameflagdb/builder/levels
										// It's included here for convenience
	Skeleton m_skeleton;
	Geometry m_geometry;
	Collision m_collision;
	ActorRefList m_actorsDependency;
	ActorRefList m_subActors;
	BundleRefList m_bundleref;
	AnimList m_animations;
	MotionMatchSetRefList m_motionMatchSetRefs;
	MotionMatchSetList m_motionMatchSets;
	MaterialRemapList m_materialRemapList;
	MaterialList m_extraMaterialsList;
	bool m_disableReflections;
	bool m_occludesRain;
	bool m_castsShadows;
	bool m_castsLocalShadows;
	bool m_castsAmbientShadows;
	unsigned int m_volumeAmbientOccluderCellSize;
	unsigned int m_volumeAmbientOccluderScale;
	bool m_volumeOccluderUseDirectionalTest;
	bool m_generateMultipleOccluders;
	int m_volumeOccluderDefaultAttachJoint;
	float m_volumeOccluderSamplePointShift;
	CanonicPath m_gui2BuildFile;
	bool m_gui2CompressTextures;
	Gui2Resolutions m_gui2Resolutions;
	size_t m_lodIndex;
		               
	SoundBankFile m_soundBank;
	SoundBankFileList m_soundBankList;
	IRPakFileList	 m_irpakFileList;

	ListFacade<ListedGeometry> m_geometryLods;
	PartModuleList m_partModules;
	Cinematic m_cinematic;
	CinematicSequence m_sequence;

	Actor(const QueryElement& queryElement, bool loadAnimations = true)
		: ElementFacade(queryElement)
		, m_skeleton(queryElement.Element("skeleton"))
		, m_geometry(queryElement.Element("geometry"))
		, m_collision(queryElement.Element("collision"))
		, m_actorsDependency(queryElement, "actorDependency")
		, m_subActors(queryElement, "subactor")
		, m_bundleref(loadAnimations ? queryElement.Element("bundles") : QueryElement::null, "ref")
		, m_animations(loadAnimations ? queryElement : QueryElement::null,
					   m_bundleref,
					   AnimRefList(QueryElement::null, ""),
					   AnimRefList(QueryElement::null, ""))
		, m_motionMatchSetRefs(loadAnimations ? queryElement.Element("motionMatchSets") : QueryElement::null, "ref")
		, m_motionMatchSets(loadAnimations ? queryElement : QueryElement::null, m_motionMatchSetRefs)
		, m_materialRemapList(queryElement.Element("materialRemapList"), std::string("materialRemap"))
		, m_extraMaterialsList(queryElement.Element("extraMaterialsList"), std::string("material"))
		, m_disableReflections(queryElement.Value("disableReflections").first == true
							   && queryElement.Value("disableReflections").second == std::string("true"))
		, m_occludesRain(queryElement.Value("occludesRain").first == true
						 && queryElement.Value("occludesRain").second == std::string("true"))
		, m_castsShadows(queryElement.Value("castsShadows").first == true
							 ? (queryElement.Value("castsShadows").second == std::string("true"))
							 : true)
		, m_castsLocalShadows(queryElement.Value("castsLocalShadows").first == true
								  ? (queryElement.Value("castsLocalShadows").second == std::string("true"))
								  : true)
		, m_soundBank(queryElement.Element("soundbank"))
		, m_soundBankList(queryElement.Element("soundbankList"), "soundbank")
		, m_irpakFileList(queryElement.Element("irpakList"), "irpak")
		, m_geometryLods(queryElement, "geometry")
		, m_partModules(queryElement, "partModule")
		, m_castsAmbientShadows(queryElement.Value("castsAmbientShadows").first == true
									? (queryElement.Value("castsAmbientShadows").second == std::string("true"))
									: false)
		, m_volumeAmbientOccluderCellSize(queryElement.Value("volumeAmbOccluderCellSize").first == true
											  ? (unsigned int)
													atoi(queryElement.Value("volumeAmbOccluderCellSize").second.c_str())
											  : 25)
		, m_volumeAmbientOccluderScale(queryElement.Value("volumeAmbOccluderScale").first == true
										   ? (unsigned int)
												 atoi(queryElement.Value("volumeAmbOccluderScale").second.c_str())
										   : 100)
		, m_volumeOccluderUseDirectionalTest(queryElement.Value("volumeOccluderUseDirTest").first == true
												 ? (queryElement.Value("volumeOccluderUseDirTest").second
													== std::string("true"))
												 : false)
		, m_generateMultipleOccluders(queryElement.Value("generateMultipleVolumeOccluders").first == true
										  ? (queryElement.Value("generateMultipleVolumeOccluders").second
											 == std::string("true"))
										  : false)
		, m_volumeOccluderDefaultAttachJoint(queryElement.Value("volumeAmbOccluderDefaultAttachJoint").first == true
												 ? (int)atoi(queryElement.Value("volumeAmbOccluderDefaultAttachJoint")
																 .second.c_str())
												 : -1)
		, m_volumeOccluderSamplePointShift(queryElement.Value("volumeAmbOccluderSamplePointShift").first == true
											   ? (float)atof(queryElement.Value("volumeAmbOccluderSamplePointShift")
																 .second.c_str())
											   : 0.0f)
		, m_gui2CompressTextures(queryElement.Value("gui2CompressTextures").first == true
								 ? queryElement.Value("gui2CompressTextures").second == "true"
								 : false)
		, m_gui2BuildFile(queryElement.Value("gui2BuildFile").second)
		, m_gui2Resolutions(queryElement.Value("gui2Resolutions").second)
		, m_lodIndex(0)
		, m_cinematic(queryElement.Element("cinematic"))
		, m_sequence(queryElement.Element("sequence"))
	{
		QueryElement::ReadGameFlags(m_gameFlagsInfo.m_gameFlags, m_gameFlagsInfo.m_gameFlagsPath, m_gameFlagsInfo.m_lastWriteTime, queryElement);

		MergeMotionMatchAnimsToAnimList();
	}



	// Lods used to be defined as actors in the subActorsList that have a -lodXX  ath the end of the name with XX being the lod level
	// This is still a supported way of defining lods
	// Starting with Big3, we wanted to define lods as 'geometry variations' of the main actor. So I added geometry-lods
	// In order to not break the already in place lod system,  I introduced this new constructor of the actor class
	// 
	// It enforces being the same as the base actor, by copying all of its members.
	// Then it uses the geometry lod section of the base as the geometry itself for this level ( m_geometry(mainActor.m_geometry[lodIndex]) ).

	Actor(const Actor& mainActor,
		  size_t lodIndex) // this is best called by code that knows the actual number of lods or this will crash
		: ElementFacade(mainActor)
		, m_gameFlagsInfo(mainActor.m_gameFlagsInfo)
		//	,m_lodDistance			(mainActor.m_lodDistance )
		, m_skeleton(mainActor.m_skeleton, false)
		, m_geometry(mainActor.m_geometryLods[lodIndex])
		, m_collision(mainActor.m_collision)
		, m_actorsDependency(mainActor.m_actorsDependency)
		, m_subActors(QueryElement::null)
		//    	,m_lodActor				(QueryElement::null          	)
		, m_bundleref(QueryElement::null)
		, m_animations(QueryElement::null,
					   m_bundleref,
					   AnimRefList(QueryElement::null, ""),
					   AnimRefList(QueryElement::null, ""))
		, m_motionMatchSetRefs(QueryElement::null)
		, m_motionMatchSets(QueryElement::null, m_motionMatchSetRefs)
		, m_materialRemapList(mainActor.m_materialRemapList)
		, m_extraMaterialsList(mainActor.m_extraMaterialsList)
		, m_disableReflections(mainActor.m_disableReflections)
		, m_occludesRain(mainActor.m_occludesRain)
		, m_castsShadows(mainActor.m_castsShadows)
		, m_castsLocalShadows(mainActor.m_castsLocalShadows)
		, m_castsAmbientShadows(mainActor.m_castsAmbientShadows)
		, m_volumeOccluderUseDirectionalTest(mainActor.m_volumeOccluderUseDirectionalTest)
		, m_volumeAmbientOccluderCellSize(mainActor.m_volumeAmbientOccluderCellSize)
		, m_volumeAmbientOccluderScale(mainActor.m_volumeAmbientOccluderScale)
		, m_generateMultipleOccluders(mainActor.m_generateMultipleOccluders)
		, m_volumeOccluderDefaultAttachJoint(mainActor.m_volumeOccluderDefaultAttachJoint)
		, m_volumeOccluderSamplePointShift(mainActor.m_volumeOccluderSamplePointShift)
		, m_soundBank(mainActor.m_soundBank)
		, m_soundBankList(mainActor.m_soundBankList)
		, m_irpakFileList(mainActor.m_irpakFileList)
		, m_geometryLods(QueryElement::null)
		, m_partModules(QueryElement::null)
		, m_lodIndex(lodIndex)
		, m_cinematic(mainActor.m_cinematic)
		, m_sequence(mainActor.m_sequence)
	{
#if  defined(LINUX) || _MSC_VER < 1800
#pragma message("compiling with an non c++x11 compliant compiler. The Actor constructor of libd2 will not work")
#else

		// bounding box are now the same for all lods based on base lod.
		m_geometry.m_boundingBox = mainActor.m_geometry.m_boundingBox;
		m_geometry.m_vis_joint_index = mainActor.m_geometry.m_vis_joint_index;
		m_geometry.m_cubemap_joint_index = mainActor.m_geometry.m_cubemap_joint_index;
#endif

	}

	//  The naming functions had to be overriden to implement the basename-lodXX naming scheme.
	//eg: hero

	std::string BaseName() const
	{
		return ElementFacade::Name();
	}
	
	virtual std::string Name()	const   
	{ 
		return ElementFacade::Name() + LodString();
	}

	//eg: characters\hero\actors\hero\hero
	virtual std::string FullName()	const 
	{
		return ElementFacade::FullName() + LodString();
	}

	//eg: characters\hero\actors\hero\hero
	virtual std::string FullNameNoLod()	const 
	{
		return ElementFacade::FullName();
	}

	static const Actor* Load(const std::string& actorName, std::string* pActorFullName = NULL);

protected:
	void MergeMotionMatchAnimsToAnimList();

	std::string LodString() const
	{
		std::stringstream strm;
		strm << m_lodIndex;
		return m_lodIndex == 0 ? "" : ".lod"+strm.str();
	}
	virtual std::string Prefix() const{return  "Actor." + ElementFacade::Prefix();}
private:
	~Actor() {};		// hide destructor for now since we rely on shared state we do not want any one deleting. In the future, with everyone having their own copy, this would be fine.
};

}

