#pragma once

#include "db2-facade.h"
#include "db2-commontypes.h"
#include "common/util/basictypes.h"
#include "common/hashes/ingamehash.h"

namespace libdb2
{
	class LevelScene
	{
	public:
		std::string m_sceneFile;
		std::string m_lightingFile;
		bool m_collidable;
		bool m_renderable;
		bool m_addToLightmap;
		bool m_useInLightBake;

		LevelScene(const QueryElement& queryElement)
			: m_sceneFile(queryElement.Value("sceneFile").second)
			, m_lightingFile(queryElement.Value("lightingFile").second)
			, m_collidable(!HasFlag(queryElement, "nonCollidable"))
			, m_renderable(!HasFlag(queryElement, "nonRenderable"))
			, m_addToLightmap(HasFlag(queryElement, "addToLightmap"))
			, m_useInLightBake(HasFlag(queryElement, "useInLightBake", true))
		{
		}

		static bool HasFlag(const QueryElement& queryElement, const std::string& flagName, bool defaultVal = false)
		{
			std::pair<const bool, const std::string> value = queryElement.Element("flags").Value(flagName);
			bool a = value.first;
			bool b = value.second=="true";
			return a ? b : defaultVal ;
		}
	};
	typedef std::vector<LevelScene> LevelSceneList;
	
	class LevelActor
	{
	public:
		std::string m_path;
		LevelActor(const QueryElement& queryElement)
			: m_path(queryElement.Value("path").second)
		{

		}
	};
	typedef std::vector<LevelActor> LevelActorList;

	class ShaderFeature
	{
	public:
		std::string m_name;
		std::string m_action;
		ShaderFeature(const QueryElement& queryElement)
			: m_name(queryElement.Value("name").second)
			, m_action(queryElement.Value("action").second)
		{
		}
	};
	typedef ListFacade<ShaderFeature> ShaderFeatureList;

	class DCBinFile
	{
	public:
		std::string m_path;
		std::string m_dcfilename;

		DCBinFile(const QueryElement& queryElement)
			: m_path(queryElement.Value("path").second)
		{
			size_t pos = m_path.find_last_of("/");
			if (pos==std::string::npos)
				pos = 0;
			else 
				pos+=1;
			m_dcfilename = m_path.substr(pos);
			m_dcfilename = m_dcfilename.substr(0, m_dcfilename.find_last_of("."));
		}
	};
	typedef ListFacade<DCBinFile> DCBinFilesList;

	class LevelTag
	{
	public:
		std::string m_value;
		LevelTag(const QueryElement& queryElement)
			: m_value(queryElement.Value("value").second)
		{
		}
	};

	class LevelName
	{
	public:
		std::string m_value;
		LevelName(const QueryElement& queryElement)
			: m_value(queryElement.Value("value").second)
		{
		}
	};

	class LevelInfo
	{
	public:
		bool m_isWide;
		bool m_isPost;
		bool m_isSky;
		bool m_isIndoor;
		bool m_isNewSky;
		bool m_exportGeometry;
		bool m_exportCollision;
		bool m_exportTextures;
		bool m_exportMaterials;
		bool m_exportVisibility;
		bool m_exportIngamePak;
		bool m_exportAutoActors;
		bool m_usingFGMotionBlur;
		bool m_ignoreSphereTree;
		bool m_ignoreVisBits;
		bool m_disableSunShadowFadeout;		// this one does not need to be exported. But only affects shader flags
		bool m_disableShadowCasting;		// this one is really a hack to ship U3, ideally we'll remove it right after shipping... (One may have hopes....). It's supposed instruct the game to not cast shadows of regular geometry unless it has a specific shader applied to it.
		bool m_alternateSunShadowFadeout;	// this one is really a hack to ship U3, ideally we'll remove it right after shipping... (One may have hopes....). It forces the shaders of the level to use an alternate method of shadow fadeout approx min(rt-shadow, baked-shadow)
		bool m_isParticle;
		bool m_designerMode;				// This does not go in game. But should drive cheaper/faster processing mode during the build.
		bool m_fixBuildTimeLODGenerationBug; // This is to fix a LOD generation bug in bl (usning wrong clamping distances) late in BIG4 and we only want to apply this fix to a known set of levels
		bool m_autoGridShader;
		bool m_disableKdTree;
		bool m_fixForHeightMapT2RIGHTBEFORESHIP; // this will be enabled to levels that need a fix in T2. Post ship, this should be de default behaviour and the flag should be removed


		std::string m_fgVisLevel;
		std::string m_associatedBlockMeshLevel;
		std::string m_selectVisVolumesAsLevel;

		LevelInfo(const QueryElement& queryElement)
			: m_isWide(queryElement.Value("isWide").second == "true")   //will be empty string if not present
			, m_isPost(queryElement.Value("isPost").second == "true")   //will be empty string if not present
			, m_isSky(queryElement.Value("isSky").second == "true")   //will be empty string if not present
			, m_isIndoor(queryElement.Value("isIndoor").second == "true")   //will be empty string if not present
			, m_isNewSky(queryElement.Value("isNewSky").second == "true")   //will be empty string if not present
			, m_isParticle(queryElement.Value("isParticle").second == "true")   //will be empty string if not present
			, m_exportGeometry(queryElement.Value("exportGeometry").second != "false")   //no string => true   Should not be part of the U32
			, m_exportCollision(queryElement.Value("exportCollision").second != "false")   //no string => true  	Should not be part of the U32
			, m_exportTextures(queryElement.Value("exportTextures").second != "false")   //no string => true	Should not be part of the U32
			, m_exportMaterials(queryElement.Value("exportMaterials").second != "false")   //no string => true 	Should not be part of the U32
			, m_exportVisibility(queryElement.Value("exportVisibility").second != "false")		//no string => true 	Should not be part of the U32
			, m_exportIngamePak(queryElement.Value("exportIngamePak").second != "false")		//no string => true 	Should not be part of the U32
			, m_exportAutoActors(queryElement.Value("exportAutoActors").second != "false")		//no string => true 	Should not be part of the U32
			, m_usingFGMotionBlur(queryElement.Value("usingFGMotionBlur").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_ignoreSphereTree(queryElement.Value("ignoreSphereTree").second == "true")   //It has to be set to be enabled (=>default = false)  
			, m_ignoreVisBits(queryElement.Value("ignoreVisBits").second == "true")			 //It has to be set to be enabled (=>default = false)  
			, m_disableSunShadowFadeout(queryElement.Value("disableSunShadowFadeout").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_disableShadowCasting(queryElement.Value("disableShadowCasting").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_alternateSunShadowFadeout(queryElement.Value("alternateSunShadowFadeout").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_fgVisLevel(queryElement.Value("foregroundVisLevel").first ? queryElement.Value("foregroundVisLevel").second : std::string(""))
			, m_associatedBlockMeshLevel(queryElement.Value("associatedBlockMeshLevel").first ? queryElement.Value("associatedBlockMeshLevel").second : std::string(""))
			, m_selectVisVolumesAsLevel(queryElement.Value("selectVisVolumesAsLevel").first ? queryElement.Value("selectVisVolumesAsLevel").second : std::string(""))
			, m_designerMode(queryElement.Value("designerMode").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_fixBuildTimeLODGenerationBug(queryElement.Value("fixBuildTimeLODGenerationBug").second == "true")   //It has to be set to be enabled (=>default = false)
			, m_autoGridShader(queryElement.Value("useAutoGridShader").second == "true")   //replace all shaders with the autogrid shader
			, m_disableKdTree(queryElement.Value("disableKdTree").second == "true")			// skip building the kd-tree
			, m_fixForHeightMapT2RIGHTBEFORESHIP(queryElement.Value("m_fixForHeightMapT2RIGHTBEFORESHIP").second == "true")			// use multiple collision levels to compute the heightmap

		{
		}

		enum {
			//these are shift values (should be in sync with whatever is used in   shared\src\ndlib\level\level-info.h
			kIsWide	= 0,
			kIsPost = 1,
			kIsSky	= 2,
			kIsIndoor = 3,
			kIsNewSky = 4,
			kUsingFGMotionBlur = 5,
			kIgnoreSphereTree = 6,
			kIgnoreVisBits	= 7,
			kDisableShadowCasting = 8,
			kIsParticle = 9,
		};
		unsigned int AsUInt32() const
		{
			return 
			(m_isWide			<<	kIsWide)
			|(m_isPost			<<	kIsPost)
			|(m_isSky			<<	kIsSky)
			|(m_isIndoor		<<	kIsIndoor)
			|(m_isNewSky		<<	kIsNewSky)
			|(m_usingFGMotionBlur	<<	kUsingFGMotionBlur)
			|(m_ignoreSphereTree	<<	kIgnoreSphereTree)
			|(m_ignoreVisBits		<<	kIgnoreVisBits)
			|(m_disableShadowCasting		<<	kDisableShadowCasting)
			|(m_isParticle			<<	kIsParticle)
				;
		}
	};



	typedef std::vector<LevelTag> LevelTagList;
	typedef ListFacade<LevelName> LevelNameList;

	class LevelCheatVis
	{
	public:
		bool m_enabled;
		SMath::Vector m_splitSize;
	
		LevelCheatVis(const QueryElement& queryElement)
			: m_enabled(queryElement.Value("enabled").first == true && queryElement.Value("enabled").second == "true")
			, m_splitSize(ParseVector(queryElement, "splitSize", SMath::ZeroTag()))
		{
		}
	};

	class BakePreset
	{
	public:
		std::string m_name;

		BakePreset(const QueryElement& queryElement) 
			: m_name(queryElement.Value("name").second)
		{
		}
	};

	typedef ListFacade<BakePreset> BakePresetList;

	class LightAtgi
	{
	public:
		std::string m_path;

		LightAtgi(const QueryElement& queryElement)
			: m_path(queryElement.Value("path").second)
		{
		}
	};

	typedef ListFacade<LightAtgi> LightAtgiList;

	class LightingSettings
	{
	public:
		bool m_designerLightingMode;
		bool m_tetraProbes;
		bool m_keepLightmapUVs;
		float m_probeBlendDistanceHACK;
		LightmapsOverride m_lightmapsOverride;
		BakePresetList m_bakePresets;
		LightAtgiList m_lightAtgis;

		LightingSettings(const QueryElement& queryElement)
			: m_designerLightingMode(queryElement.Value("designerLighting").first == true && queryElement.Value("designerLighting").second == "true")
			, m_tetraProbes(queryElement.Value("tetraProbes").first == true && queryElement.Value("tetraProbes").second == "true")
			, m_keepLightmapUVs(queryElement.Value("keepLightmapUVs").first == true && queryElement.Value("keepLightmapUVs").second == "true")
			, m_probeBlendDistanceHACK(ParseScalar<float>(queryElement, "probeBlendDistanceHACK", -1.0f))
			, m_lightmapsOverride(queryElement.Element("lightmapsOverride"))
			, m_lightAtgis(queryElement.Element("lightAtgis"), "lightAtgi")
			, m_bakePresets(queryElement.Element("bakePresets"), "preset")
		{
		}
	};

	class Level : public ElementFacade
	{
	public:
		GameFlagsInfo m_gameFlagsInfo;		// this does not belong to the builder DB	
											// but is somehow tied to the actor through its own db 
											// under $GAMEDBDIR/data/db/gameflagdb/builder/levels
											// It's included here for convenience
		std::string m_tags;
		SMath::Vector m_minColor;
		std::string m_ingame;
		LevelSceneList m_sceneList;
		LevelActorList m_actorList;
		DCBinFilesList m_dcBinFileList;
		SoundBankFileList m_soundBankFileList;
		IRPakFileList m_irpakFileList;
		LevelTagList m_cache;
		LevelTagList m_occluded_by;
		LevelTagList m_vis_volume;
		LevelTagList m_foregroundVisLevel;
		LevelTagList m_visible;
		LevelTagList m_lightBakeOccluder;
		LevelTagList m_featureNeighbors;
		SMath::Vector m_translation;
		float m_cellSize;
		float m_samplingDistance;
		SMath::Vector m_splitSize;
		SMath::Vector m_shaderLod;
		float m_shrubFadeStart;
		float m_shrubFadeEnd;
		std::string m_runtimeLightsFile;
		std::string m_bakedSceneFile;
		std::string m_populatorFile;
		int m_coverActionPackCount;
		bool m_useLightmaps;
		LevelInfo	m_levelInfo;
		std::string m_killGroups;
		std::string m_geometryCompression;
		std::string m_lutsToLoad;
		bool m_enableRealtimeReflections;
		bool m_enableBouncedLighting;
		bool m_enableClustering;
		MaterialList m_extraMaterialsList;
		MaterialList m_materialReloadList;
		LevelCheatVis m_cheatVis;
		ShaderFeatureList m_shaderFeatures;
		LightingSettings m_lightingSettings;
		MaterialRemapList m_materialRemapList;
		AlternateResources	m_alternateResources;

		Level(const QueryElement& queryElement) 
			: ElementFacade(queryElement)
			, m_tags(queryElement.Element("tags").Value("content").second)
			, m_sceneList(ParseList<LevelScene>(queryElement.Element("sceneList"), "scene"))
			, m_actorList(ParseList<LevelActor>(queryElement.Element("actorList"), "ref"))
			, m_dcBinFileList(queryElement.Element("dcfileList"), "dcfile")
			, m_soundBankFileList(queryElement.Element("soundbankList"), "soundbank")
			, m_irpakFileList(queryElement.Element("irpakList"), "irpak")
			, m_minColor(ParseVector(queryElement, "minColor", SMath::ZeroTag()))
			, m_ingame(queryElement.Value("ingame").second)
			, m_cache(ParseList<LevelTag>(queryElement, "cache"))
			, m_occluded_by(ParseList<LevelTag>(queryElement, "occluded_by"))
			, m_vis_volume(ParseList<LevelTag>(queryElement, "vis_volume"))
			, m_foregroundVisLevel(ParseList<LevelTag>(queryElement, "foregroundVisLevel"))
			, m_visible(ParseList<LevelTag>(queryElement, "visible"))
			, m_lightBakeOccluder(ParseList<LevelTag>(queryElement, "lightBakeOccluder"))
			, m_featureNeighbors(ParseList<LevelTag>(queryElement, "featureNeighbor"))
			, m_translation(ParseVector(queryElement, "translation", SMath::ZeroTag()))
			, m_cellSize(ParseScalar<float>(queryElement, "cellSize", -1.0f))
			, m_samplingDistance(ParseScalar<float>(queryElement, "samplingDistance", -1.0f))
			, m_splitSize(ParseVector(queryElement, "splitSize", SMath::ZeroTag()))
			, m_shaderLod(ParseVector4(queryElement, "shaderLod", SMath::ZeroTag()))
			, m_shrubFadeStart(ParseScalar<float>(queryElement, "shrubFadeStart", 0.0f))
			, m_shrubFadeEnd(ParseScalar<float>(queryElement, "shrubFadeEnd", 0.0f))
			, m_runtimeLightsFile(queryElement.Value("runtimeLights").second)
			, m_bakedSceneFile(queryElement.Value("bakedScene").second)
			, m_populatorFile(queryElement.Value("populator").second)
			, m_coverActionPackCount(ParseScalar<int>(queryElement, "coverActionPackCount", 0))
			, m_useLightmaps(queryElement.Value("useLightmaps").first == true && queryElement.Value("useLightmaps").second == std::string("true"))
			, m_levelInfo(queryElement.Element("levelInfo"))
			, m_killGroups(queryElement.Value("killGroups").second)
			, m_lutsToLoad(queryElement.Value("lutsToLoad").second)
			, m_geometryCompression(queryElement.Value("geometryCompression").first == true ? queryElement.Value("geometryCompression").second : "default")
			, m_extraMaterialsList(queryElement.Element("extraMaterialsList"), std::string("material"))
			, m_materialReloadList(queryElement.Element("materialReloadList"), std::string("material"))
			, m_enableRealtimeReflections(queryElement.Value("enableRealtimeReflections").first == true && queryElement.Value("enableRealtimeReflections").second == std::string("true"))
			, m_enableBouncedLighting(queryElement.Value("enableBouncedLighting").first == true && queryElement.Value("enableBouncedLighting").second == std::string("true"))
			, m_enableClustering(queryElement.Value("enableClustering").first == false || queryElement.Value("enableClustering").second == std::string("true"))
			, m_cheatVis(queryElement.Element("cheatVis"))
			, m_shaderFeatures(queryElement.Element("shaderFeatures"), "shaderFeature")
			, m_lightingSettings(queryElement.Element("lightingSettings"))
			, m_materialRemapList(queryElement.Element("materialRemapList"), std::string("materialRemap"))
			, m_alternateResources(queryElement.Element("alternateResources"))
		{
			QueryElement::ReadGameFlags(m_gameFlagsInfo.m_gameFlags, m_gameFlagsInfo.m_gameFlagsPath, m_gameFlagsInfo.m_lastWriteTime, queryElement);
		}

	protected:
		virtual std::string Prefix() const{return  "Level." + ElementFacade::Prefix();}
	private:
		~Level() { }
	};

};