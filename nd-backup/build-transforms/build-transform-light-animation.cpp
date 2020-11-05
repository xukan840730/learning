/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Interactive Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "tools/pipeline3/build/build-transforms/build-transform-light-animation.h"
#include "tools/pipeline3/build/build-transforms/build-transform-skeleton.h"
#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"
#include "tools/pipeline3/build/build-transforms/build-transform-anim-stream.h"
#include "tools/pipeline3/build/build-transforms/build-transform-file-list.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/toolversion.h"
#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/libscene/bigscene.h"
#include "common/util/timer.h"
#include "common/render/lightdef.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "ice/src/tools/icelib/itscene/scenedb.h"
#include "ice/src/tools/icelib/itscene/ndbwriter.h"
#include "ice/src/tools/icelib/itscene/itjoint.h"
#include "ice/src/tools/icelib/itscene/itjointanim.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

extern std::string ExtraMaya3NDBOptions(const ToolParams &tool);

struct AnimatedLightSample
{
	float m_transX;
	float m_transY;
	float m_transZ;
	float m_dirX;
	float m_dirY;
	float m_dirZ;
	float m_upX;
	float m_upY;
	float m_upZ;
	float m_colorR;
	float m_colorG;
	float m_colorB;
	float m_coneAngle;
	float m_penumbraAngle;
	float m_shadowBlurScale;
	float m_shapeRadius;
	float m_volIntensity;
	float m_shadowSlope;
	float m_shadowConst;
	float m_nearClipDist;
	float m_farClipDist;
	float m_useRayTraceShadows;
	float m_decayRate;
	float m_startDistance;
	float m_startRange;
	float m_radius;
	float m_skipBgShadow;
	float m_emitDiffuse;
	float m_emitSpecular;
	float m_noLightBg;
	float m_noLightFg;
	float m_castsSss;
	float m_enableBounce;
	float m_minRoughness;
	float m_charOnly;
	float m_eyesOnly;
	float m_hairOnly;
	float m_particleOnly;
	float m_particle;
	float m_propOnly;
	float m_noHair;
	float m_noTransmittance;
	float m_specScale;
	float m_noLightChar;
	float m_projWidth;
	float m_projHeight;
	float m_goboUScrollSpeed;
	float m_goboVScrollSpeed;
	float m_goboUTileFactor;
	float m_goboVTileFactor;
	float m_goboAnimSpeed;
	float m_volInnerFalloff;
	float m_volInnerIntensity;
	float m_minShadowBlurScale;
	float m_maxShadowBlurDist;
	float m_variableShadowBlur;
};

struct AnimatedLightData
{
	std::string							m_name;
	LightType							m_type;
	U32									m_index;
	std::vector<AnimatedLightSample>	m_samples;	// only for the old way of cinematic light animation, will be removed later
};

const std::string UnderscoresToDashes(const std::string &path)
{
	std::string result = path;
	size_t underscorePos = result.find('_');
	while (underscorePos != std::string::npos)
	{
		result.replace(underscorePos, 1, "-");
		underscorePos = result.find('_', underscorePos);
	}

	return result;
}

void NdbRead(NdbStream &stream, const char *pSymName, ITSCENE::Light &light);

//--------------------------------------------------------------------------------------------------------------------//
static void AddAllLightAnimFloatAttributes(ITSCENE::SceneDb& lightSkelScene, ITSCENE::FloatAttributeNode* fan)
{
	AddFloatAttribute(lightSkelScene, fan, "colorR");
	AddFloatAttribute(lightSkelScene, fan, "colorG");
	AddFloatAttribute(lightSkelScene, fan, "colorB");
	AddFloatAttribute(lightSkelScene, fan, "coneAngle");
	AddFloatAttribute(lightSkelScene, fan, "penumbraAngle");
	AddFloatAttribute(lightSkelScene, fan, "shadowBlurScale");
	AddFloatAttribute(lightSkelScene, fan, "shapeRadius");
	AddFloatAttribute(lightSkelScene, fan, "volumetricIntensity");
	AddFloatAttribute(lightSkelScene, fan, "shadowSlope");
	AddFloatAttribute(lightSkelScene, fan, "shadowConst");
	AddFloatAttribute(lightSkelScene, fan, "nearClipDist");
	AddFloatAttribute(lightSkelScene, fan, "farClipDist");
	AddFloatAttribute(lightSkelScene, fan, "useRayTraceShadows");
	AddFloatAttribute(lightSkelScene, fan, "decayRate");
	AddFloatAttribute(lightSkelScene, fan, "startDistance");
	AddFloatAttribute(lightSkelScene, fan, "startRange");
	AddFloatAttribute(lightSkelScene, fan, "radius");
	AddFloatAttribute(lightSkelScene, fan, "skipBgShadow");
	AddFloatAttribute(lightSkelScene, fan, "emitDiffuse");
	AddFloatAttribute(lightSkelScene, fan, "emitSpecular");
	AddFloatAttribute(lightSkelScene, fan, "noLightBg");
	AddFloatAttribute(lightSkelScene, fan, "noLightFg");
	AddFloatAttribute(lightSkelScene, fan, "castsSss");
	AddFloatAttribute(lightSkelScene, fan, "enableBounce");
	AddFloatAttribute(lightSkelScene, fan, "minRoughness");
	AddFloatAttribute(lightSkelScene, fan, "charOnly");
	AddFloatAttribute(lightSkelScene, fan, "eyesOnly");
	AddFloatAttribute(lightSkelScene, fan, "hairOnly");
	AddFloatAttribute(lightSkelScene, fan, "particleOnly");
	AddFloatAttribute(lightSkelScene, fan, "particle");
	AddFloatAttribute(lightSkelScene, fan, "propOnly");
	AddFloatAttribute(lightSkelScene, fan, "noHair");
	AddFloatAttribute(lightSkelScene, fan, "noTransmittance");
	AddFloatAttribute(lightSkelScene, fan, "specScale");
	AddFloatAttribute(lightSkelScene, fan, "noLightChar");
	AddFloatAttribute(lightSkelScene, fan, "projWidth");
	AddFloatAttribute(lightSkelScene, fan, "projHeight");
	AddFloatAttribute(lightSkelScene, fan, "goboUScrollSpeed");
	AddFloatAttribute(lightSkelScene, fan, "goboVScrollSpeed");
	AddFloatAttribute(lightSkelScene, fan, "goboUTileFactor");
	AddFloatAttribute(lightSkelScene, fan, "goboVTileFactor");
	AddFloatAttribute(lightSkelScene, fan, "goboAnimSpeed");
	AddFloatAttribute(lightSkelScene, fan, "volInnerFalloff");
	AddFloatAttribute(lightSkelScene, fan, "volInnerIntensity");
	AddFloatAttribute(lightSkelScene, fan, "minShadowBlurScale");
	AddFloatAttribute(lightSkelScene, fan, "maxShadowBlurDist");
	AddFloatAttribute(lightSkelScene, fan, "variableShadowBlur");
}

//--------------------------------------------------------------------------------------------------------------------//
// only for the old way of cinematic light animation, will be removed later
static void GetLightSamples(const ITSCENE::LightAnimation* pLight, const Locator& originLoc, std::vector<AnimatedLightSample>& outSamples)
{
	if (!pLight->HasTracks(ITSCENE::kLatTranslateX, ITSCENE::kLatTranslateY, ITSCENE::kLatTranslateZ) ||
		!pLight->HasTracks(ITSCENE::kLatColorR, ITSCENE::kLatColorG, ITSCENE::kLatColorB) ||
		!pLight->HasTracks(ITSCENE::kLatDirectionX, ITSCENE::kLatDirectionY, ITSCENE::kLatDirectionZ))
	{
		return;
	}

	size_t samplesN = pLight->GetNumSamples((~(1ull << ITSCENE::kLatCount)));
	if (samplesN != 0)
	{
		outSamples.resize(samplesN);

		ITDATAMGR::FloatArray posX, posY, posZ;
		ITDATAMGR::FloatArray r, g, b;
		ITDATAMGR::FloatArray dirX, dirY, dirZ;
		ITDATAMGR::FloatArray upX, upY, upZ;
		ITDATAMGR::FloatArray coneAngle, penumbraAngle, shadowBlurScale, shapeRadius, volIntensity, shadowSlope,
			shadowConst, nearClipDist, farClipDist, useRayTraceShadows, decayRate, startDistance,
			startRange, radius, skipBgShadow, emitDiffuse, emitSpecular, noLightBg, noLightFg, noLightChar, castsSss, enableBounce, minRoughness, charOnly, eyesOnly, hairOnly, particleOnly, particle, propOnly, noHair, noTransmittance, specScale, 
			projWidth, projHeight, goboUScrollSpeed, goboVScrollSpeed, goboUTileFactor, goboVTileFactor, goboAnimSpeed, volInnerFalloff, volInnerIntensity, minShadowBlurScale, maxShadowBlurDist, variableShadowBlur;

		pLight->m_tracks[ITSCENE::kLatTranslateX]->GetSamples(posX); // CIN_TODO_LIGHT add code to handle the additional tracks for intensity, cone angle, radius, etc.
		pLight->m_tracks[ITSCENE::kLatTranslateY]->GetSamples(posY);
		pLight->m_tracks[ITSCENE::kLatTranslateZ]->GetSamples(posZ);

		pLight->m_tracks[ITSCENE::kLatColorR]->GetSamples(r);
		pLight->m_tracks[ITSCENE::kLatColorG]->GetSamples(g);
		pLight->m_tracks[ITSCENE::kLatColorB]->GetSamples(b);

		pLight->m_tracks[ITSCENE::kLatDirectionX]->GetSamples(dirX);
		pLight->m_tracks[ITSCENE::kLatDirectionY]->GetSamples(dirY);
		pLight->m_tracks[ITSCENE::kLatDirectionZ]->GetSamples(dirZ);

		pLight->m_tracks[ITSCENE::kLatUpX]->GetSamples(upX);
		pLight->m_tracks[ITSCENE::kLatUpY]->GetSamples(upY);
		pLight->m_tracks[ITSCENE::kLatUpZ]->GetSamples(upZ);

		pLight->m_tracks[ITSCENE::kLatConeAngle]->GetSamples(coneAngle);
		pLight->m_tracks[ITSCENE::kLatPenumbraAngle]->GetSamples(penumbraAngle);
		pLight->m_tracks[ITSCENE::kLatShadowBlurScale]->GetSamples(shadowBlurScale);
		pLight->m_tracks[ITSCENE::kLatShapeRadius]->GetSamples(shapeRadius);
		pLight->m_tracks[ITSCENE::kLatVolumetricIntensity]->GetSamples(volIntensity);
		pLight->m_tracks[ITSCENE::kLatShadowSlope]->GetSamples(shadowSlope);
		pLight->m_tracks[ITSCENE::kLatShadowConst]->GetSamples(shadowConst);
		pLight->m_tracks[ITSCENE::kLatNearClipDist]->GetSamples(nearClipDist);
		pLight->m_tracks[ITSCENE::kLatFarClipDist]->GetSamples(farClipDist);
		pLight->m_tracks[ITSCENE::kLatUseRayTraceShadows]->GetSamples(useRayTraceShadows);
		pLight->m_tracks[ITSCENE::kLatDecayRate]->GetSamples(decayRate);
		pLight->m_tracks[ITSCENE::kLatStartDistance]->GetSamples(startDistance);
		pLight->m_tracks[ITSCENE::kLatStartRange]->GetSamples(startRange);
		pLight->m_tracks[ITSCENE::kLatRadius]->GetSamples(radius);
		pLight->m_tracks[ITSCENE::kLatSkipBgShadow]->GetSamples(skipBgShadow);
		pLight->m_tracks[ITSCENE::kLatEmitDiffuse]->GetSamples(emitDiffuse);
		pLight->m_tracks[ITSCENE::kLatEmitSpecular]->GetSamples(emitSpecular);
		pLight->m_tracks[ITSCENE::kLatNoLightBg]->GetSamples(noLightBg);
		pLight->m_tracks[ITSCENE::kLatNoLightFg]->GetSamples(noLightFg);
		pLight->m_tracks[ITSCENE::kLatCastsSss]->GetSamples(castsSss);
		pLight->m_tracks[ITSCENE::kLatEnableBounce]->GetSamples(enableBounce);
		pLight->m_tracks[ITSCENE::kLatMinRoughness]->GetSamples(minRoughness);
		pLight->m_tracks[ITSCENE::kLatCharOnly]->GetSamples(charOnly);
		pLight->m_tracks[ITSCENE::kLatEyesOnly]->GetSamples(eyesOnly);
		pLight->m_tracks[ITSCENE::kLatHairOnly]->GetSamples(hairOnly);
		pLight->m_tracks[ITSCENE::kLatParticleOnly]->GetSamples(particleOnly);
		pLight->m_tracks[ITSCENE::kLatParticle]->GetSamples(particle);
		pLight->m_tracks[ITSCENE::kLatPropOnly]->GetSamples(propOnly);
		pLight->m_tracks[ITSCENE::kLatNoHair]->GetSamples(noHair);
		pLight->m_tracks[ITSCENE::kLatNoTransmittance]->GetSamples(noTransmittance);
		pLight->m_tracks[ITSCENE::kLatSpecScale]->GetSamples(specScale);
		pLight->m_tracks[ITSCENE::kLatNoLightChar]->GetSamples(noLightChar);
		pLight->m_tracks[ITSCENE::kLatProjWidth]->GetSamples(projWidth);
		pLight->m_tracks[ITSCENE::kLatProjHeight]->GetSamples(projHeight);
		pLight->m_tracks[ITSCENE::kLatGoboUScrollSpeed]->GetSamples(goboUScrollSpeed);
		pLight->m_tracks[ITSCENE::kLatGoboVScrollSpeed]->GetSamples(goboVScrollSpeed);
		pLight->m_tracks[ITSCENE::kLatGoboUTileFactor]->GetSamples(goboUTileFactor);
		pLight->m_tracks[ITSCENE::kLatGoboVTileFactor]->GetSamples(goboVTileFactor);
		pLight->m_tracks[ITSCENE::kLatGoboAnimSpeed]->GetSamples(goboAnimSpeed);
		pLight->m_tracks[ITSCENE::kLatVolInnerFalloff]->GetSamples(volInnerFalloff);
		pLight->m_tracks[ITSCENE::kLatVolInnerIntensity]->GetSamples(volInnerIntensity);
		pLight->m_tracks[ITSCENE::kLatMinShadowBlurScale]->GetSamples(minShadowBlurScale);
		pLight->m_tracks[ITSCENE::kLatMaxShadowBlurDist]->GetSamples(maxShadowBlurDist);
		pLight->m_tracks[ITSCENE::kLatVariableShadowBlur]->GetSamples(variableShadowBlur);

		for (size_t i = 0; i < samplesN; ++i)
		{
			AnimatedLightSample & sample = outSamples[i];

			Point transApSpace = originLoc.UntransformPoint(Point(posX[i], posY[i], posZ[i]));
			Vector dirApSpace = originLoc.UntransformVector(Normalize(Vector(dirX[i], dirY[i], dirZ[i])));
			Vector upApSpace = originLoc.UntransformVector(Normalize(Vector(upX[i], upY[i], upZ[i])));

			sample.m_transX = transApSpace.X();
			sample.m_transY = transApSpace.Y();
			sample.m_transZ = transApSpace.Z();

			sample.m_dirX = dirApSpace.X();
			sample.m_dirY = dirApSpace.Y();
			sample.m_dirZ = dirApSpace.Z();

			sample.m_upX = upApSpace.X();
			sample.m_upY = upApSpace.Y();
			sample.m_upZ = upApSpace.Z();

			sample.m_colorR = r[i];
			sample.m_colorG = g[i];
			sample.m_colorB = b[i];

			sample.m_coneAngle = coneAngle[i];
			sample.m_penumbraAngle = penumbraAngle[i];
			sample.m_shadowBlurScale = shadowBlurScale[i];
			sample.m_shapeRadius = shapeRadius[i];
			sample.m_volIntensity = volIntensity[i];
			sample.m_shadowSlope = shadowSlope[i];
			sample.m_shadowConst = shadowConst[i];
			sample.m_nearClipDist = nearClipDist[i];
			sample.m_farClipDist = farClipDist[i];
			sample.m_useRayTraceShadows = useRayTraceShadows[i];
			sample.m_decayRate = decayRate[i];
			sample.m_startDistance = startDistance[i];
			sample.m_startRange = startRange[i];
			sample.m_radius = radius[i];
			sample.m_skipBgShadow = skipBgShadow[i];
			sample.m_emitDiffuse = emitDiffuse[i];
			sample.m_emitSpecular = emitSpecular[i];
			sample.m_noLightBg = noLightBg[i];
			sample.m_noLightFg = noLightFg[i];
			sample.m_castsSss = castsSss[i];
			sample.m_enableBounce = enableBounce[i];
			sample.m_minRoughness = minRoughness[i];
			sample.m_charOnly = charOnly[i];
			sample.m_eyesOnly = eyesOnly[i];
			sample.m_hairOnly = hairOnly[i];
			sample.m_particleOnly = particleOnly[i];
			sample.m_particle = particle[i];
			sample.m_propOnly = propOnly[i];
			sample.m_noHair = noHair[i];
			sample.m_noTransmittance = noTransmittance[i];
			sample.m_specScale = specScale[i];
			sample.m_noLightChar = noLightChar[i];
			sample.m_projWidth = projWidth[i];
			sample.m_projHeight = projHeight[i];
			sample.m_goboUScrollSpeed = goboUScrollSpeed[i];
			sample.m_goboVScrollSpeed = goboVScrollSpeed[i];
			sample.m_goboUTileFactor = goboUTileFactor[i];
			sample.m_goboVTileFactor = goboVTileFactor[i];
			sample.m_goboAnimSpeed = goboAnimSpeed[i];
			sample.m_volInnerFalloff = volInnerFalloff[i];
			sample.m_volInnerIntensity = volInnerIntensity[i];
			sample.m_minShadowBlurScale = minShadowBlurScale[i];
			sample.m_maxShadowBlurDist = maxShadowBlurDist[i];
			sample.m_variableShadowBlur = variableShadowBlur[i];
		}
	}
}

//--------------------------------------------------------------------------------------------------------------------//
struct MatXT
{
	float data[4][4];
};

static void QuaternionFromMatrix(ITGEOM::Quat *quat, MatXT const* mtx)
{
	float trace = mtx->data[0][0] + mtx->data[1][1] + mtx->data[2][2];
	if (trace > 0.0f) {
		float s = 0.5f / sqrtf(1.0f + trace);
		quat->w = 0.25f / s;
		quat->x = s * (mtx->data[1][2] - mtx->data[2][1]);
		quat->y = s * (mtx->data[2][0] - mtx->data[0][2]);
		quat->z = s * (mtx->data[0][1] - mtx->data[1][0]);
	}
	else if (mtx->data[0][0] > mtx->data[1][1] && mtx->data[0][0] > mtx->data[2][2]) {
		float s = 0.5f / sqrtf(1.0f + mtx->data[0][0] - mtx->data[1][1] - mtx->data[2][2]);
		quat->x = 0.25f / s;
		quat->y = s * (mtx->data[1][0] + mtx->data[0][1]);
		quat->z = s * (mtx->data[2][0] + mtx->data[0][2]);
		quat->w = s * (mtx->data[1][2] - mtx->data[2][1]);
	}
	else if (mtx->data[1][1] > mtx->data[2][2]) {
		float s = 0.5f / sqrtf(1.0f + mtx->data[1][1] - mtx->data[0][0] - mtx->data[2][2]);
		quat->x = s * (mtx->data[1][0] + mtx->data[0][1]);
		quat->y = 0.25f / s;
		quat->z = s * (mtx->data[2][1] + mtx->data[1][2]);
		quat->w = s * (mtx->data[2][0] - mtx->data[0][2]);
	}
	else {
		float s = 0.5f / sqrtf(1.0f + mtx->data[2][2] - mtx->data[0][0] - mtx->data[1][1]);
		quat->x = s * (mtx->data[2][0] + mtx->data[0][2]);
		quat->y = s * (mtx->data[2][1] + mtx->data[1][2]);
		quat->z = 0.25f / s;
		quat->w = s * (mtx->data[0][1] - mtx->data[1][0]);
	}
}

//--------------------------------------------------------------------------------------------------------------------//
static void GetOriginLoc(const ITSCENE::SceneDb& scene, Locator* origin)
{
	int numApRefsFound = 0;
	Locator apRef;

	for (size_t i = 0; i < scene.m_transforms.size(); ++i)
	{
		const ITSCENE::Transform* pTrans = scene.m_transforms[i];
		const std::string* exportAlias = pTrans->m_propList.Value("exportAlias");
		if (exportAlias)
		{
			if (*exportAlias == "apReference")
			{
				// convert this guy.
				MatXT mat;
				ITGEOM::Quat q;

				memcpy(&mat, &pTrans->m_transformMatrix, sizeof(MatXT));
				QuaternionFromMatrix(&q, &mat);
				q.Normalize();
				ITGEOM::Vec4 v = pTrans->m_transformMatrix.GetRow(3);
				apRef.SetTranslation(Point(v.x, v.y, v.z));
				apRef.SetRotation(Quat(q.x, q.y, q.z, q.w));

				++numApRefsFound;
			}
		}
	}

	if (numApRefsFound != 1)
	{
		IWARN("%s apReference found in scene: Light animations will probably be wrong!", (numApRefsFound == 0) ? "No" : "More than one");
		apRef = Locator(SMath::kIdentity);
	}

	*origin = apRef;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ExportLightAnims::PopulatePreEvalDependencies()
{
	if (m_pContext->m_toolParams.m_baseMayaVer.size())
		m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);

	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_ANIM_EXPORT_VERSION);

	// Input settings
	m_preEvaluateDependencies.SetInt("startFrame", m_startFrame);
	m_preEvaluateDependencies.SetInt("endFrame", m_endFrame);
	// We can restrict which frames we actually sample the animations vs write out identity information for.
	// This is to speed up extraction of really long animations
	m_preEvaluateDependencies.SetInt("restrictedStartFrame", m_startFrame);
	m_preEvaluateDependencies.SetInt("restrictedEndFrame", m_endFrame);
	m_preEvaluateDependencies.SetInt("alignFirstFrame", m_startFrame);
	m_preEvaluateDependencies.SetInt("sampleRate", m_sampleRate);
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_ExportLightAnims::Evaluate()
{
	KickExportLightAnimsJob(m_pContext->m_buildScheduler.GetFarmSession(), m_pContext->m_toolParams);
	m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_maya3NdbJobId);

	return BuildTransformStatus::kResumeNeeded;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_LevelLightAnimDispatcher::PopulatePreEvalDependencies()
{
	if (m_pContext->m_toolParams.m_baseMayaVer.size())
		m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);

	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_ANIM_EXPORT_VERSION);
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_LevelLightAnimDispatcher::Evaluate()
{
	NdbStream lightAnimsNdbStream;
	DataStore::ReadData(GetFirstInput().m_file, lightAnimsNdbStream);

	ITSCENE::SceneDb * pLightAnimsScene = new ITSCENE::SceneDb;
	LoadSceneNdb(lightAnimsNdbStream, pLightAnimsScene);
	pLightAnimsScene->m_bigExt->m_jointUserAnimations[0]->m_targetPathString += ":|root";
	const U32 numFloatAttrs = pLightAnimsScene->m_floatAttributes.size() / pLightAnimsScene->m_floatAttributeNodes.size();

	for (auto lightIter = pLightAnimsScene->m_leLights.begin(); lightIter != pLightAnimsScene->m_leLights.end(); ++lightIter)
	{
		const ITSCENE::Light* pLight = *lightIter;
		float animEndFrame = 0.f;
		pLight->m_propList.GetFloat("ndi_anim_end_frame", animEndFrame);
		if (animEndFrame > 0.f)	// this light has animation, so add anim build transform
		{
			// first find its corresponding light joint
			for (U32 iJoint = 0; iJoint < pLightAnimsScene->m_joints.size(); iJoint++)
			{
				ITSCENE::Joint* pLightJoint = pLightAnimsScene->m_joints[iJoint];
				if (ITSCENE::LightToJointName(pLight->GetName()) == pLightJoint->m_name)
				{
					// first output this light's anim as a single joint ndb
					ITSCENE::SceneDb* pCurrScene = new ITSCENE::SceneDb;
					pCurrScene->Reset();
					pCurrScene->AddInfo("iceexport", "");

					for (U32 iTransform = 0; iTransform < pLightAnimsScene->m_transforms.size(); iTransform++)
					{
						ITSCENE::Transform* pTransform = new ITSCENE::Transform();
						*pTransform = *pLightAnimsScene->m_transforms[iTransform];
						pCurrScene->AddTransform(pTransform);
					}

					pLightJoint->m_name += ":|root";
					pCurrScene->AddJoint(pLightJoint);
					pLightAnimsScene->m_jointAnimations[iJoint]->m_targetPathString += ":|root";
					pCurrScene->m_jointAnimations.push_back(pLightAnimsScene->m_jointAnimations[iJoint]);

					pCurrScene->m_bigExt->m_jointUserAnimations.push_back(pLightAnimsScene->m_bigExt->m_jointUserAnimations[0]);
					pLightAnimsScene->m_floatAttributeNodes[iJoint]->m_dagpath += ":|root";
					pCurrScene->m_floatAttributeNodes.push_back(pLightAnimsScene->m_floatAttributeNodes[iJoint]);
					for (UINT iAttr = 0; iAttr < numFloatAttrs; iAttr++)
					{
						pCurrScene->m_floatAttributes.push_back(pLightAnimsScene->m_floatAttributes[iJoint * numFloatAttrs + iAttr]);
						size_t dotPos = pLightAnimsScene->m_floatAttributeAnims[iJoint * numFloatAttrs + iAttr]->m_targetPathString.find('.');
						pLightAnimsScene->m_floatAttributeAnims[iJoint * numFloatAttrs + iAttr]->m_targetPathString = pLightAnimsScene->m_floatAttributeAnims[iJoint * numFloatAttrs + iAttr]->m_targetPathString.substr(0, dotPos) + ":|root" + pLightAnimsScene->m_floatAttributeAnims[iJoint * numFloatAttrs + iAttr]->m_targetPathString.substr(dotPos);
						pCurrScene->m_floatAttributeAnims.push_back(pLightAnimsScene->m_floatAttributeAnims[iJoint * numFloatAttrs + iAttr]);
					}

					const std::string lightObjName = UnderscoresToDashes(pLight->GetName().substr(pLight->GetName().find_last_of('|') + 1));
					if (HasOutput(lightObjName))
					{
						NdbStream animStream;
						bool bExportedAnim = SaveSceneNdb(GetOutputPath(lightObjName).AsAbsolutePath(), pCurrScene, animStream);
						animStream.Close();
						DataStore::WriteData(GetOutput(lightObjName).m_path, animStream);
					}

					break;
				}
			}
		}
	}

	return BuildTransformStatus::kOutputsUpdated;
}

//--------------------------------------------------------------------------------------------------------------------//
static inline const std::string ToLightSkelName(const std::string& lightSkelSceneFileName)
{
	return lightSkelSceneFileName.substr(lightSkelSceneFileName.find_last_of('/') + 1, lightSkelSceneFileName.find(".ma") - lightSkelSceneFileName.find_last_of('/') - 1);
}

static inline const std::string ToRigInfoNdbPath(const std::string& lightSkelSceneFileName)
{
	return lightSkelSceneFileName.substr(0, lightSkelSceneFileName.find(".ma")) + FileIO::separator + "light-riginfo.ndb";
}

//--------------------------------------------------------------------------------------------------------------------//
static void AddLightSkelTransforms(const BuildTransformContext *const pContext, const std::string& lightSkelSceneFileName, const std::string& lightSkelName, const std::string& lightRigInfoNdbPath, BuildTransform_FileList* pLightAnimFileListXform, const TransformInput& lightSkelNdb)
{
	// next build the light-skel rig info
	BuildTransform_BuildRigInfo* pLightSkelBuildRigInfoXform = new BuildTransform_BuildRigInfo(pContext, lightSkelSceneFileName + ".light-skel");
	pLightSkelBuildRigInfoXform->SetInput(lightSkelNdb);
	pLightSkelBuildRigInfoXform->SetOutput(TransformOutput(lightRigInfoNdbPath, "RigInfo"));
	pContext->m_buildScheduler.AddBuildTransform(pLightSkelBuildRigInfoXform, pContext);

	// next build the light-skel
	BuildTransform_BuildSkel* pBuildSkelXform = new BuildTransform_BuildSkel(pContext, lightSkelSceneFileName, "light-skel", lightSkelSceneFileName.substr(0, lightSkelSceneFileName.find(".ma")));
	pBuildSkelXform->m_skeletonName = lightSkelName;
	std::vector<TransformInput> inputs;
	inputs.push_back(lightSkelNdb);
	inputs.push_back(TransformInput(lightRigInfoNdbPath, "RigInfo"));
	pBuildSkelXform->SetInputs(inputs);
	const std::string lightSkelBoPath = pContext->m_toolParams.m_buildPathSkelBo + lightSkelSceneFileName.substr(0, lightSkelSceneFileName.find(".ma")) + FileIO::separator + "light-skel.bo";
	pBuildSkelXform->SetOutput(TransformOutput(lightSkelBoPath, "SkelBo"));
	pContext->m_buildScheduler.AddBuildTransform(pBuildSkelXform, pContext);
	pLightAnimFileListXform->AddInput(TransformInput(lightSkelBoPath));

	// build light skel debug info
	BuildTransform_BuildDebugSkelInfo* pBuildDebugSkelInfoXform = new BuildTransform_BuildDebugSkelInfo(pContext, lightSkelName, true);
	pBuildDebugSkelInfoXform->AddInput(lightSkelNdb);
	pBuildDebugSkelInfoXform->AddInput(TransformInput(lightRigInfoNdbPath, "RigInfo"));
	const std::string lightSkelDebugInfoBoFilename = pContext->m_toolParams.m_buildPathSkelDebugInfoBo + lightSkelSceneFileName.substr(0, lightSkelSceneFileName.find(".ma")) + ".light-skeldebuginfo.bo";
	pBuildDebugSkelInfoXform->AddOutput(TransformOutput(lightSkelDebugInfoBoFilename, "SkelInfoBo"));
	pContext->m_buildScheduler.AddBuildTransform(pBuildDebugSkelInfoXform, pContext);
	pLightAnimFileListXform->AddInput(TransformInput(lightSkelDebugInfoBoFilename));
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_LevelLightAnimTransformSpawner::Evaluate()
{
	BuildFile lightNdb = GetInputFile("LightNdb");
	NdbStream stream;
	ITSCENE::SceneDb srcScene;

	DataStore::ReadData(lightNdb, stream);
	NdbRead(stream, "m_leLights", srcScene.m_leLights);

	BuildTransform_LevelLightAnimDispatcher* pLevelLightAnimDispatcher = new BuildTransform_LevelLightAnimDispatcher(m_pContext);

	float maxLightAnimEndFrame = 0.f;
	const libdb2::Actor* plightSkelActor = libdb2::GetActor("light-skel", false);
	const std::string lightSkelNdbPath = m_pContext->m_toolParams.m_buildPathSkel + plightSkelActor->FullNameNoLod() + FileIO::separator + "skel.ndb";
	const std::string lightRigInfoNdbPath = m_pContext->m_toolParams.m_buildPathRigInfo + plightSkelActor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
	const std::string lightAnimFileListPath = m_pContext->m_toolParams.m_buildPathFileList + m_levelName + ".light-anim.a";
	BuildTransform_FileList* pLightAnimFileListXform = new BuildTransform_FileList(lightAnimFileListPath);

	std::vector<std::string> animNames;
	std::vector<std::string> animTagNames;
	SkeletonId skelId = INVALID_SKELETON_ID;

	std::vector<ITSCENE::Light*>::const_iterator lightIter;
	for (lightIter = srcScene.m_leLights.begin(); lightIter != srcScene.m_leLights.end(); ++lightIter)
	{
		const ITSCENE::Light* pLight = *lightIter;
		if (pLight->GetName().find("runtime_lights", 0) == 0)
		{
			float animEndFrame = 0.f;
			pLight->m_propList.GetFloat("ndi_anim_end_frame", animEndFrame);
			if (animEndFrame > maxLightAnimEndFrame)
				maxLightAnimEndFrame = animEndFrame;

			if (animEndFrame > 0.f)	// this light has animation, add to spawner output
			{
				const std::string lightObjName = UnderscoresToDashes(pLight->GetName().substr(pLight->GetName().find_last_of('|') + 1));
				const std::string ndbFileName = m_pContext->m_toolParams.m_buildPathAnim + m_levelName + FileIO::separator + lightObjName + "." + ToString(m_pContext->m_toolParams.m_defaultAnimSampleRate) + "fps.ndb";
				pLevelLightAnimDispatcher->AddOutput(TransformOutput(ndbFileName, lightObjName));

				// now add the light anim build transform
				int32_t startFrame = 1;
				int32_t endFrame = animEndFrame;
				U32 sampleRate = m_pContext->m_toolParams.m_defaultAnimSampleRate;
				const std::string animName = m_levelName + "--" + lightObjName;

				AnimBuildData* pAnimBuildData = new AnimBuildData(plightSkelActor->TypedDbPath());
				pAnimBuildData->m_startFrame = 0;
				pAnimBuildData->m_endFrame = endFrame - startFrame;
				const std::string animBoFilename = m_pContext->m_toolParams.m_buildPathAnimBo + animName + "." + ToString(sampleRate) + "fps.bo";
				pAnimBuildData->m_animBoFilename = animBoFilename;
				pAnimBuildData->m_ignoreSkelDebugInfo = false;

				CommonAnimData* pCommonData = new CommonAnimData;
				pCommonData->m_animName = animName;
				pCommonData->m_exportNamespace = "";
				pCommonData->m_skelId = GetSkelId(*plightSkelActor);
				pCommonData->m_sampleRate = sampleRate;
				pCommonData->m_isAdditive = false;
				pCommonData->m_isStreaming = false;
				pCommonData->m_isLooping = true;
				pCommonData->m_generateCenterOfMass = false;
				pCommonData->m_jointCompression = "minimal";
				pCommonData->m_channelCompression = "none";
				
				animNames.push_back(pCommonData->m_animName);
				animTagNames.push_back(plightSkelActor->Name() + FileIO::separator + pCommonData->m_animName);
				skelId = pCommonData->m_skelId;

				BuildTransform_BuildAnim* pBuildLightAnimXform = new BuildTransform_BuildAnim(m_pContext, pAnimBuildData, pCommonData);
				pBuildLightAnimXform->AddInput(TransformInput(lightSkelNdbPath, "SkelNdb"));
				pBuildLightAnimXform->AddInput(TransformInput(lightRigInfoNdbPath, "RigInfo"));
				pBuildLightAnimXform->AddInput(TransformInput(ndbFileName, "AnimNdb"));
				pBuildLightAnimXform->AddOutput(TransformOutput(animBoFilename, "AnimBo"));
				AddBuildTransform(pBuildLightAnimXform);
				pLightAnimFileListXform->AddInput(TransformInput(animBoFilename));
			}
		}
	}

	if (maxLightAnimEndFrame > 0.f)	// this level has light animation, spawn the transforms
	{
		std::string lightsMayaFile;
		NdbRead(stream, "m_lightsMayaFile", lightsMayaFile);

		BuildTransform_ExportLightAnims* pExportLightAnimsXform = new BuildTransform_ExportLightAnims(m_pContext, 1, maxLightAnimEndFrame, m_pContext->m_toolParams.m_defaultAnimSampleRate, true);
		pExportLightAnimsXform->SetInput(TransformInput(PathPrefix::BAM + lightsMayaFile));
		std::string lightAnimsExportFilename = m_pContext->m_toolParams.m_buildPathAnim + m_levelName + "." + ToString(m_pContext->m_toolParams.m_defaultAnimSampleRate) + "fps.ndb";
		pExportLightAnimsXform->SetOutput(TransformOutput(lightAnimsExportFilename));
		AddBuildTransform(pExportLightAnimsXform);

		pLevelLightAnimDispatcher->SetInput(pExportLightAnimsXform->GetFirstOutput());
		AddBuildTransform(pLevelLightAnimDispatcher);

		BuildTransform_ExportSkel* pLightSkelExportXform = new BuildTransform_ExportSkel(m_pContext, plightSkelActor->FullNameNoLod(), plightSkelActor->m_skeleton.m_skelExportNamespace, plightSkelActor->m_skeleton.m_set);
		pLightSkelExportXform->m_skeletonName = plightSkelActor->Name();
		pLightSkelExportXform->m_skeletonFullName = plightSkelActor->m_skeleton.FullName();
		pLightSkelExportXform->SetInput(TransformInput(PathPrefix::BAM + plightSkelActor->m_skeleton.m_sceneFile));
		pLightSkelExportXform->SetOutput(TransformOutput(lightSkelNdbPath));
		AddBuildTransform(pLightSkelExportXform);

		// next build the light-skel rig info
		BuildTransform_BuildRigInfo* pLightSkelBuildRigInfoXform = new BuildTransform_BuildRigInfo(m_pContext, plightSkelActor->BaseName(), plightSkelActor->m_lodIndex);
		pLightSkelBuildRigInfoXform->m_skeletonName = plightSkelActor->Name();
		pLightSkelBuildRigInfoXform->SetInput(TransformInput(lightSkelNdbPath, "SkelNdb"));
		pLightSkelBuildRigInfoXform->SetOutput(TransformOutput(lightRigInfoNdbPath, "RigInfo"));
		AddBuildTransform(pLightSkelBuildRigInfoXform);

		// build light skel debug info
		BuildTransform_BuildDebugSkelInfo* pBuildDebugSkelInfoXform = new BuildTransform_BuildDebugSkelInfo(m_pContext, plightSkelActor->Name());
		pBuildDebugSkelInfoXform->AddInput(TransformInput(lightSkelNdbPath, "SkelNdb"));
		pBuildDebugSkelInfoXform->AddInput(TransformInput(lightRigInfoNdbPath, "RigInfo"));
		const std::string lightSkelDebugInfoBoFilename = m_pContext->m_toolParams.m_buildPathSkelDebugInfoBo + plightSkelActor->Name() + ".skeldebuginfo.bo";
		pBuildDebugSkelInfoXform->AddOutput(TransformOutput(lightSkelDebugInfoBoFilename, "SkelInfoBo"));
		AddBuildTransform(pBuildDebugSkelInfoXform);
		pLightAnimFileListXform->AddInput(TransformInput(lightSkelDebugInfoBoFilename));
	}

	BuildTransform_AnimGroup *const pAnimGroup = new BuildTransform_AnimGroup(m_pContext,
		m_levelName,
		skelId,
		animNames,
		animTagNames,
		true);
	const std::string animGroupBoPath = m_pContext->m_toolParams.m_buildPathAnimGroupBo + m_levelName + ".light-anims.bo";
	pAnimGroup->SetOutput(TransformOutput(animGroupBoPath, "BoFile"));
	AddBuildTransform(pAnimGroup);
	pLightAnimFileListXform->AddInput(TransformInput(animGroupBoPath));

	AddBuildTransform(pLightAnimFileListXform);	// always add even if no light anim, so the pak transform can find the file list input
	DataStore::WriteData(GetOutputPath("DummyOutput"), "");

	return BuildTransformStatus::kOutputsUpdated;
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_ExportLightAnims::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	const std::vector<std::pair<std::string, int>> loadedReferences = ExtractLoadedReferencesFromMaya3ndbOutput(m_pContext->m_buildScheduler, m_maya3NdbJobId);

	// Add the loaded references
	const std::string& fsRoot = m_pContext->m_toolParams.m_fsRoot;
	for (const auto& ref : loadedReferences)
	{
		const BuildPath refPath(fsRoot + FileIO::separator + ref.first);
		RegisterDiscoveredDependency(refPath, ref.second);
	}

	return BuildTransformStatus::kOutputsUpdated;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ExportLightAnims::KickExportLightAnimsJob(Farm& farm, const ToolParams& tool)
{
	AutoTimer submitAnimJobsTimer("KickExportLightAnimsJob");

	const BuildFile& sceneInputFilename = GetInputs()[0].m_file;
	std::string relativeFilename = sceneInputFilename.AsRelativePath();
	std::string options = relativeFilename + " ";
	options += " -lightanimsonly ";

	char tmp[1024];
	sprintf(tmp, " -startframe %i", m_startFrame);
	options += std::string(tmp);
	sprintf(tmp, " -endframe %i", m_endFrame);
	options += std::string(tmp);
	sprintf(tmp, " -restrictedstartframe %i", m_startFrame);
	options += std::string(tmp);
	sprintf(tmp, " -restrictedendframe %i", m_endFrame);
	options += std::string(tmp);
	sprintf(tmp, " -alignfirstframe %i", m_startFrame);
	options += std::string(tmp);
	// Speed up multiple executions on the same host if all of them want to download the same file
	options += " -cachesource ";
	sprintf(tmp, " -samplerate %i", m_sampleRate);
	options += std::string(tmp);
	if (!m_isLevel)
	{
		options += " -skelset ";
		options += "skeleton";
	}
	options += " -user " + tool.m_userName + " ";
	options += " -host " + tool.m_host + " ";
	if (tool.m_local)
		options += " -local ";

	std::string dbPath = toolsutils::GetGlobalDbPath(tool.m_host);
	options += " -dbpath " + dbPath;

	const TransformOutput& ndbOutput = GetFirstOutput();
	options += " -output " + ndbOutput.m_path.AsAbsolutePath();
	options += ExtraMaya3NDBOptions(tool);

	IASSERT(!tool.m_executablesPath.empty());
	const std::string exePath = tool.m_executablesPath + "/maya3ndb.app/" + "maya3ndb.exe";

	JobDescription job(exePath, options, tool.m_localtools, tool.m_local, tool.m_x64maya);
	job.m_useSetCmdLine = true;

	m_maya3NdbJobId = farm.submitJob(job.GetCommand(), 5ULL * 1024LL * 1024ULL * 1024ULL, 1);	// yes 5Gb it's been clocked on the farm unfortunately and it dies when building too many ot thos simultaneously
}

const libdb2::Anim* BuildTransform_LightAnimsBuildSpawner::SceneNdbToAnimDb(const std::string& ndbPath, const libdb2::Actor* pDbActor) const
{
	const libdb2::AnimList& dbanimList = pDbActor->m_animations;
	const std::string expectedAnimName = ndbPath.substr(m_pContext->m_toolParams.m_buildPathAnim.length(), ndbPath.find_first_of('.') - m_pContext->m_toolParams.m_buildPathAnim.length());

	for (const libdb2::Anim* pAnim : dbanimList)
	{
		if (pDbActor->m_sequence.Loaded() || pAnim->m_flags.m_exportLightAnim)
		{
			if (expectedAnimName == pAnim->GetLightAnimName(pDbActor))
				return pAnim;
		}
	}

	IABORT("Could not find the scene file for light animation %s.", expectedAnimName.c_str());
	return dbanimList[0];
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_LightAnimsBuildSpawner::Evaluate()
{
	struct AnimGroupInfo
	{
		SkeletonId m_skelId;
		std::vector<std::string> m_animNames;
		std::vector<std::string> m_animTagNames;
	};

	const std::string lightAnimFileListPath = m_pContext->m_toolParams.m_buildPathFileList + m_actorName + ".light-anim.a";
	BuildTransform_FileList* pLightAnimFileListXform = new BuildTransform_FileList(lightAnimFileListPath);

	std::map<std::string, AnimGroupInfo> sceneFileToAnimGroupInfo;	// the animation scene file name uniquely identifies each light skel
	const libdb2::Actor* pDbActor = libdb2::GetActor(m_actorName);
	std::vector<StreamBoEntry> streamBoList;
	std::string actorTxt;
	std::vector<pipeline3::LightBoTexture> lightTextures;
	BigStreamWriter lightsStream(m_pContext->m_toolParams.m_streamConfig);
	BigWriter::NameTable nameTable("LIGHTS");

	for (const auto& input : GetInputs())
	{
		NdbStream lightAnimsNdbStream;
		DataStore::ReadData(input.m_file, lightAnimsNdbStream);

		ITSCENE::SceneDb * pLightAnimsScene = new ITSCENE::SceneDb;
		LoadSceneNdb(lightAnimsNdbStream, pLightAnimsScene);

		// figure out the animation scene file name
		const libdb2::Anim* pLightExpAnim = SceneNdbToAnimDb(input.m_file.AsAbsolutePath(), pDbActor);
		const std::string lightSkelSceneFileName = pLightExpAnim->GetLightSkelSceneFileName(pDbActor);

		const std::string lightSkelName = ToLightSkelName(lightSkelSceneFileName);
		const std::string lightRigInfoNdbPath = m_pContext->m_toolParams.m_buildPathRigInfo + ToRigInfoNdbPath(lightSkelSceneFileName);

		auto animSceneFileIT = sceneFileToAnimGroupInfo.find(lightSkelSceneFileName);
		if (animSceneFileIT == sceneFileToAnimGroupInfo.end())
		{
			animSceneFileIT = sceneFileToAnimGroupInfo.insert(std::make_pair(lightSkelSceneFileName, AnimGroupInfo())).first;
			animSceneFileIT->second.m_skelId = ComputeSkelId(lightSkelSceneFileName + ".light-skel");

			ITSCENE::SceneDb lightSkelScene;
			lightSkelScene.Reset();
			lightSkelScene.AddInfo("iceexport", "");
			lightSkelScene.AddInfo("light-skel", "");

			// only build runtime light joint anims when there is any
			if (pLightAnimsScene->m_joints.size() > 0)	// this array only contains joints generated from runtime lights
			{
				for (U32 iJoint = 0; iJoint < pLightAnimsScene->m_joints.size(); iJoint++)
				{
					lightSkelScene.AddJoint(pLightAnimsScene->m_joints[iJoint]);
					ITSCENE::FloatAttributeNode *fan = new ITSCENE::FloatAttributeNode(pLightAnimsScene->m_joints[iJoint]->m_name.c_str());
					lightSkelScene.m_floatAttributeNodes.push_back(fan);

					AddAllLightAnimFloatAttributes(lightSkelScene, fan);
				}

				AddLightSkelTransforms(m_pContext, lightSkelSceneFileName, lightSkelName, lightRigInfoNdbPath, pLightAnimFileListXform, TransformInput(GetOutput(lightSkelSceneFileName).m_path, "SkelNdb"));
			}

			// always export the light-skel
			NdbStream lightSkelStream;
			bool bExportedLightSkel = SaveSceneNdb(GetOutput(lightSkelSceneFileName).m_path.AsAbsolutePath(), &lightSkelScene, lightSkelStream);
			lightSkelStream.Close();
			DataStore::WriteData(GetOutput(lightSkelSceneFileName).m_path, lightSkelStream);

			// gather the lights to the light table and their animations
			U32 numPointLights = 0;
			U32 numDirLights = 0;
			U32 numSpotLights = 0;
			LightCopier lightCopier;
			lightCopier.m_runtimeLightTable.m_filename = lightSkelSceneFileName + ".light-skel";
			Locator	originLoc;
			std::vector<AnimatedLightData> animatedLights;

			GetOriginLoc(*pLightAnimsScene, &originLoc);

			for (auto lightIter = pLightAnimsScene->m_leLights.begin(); lightIter != pLightAnimsScene->m_leLights.end(); ++lightIter)
			{
				const ITSCENE::Light* pLight = *lightIter;
				const std::string &name = pLight->GetName();

				AnimatedLightData lightData;
				lightData.m_name = ITSCENE::StripNamespace(name);

				switch (pLight->GetKind())
				{
				case ITSCENE::kIsPoint:
					lightData.m_type = kPointLight;
					lightData.m_index = numPointLights;
					numPointLights++;
					break;
				case ITSCENE::kIsDirectional:
					continue;	// we don't support direction at all
					break;
				case ITSCENE::kIsProjector:	// for unkonwn historical reasons, dir light and proj light were confused for a while
					lightData.m_type = kDirectionalLight;
					lightData.m_index = numDirLights;
					numDirLights++;
					break;
				case ITSCENE::kIsSpot:
					lightData.m_type = kSpotLight;
					lightData.m_index = numSpotLights;
					numSpotLights++;
					break;
				default:
					IERR("GatherLights() - unsupported light %s\n", name.c_str());
					continue;
					break;
				};

				if (pDbActor->m_sequence.Loaded())
				{
					// find animation, only for the old way of cinematic light animation, will be removed later
					const ITSCENE::LightAnimationList & lightAnimations = pLightAnimsScene->m_bigExt->m_lightAnimations;
					for (auto iLight = lightAnimations.begin(); iLight != lightAnimations.end(); ++iLight)
					{
						const ITSCENE::LightAnimation* pLightAnim = *iLight;
						if (pLightAnim->m_targetPathString == name)
						{
							GetLightSamples(pLightAnim, originLoc, lightData.m_samples);
							break;
						}
					}
				}

				animatedLights.push_back(lightData);
				lightCopier.AddLightNoTransform(pLightAnimsScene, pLight);
			}

			CollectLightTextures(lightTextures, lightCopier.m_runtimeLightTable);

			// output the light table
			BigWriter::WriteLightTable(lightsStream, lightCopier.m_runtimeLightTable, nameTable);

			std::vector<Location> nameLocs;
			std::vector<Location> sampleLocs;

			// output the animated light structures
			if (animatedLights.size())
			{
				BigStreamWriter::Item* pItem = lightsStream.StartItem(BigWriter::ANIMATED_LIGHTS);
				lightsStream.AddLoginItem(pItem, BigWriter::LIGHT_PRIORITY);

				for (size_t iLight = 0; iLight < animatedLights.size(); ++iLight)
				{
					const AnimatedLightData& lightData = animatedLights[iLight];

					lightsStream.Align(kAlignDefaultLink); // AnimatedLight on game side
					Location nameLoc = lightsStream.WritePointer(); // m_pStrName
					nameLocs.push_back(nameLoc);

					lightsStream.Write4U((U32)lightData.m_type); // m_type
					lightsStream.Write4U((U32)lightData.m_index); // m_index

					lightsStream.Align(kAlignDefaultLink);
					lightsStream.WriteStringId64(StringToStringId64(lightCopier.m_runtimeLightTable.m_filename.c_str(), true)); // m_lightTableName

					lightsStream.Write4U(pDbActor->m_sequence.Loaded() ? (U32)lightData.m_samples.size() : 0); // m_numSamples
				}

				for (size_t iLight = 0; iLight < animatedLights.size(); ++iLight)
				{
					lightsStream.SetPointer(nameLocs[iLight]);
					lightsStream.WriteStr(animatedLights[iLight].m_name.c_str());
				}
				lightsStream.EndItem();

				// only for the old way of cinematic light animation, will be removed later
				if (/*pDbActor->m_sequence.Loaded()*/false)
				{
					for (size_t iLight = 0; iLight < animatedLights.size(); ++iLight)
					{
						lightsStream.StartItem();
						lightsStream.Align(kAlignDefaultLink);
						lightsStream.SetPointer(sampleLocs[iLight]);

						const AnimatedLightData& lightData = animatedLights[iLight];
						// dump the samples
						for (size_t j = 0; j < lightData.m_samples.size(); j++)
						{
							const AnimatedLightSample& sample = lightData.m_samples[j]; // AnimatedLightSample on game side

							lightsStream.WriteF(sample.m_transX); // m_trans[3]
							lightsStream.WriteF(sample.m_transY);
							lightsStream.WriteF(sample.m_transZ);
							lightsStream.WriteF(sample.m_dirX);	// m_dir[3]
							lightsStream.WriteF(sample.m_dirY);
							lightsStream.WriteF(sample.m_dirZ);
							lightsStream.WriteF(sample.m_upX);	// m_up[3]
							lightsStream.WriteF(sample.m_upY);
							lightsStream.WriteF(sample.m_upZ);
							lightsStream.WriteF(sample.m_colorR);	// m_color[3]
							lightsStream.WriteF(sample.m_colorG);
							lightsStream.WriteF(sample.m_colorB);
							lightsStream.WriteF(sample.m_coneAngle);
							lightsStream.WriteF(sample.m_penumbraAngle);
							lightsStream.WriteF(sample.m_shadowBlurScale);
							lightsStream.WriteF(sample.m_shapeRadius);
							lightsStream.WriteF(sample.m_volIntensity);
							lightsStream.WriteF(sample.m_shadowSlope);
							lightsStream.WriteF(sample.m_shadowConst);
							lightsStream.WriteF(sample.m_nearClipDist);
							lightsStream.WriteF(sample.m_farClipDist);
							lightsStream.WriteF(sample.m_useRayTraceShadows);
							lightsStream.WriteF(sample.m_decayRate);
							lightsStream.WriteF(sample.m_startDistance);
							lightsStream.WriteF(sample.m_startRange);
							lightsStream.WriteF(sample.m_radius);
							lightsStream.WriteF(sample.m_skipBgShadow);
							lightsStream.WriteF(sample.m_emitDiffuse);
							lightsStream.WriteF(sample.m_emitSpecular);
							lightsStream.WriteF(sample.m_noLightBg);
							lightsStream.WriteF(sample.m_noLightFg);
							lightsStream.WriteF(sample.m_castsSss);
							lightsStream.WriteF(sample.m_enableBounce);
							lightsStream.WriteF(sample.m_minRoughness);
							lightsStream.WriteF(sample.m_charOnly);
							lightsStream.WriteF(sample.m_eyesOnly);
							lightsStream.WriteF(sample.m_hairOnly);
							lightsStream.WriteF(sample.m_particleOnly);
							lightsStream.WriteF(sample.m_particle);
							lightsStream.WriteF(sample.m_propOnly);
							lightsStream.WriteF(sample.m_noHair);
							lightsStream.WriteF(sample.m_noTransmittance);
							lightsStream.WriteF(sample.m_specScale);
							lightsStream.WriteF(sample.m_noLightChar);
							lightsStream.WriteF(sample.m_projWidth);
							lightsStream.WriteF(sample.m_projHeight);
							lightsStream.WriteF(sample.m_goboUScrollSpeed);
							lightsStream.WriteF(sample.m_goboVScrollSpeed);
							lightsStream.WriteF(sample.m_goboUTileFactor);
							lightsStream.WriteF(sample.m_goboVTileFactor);
							lightsStream.WriteF(sample.m_goboAnimSpeed);
							lightsStream.WriteF(sample.m_volInnerFalloff);
							lightsStream.WriteF(sample.m_volInnerIntensity);
							lightsStream.WriteF(sample.m_minShadowBlurScale);
							lightsStream.WriteF(sample.m_maxShadowBlurDist);
							lightsStream.WriteF(sample.m_variableShadowBlur);
						}

						lightsStream.EndItem();
					}
				}
			}
		}

		if (pLightAnimsScene->m_joints.size() > 0)
		{
			// now build the light anim
			int32_t startFrame = (I32)pLightExpAnim->m_animationStartFrame.m_value;
			int32_t endFrame = (I32)pLightExpAnim->m_animationEndFrame.m_value;
			U32 sampleRate = GetAnimSampleRate(*pLightExpAnim, m_pContext->m_toolParams.m_defaultAnimSampleRate);
			const std::string animName = pLightExpAnim->GetLightAnimName(pDbActor);

			AnimBuildData* pAnimBuildData = new AnimBuildData("/" + lightSkelName + ".actor.xml");	// fake a builder path for the light skel actor since there's none
			pAnimBuildData->m_startFrame = 0;
			pAnimBuildData->m_endFrame = endFrame - startFrame;
			const std::string animBoFilename = m_pContext->m_toolParams.m_buildPathAnimBo + animName + "." + ToString(sampleRate) + "fps.bo";
			pAnimBuildData->m_animBoFilename = animBoFilename;
			pAnimBuildData->m_ignoreSkelDebugInfo = false;

			CommonAnimData* pCommonData = new CommonAnimData;
			pCommonData->m_animName = animName;
			pCommonData->m_exportNamespace = "";
			pCommonData->m_skelId = animSceneFileIT->second.m_skelId;
			pCommonData->m_sampleRate = sampleRate;
			pCommonData->m_isAdditive = false;
			pCommonData->m_isStreaming = pLightExpAnim->m_flags.m_Streaming;
			pCommonData->m_isLooping = pLightExpAnim->m_flags.m_Looping;
			pCommonData->m_generateCenterOfMass = false;
			pCommonData->m_jointCompression = "minimal";
			pCommonData->m_channelCompression = "none";
			if (pCommonData->m_isStreaming)
				pAnimBuildData->m_headerOnly = true;

			animSceneFileIT->second.m_animNames.push_back(pCommonData->m_animName);
			animSceneFileIT->second.m_animTagNames.push_back(lightSkelName + FileIO::separator + pCommonData->m_animName);

			BuildTransform_BuildAnim* pBuildLightAnimXform = new BuildTransform_BuildAnim(m_pContext, pAnimBuildData, pCommonData);
			pBuildLightAnimXform->AddInput(TransformInput(GetOutput(lightSkelSceneFileName).m_path, "SkelNdb"));
			pBuildLightAnimXform->AddInput(TransformInput(lightRigInfoNdbPath, "RigInfo"));
			const std::string animationExportFilename = m_pContext->m_toolParams.m_buildPathAnim + animName + "." + ToString(sampleRate) + "fps.ndb";
			pBuildLightAnimXform->AddInput(TransformInput(animationExportFilename, "AnimNdb"));
			pBuildLightAnimXform->AddOutput(TransformOutput(animBoFilename, "AnimBo"));
			AddBuildTransform(pBuildLightAnimXform);
			pLightAnimFileListXform->AddInput(TransformInput(animBoFilename));

			// streaming
			if (pCommonData->m_isStreaming)
			{
				const int numChunks = (endFrame - startFrame + kNumFramesPerStreamingChunk - 1) / kNumFramesPerStreamingChunk;
				for (int chunkIndex = 0; chunkIndex < numChunks || chunkIndex == numChunks; chunkIndex++)
				{
					const std::string basePath = animationExportFilename.substr(0, animationExportFilename.length() - 10);
					const std::string endingPath = animationExportFilename.substr(animationExportFilename.length() - 10);
					const std::string chunkExportFilename = basePath + "-chunk-all" + endingPath;

					const int totalFrames = endFrame - startFrame;

					AnimBuildData* pStreamingAnimBuildData = new AnimBuildData("/" + lightSkelName + ".actor.xml");	// fake a builder path for the light skel actor since there's none
					pStreamingAnimBuildData->m_startFrame = 0;
					pStreamingAnimBuildData->m_endFrame = totalFrames;
					pStreamingAnimBuildData->m_ignoreSkelDebugInfo = false;

					// populate the stream anim list for linking into the final art group
					pStreamingAnimBuildData->m_pStreamAnim = new StreamAnim;
					if (chunkIndex == numChunks)
					{
						pStreamingAnimBuildData->m_pStreamAnim->m_fullName = MakeStreamAnimNameFinal(animName);
						pStreamingAnimBuildData->m_pStreamAnim->m_name = MakeStreamAnimNameFinal(animName);
						pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex = kStreamingChunkFinalIndex;

						pStreamingAnimBuildData->m_startFrame = totalFrames;
						pStreamingAnimBuildData->m_endFrame = totalFrames;
					}
					else
					{
						pStreamingAnimBuildData->m_pStreamAnim->m_fullName = MakeStreamAnimName(animName, chunkIndex);
						pStreamingAnimBuildData->m_pStreamAnim->m_name = MakeStreamAnimName(animName, chunkIndex);
						pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex = chunkIndex;

						const int numFramesInExportBlock = endFrame - startFrame;
						const int streamStartFrame = Min(pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex * kNumFramesPerStreamingChunk, numFramesInExportBlock);
						const int streamEndFrame = Min((pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex + 1) * kNumFramesPerStreamingChunk, numFramesInExportBlock);
						pStreamingAnimBuildData->m_startFrame = streamStartFrame;
						pStreamingAnimBuildData->m_endFrame = streamEndFrame;

						// We don't need custom channel data in the stream blocks (except for the last one)
						pStreamingAnimBuildData->m_exportCustomChannels = false;
					}

					const std::string animStreamChunkBoFilename = m_pContext->m_toolParams.m_buildPathAnimBo + pStreamingAnimBuildData->m_pStreamAnim->m_name + "." + ToString(sampleRate) + "fps.bo";
					pStreamingAnimBuildData->m_animBoFilename = animStreamChunkBoFilename;

					// All animations need to generate a .bo file
					std::vector<TransformInput> inputs;
					inputs.push_back(TransformInput(GetOutput(lightSkelSceneFileName).m_path, "SkelNdb"));
					inputs.push_back(TransformInput(lightRigInfoNdbPath, "RigInfo"));
					inputs.push_back(TransformInput(chunkExportFilename, "AnimNdb"));

					BuildTransform_BuildAnim* pBuildLightAnimXformStreaming = new BuildTransform_BuildAnim(m_pContext, pStreamingAnimBuildData, pCommonData);
					pBuildLightAnimXformStreaming->SetInputs(inputs);
					pBuildLightAnimXformStreaming->SetOutput(TransformOutput(animStreamChunkBoFilename, "AnimBo"));
					AddBuildTransform(pBuildLightAnimXformStreaming);

					// The first and possibly the last chunk goes into the main pak file.
					// All other .bo files will become .pak files and be added to the anim stream pak
					if (chunkIndex == 0 ||			// first chunk
						chunkIndex == numChunks)	// last chunk
					{
						pLightAnimFileListXform->AddInput(TransformInput(animStreamChunkBoFilename));
						animSceneFileIT->second.m_animNames.push_back(pStreamingAnimBuildData->m_pStreamAnim->m_name);
						animSceneFileIT->second.m_animTagNames.push_back(lightSkelName + FileIO::separator + pStreamingAnimBuildData->m_pStreamAnim->m_name);
					}
					else
					{
						StreamBoEntry entry;
						entry.m_boFileName = animStreamChunkBoFilename;
						entry.m_index = chunkIndex;
						entry.m_streamName = m_actorName + "." + ConstructStreamName(animName, animName);
						entry.m_sampleRate = pCommonData->m_sampleRate;
						entry.m_pAnim = nullptr;
						entry.m_numFrames = totalFrames;
						entry.m_animName = animName;
						entry.m_skelId = pCommonData->m_skelId;

						streamBoList.push_back(entry);
					}
				}
			}
		}
	}	// end for each light anim

	lightsStream.WriteBoFile(GetOutputPath("LightsBo"));

	// output the lights gobo textures
	NdbStream lightTexturesNdb;
	if (lightTexturesNdb.OpenForWriting(Ndb::kBinaryStream) == Ndb::kNoError)
	{
		NdbWrite(lightTexturesNdb, "m_lightTextures", lightTextures);
		lightTexturesNdb.Close();
		DataStore::WriteData(GetOutputPath("LightTextures"), lightTexturesNdb);
	}

	if (!streamBoList.empty())
	{
		// Sort them based on streamName
		std::sort(streamBoList.begin(), streamBoList.end(), StreamBoCompareFunc);

		std::vector<std::vector<StreamBoEntry>> perStreamBoItems;
		std::vector<int> numSlotsPerStream;
		std::string curAnimName;
		std::vector<StreamBoEntry> streamEntries;
		for (auto entry : streamBoList)
		{
			if (curAnimName != entry.m_streamName)
			{
				if (!streamEntries.empty())
					perStreamBoItems.push_back(streamEntries);
				streamEntries.clear();
				curAnimName = entry.m_streamName;
			}

			streamEntries.push_back(entry);
		}
		perStreamBoItems.push_back(streamEntries);

		// Create all STM files by reading in all bo files, converting them to pak files and then writing them back to back into the STM file
		std::vector<BuildPath> inputStms;
		for (auto streamEntries : perStreamBoItems)
		{
			if (streamEntries.empty())
				continue;

			std::vector<TransformInput> inputs;
			for (const auto& entry : streamEntries)
				inputs.push_back(TransformInput(entry.m_boFileName));

			std::vector<TransformOutput> outputs;
			outputs.push_back(TransformOutput(m_pContext->m_toolParams.m_buildPathAnimStream + streamEntries[0].m_streamName + ".stm", "AnimStream", TransformOutput::kIncludeInManifest));
			const std::string animStreamBufferBoFilename = m_pContext->m_toolParams.m_buildPathAnimStream + streamEntries[0].m_streamName + ".bo";
			outputs.push_back(TransformOutput(animStreamBufferBoFilename, "AnimStreamBo"));
			pLightAnimFileListXform->AddInput(TransformInput(animStreamBufferBoFilename));

			BuildTransform_AnimStreamStm* pStreamXform = new BuildTransform_AnimStreamStm(m_pContext, streamEntries/*, pStreamBoXform->m_streamHeaders*/);
			pStreamXform->SetInputs(inputs);
			pStreamXform->SetOutputs(outputs);
			AddBuildTransform(pStreamXform);

			actorTxt += ("animstream " + streamEntries[0].m_streamName + "\n");
		}
	}

	const BuildPath& actorTxtPath = GetOutputPath("LightsAnimActorTxt");
	DataStore::WriteData(actorTxtPath, actorTxt);
	
	for (auto sceneFile : sceneFileToAnimGroupInfo)
	{
		BuildTransform_AnimGroup *const pAnimGroup = new BuildTransform_AnimGroup(m_pContext,
			m_actorName,
			sceneFile.second.m_skelId,
			sceneFile.second.m_animNames,
			sceneFile.second.m_animTagNames,
			true);

		const std::string animGroupBoPath = m_pContext->m_toolParams.m_buildPathAnimGroupBo + m_actorName + "." + sceneFile.first + ".light-anims.bo";
		pAnimGroup->SetOutput(TransformOutput(animGroupBoPath, "BoFile"));
		AddBuildTransform(pAnimGroup);
		pLightAnimFileListXform->AddInput(TransformInput(animGroupBoPath));
	}

	AddBuildTransform(pLightAnimFileListXform);

	return BuildTransformStatus::kOutputsUpdated;
}

U32 BuildModuleLightAnim_Configure(const BuildTransformContext *const pContext,
									const libdb2::Actor *const pDbActor)
{
	U32 numLightSkels = 0;
	BuildTransform_LightAnimsBuildSpawner *const pLightAnimsBuildSpawner = new BuildTransform_LightAnimsBuildSpawner(pContext, pDbActor->Name());
	std::vector<std::string> uniquelightSkels;	// each animation scene file contains an unique light skel

	const libdb2::AnimList& dbanimList = pDbActor->m_animations;
	for (const libdb2::Anim* pAnim : dbanimList)
	{
		if (pDbActor->m_sequence.Loaded() || pAnim->m_flags.m_exportLightAnim)
		{
			int32_t startFrame = (I32)pAnim->m_animationStartFrame.m_value;
			int32_t endFrame = (I32)pAnim->m_animationEndFrame.m_value;
			U32 sampleRate = GetAnimSampleRate(*pAnim, pContext->m_toolParams.m_defaultAnimSampleRate);
			const std::string animName = pAnim->GetLightAnimName(pDbActor);

			BuildTransform_ExportLightAnims* pExportLightAnimsXform = new BuildTransform_ExportLightAnims(pContext, startFrame, endFrame, sampleRate);
			pExportLightAnimsXform->SetInput(TransformInput(PathPrefix::BAM + pAnim->m_animationSceneFile));
			std::string lightAnimsExportFilename = pContext->m_toolParams.m_buildPathAnim + animName + "." + ToString(sampleRate) + "fps.ndb";
			pExportLightAnimsXform->SetOutput(TransformOutput(lightAnimsExportFilename));
			pContext->m_buildScheduler.AddBuildTransform(pExportLightAnimsXform, pContext);

			// streaming export
			if (pAnim->m_flags.m_Streaming)
			{
				BuildTransform_ExportLightAnims* pExportLightAnimsXformStreaming = new BuildTransform_ExportLightAnims(pContext, startFrame, endFrame, sampleRate);
				pExportLightAnimsXformStreaming->SetInput(TransformInput(PathPrefix::BAM + pAnim->m_animationSceneFile));

				const std::string basePath = lightAnimsExportFilename.substr(0, lightAnimsExportFilename.length() - 10);
				const std::string endingPath = lightAnimsExportFilename.substr(lightAnimsExportFilename.length() - 10);
				pExportLightAnimsXformStreaming->SetOutput(TransformOutput(basePath + "-chunk-all" + endingPath));
				pContext->m_buildScheduler.AddBuildTransform(pExportLightAnimsXformStreaming, pContext);
			}

			// add to spawner
			pLightAnimsBuildSpawner->AddInput(pExportLightAnimsXform->GetFirstOutput());
			const std::string lightSkelSceneFileName = pAnim->GetLightSkelSceneFileName(pDbActor);

			auto refSkelActorsIter = std::find(uniquelightSkels.begin(), uniquelightSkels.end(), lightSkelSceneFileName);
			if (refSkelActorsIter == uniquelightSkels.end())
			{
				numLightSkels++;
				uniquelightSkels.push_back(lightSkelSceneFileName);
				pLightAnimsBuildSpawner->AddOutput(TransformOutput(pContext->m_toolParams.m_buildPathSkel + lightSkelSceneFileName.substr(0, lightSkelSceneFileName.find(".ma")) + FileIO::separator + "light-skel.ndb", lightSkelSceneFileName));
			}

			if (pDbActor->m_sequence.Loaded())	// cinematics only export light anim from the 0th animation
				break;
		}
	}

	pLightAnimsBuildSpawner->AddOutput(TransformOutput(pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + FileIO::separator + "unpatched-lights.bo", "LightsBo"));

	const std::string lightTexturesFilename = pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + FileIO::separator + "lights-textures.ndb";
	pLightAnimsBuildSpawner->AddOutput(TransformOutput(lightTexturesFilename, "LightTextures"));

	const std::string lightsAnimActorTxtFilename = pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + ".txt";
	pLightAnimsBuildSpawner->AddOutput(TransformOutput(lightsAnimActorTxtFilename, "LightsAnimActorTxt"));

	if (numLightSkels > 0)
		pContext->m_buildScheduler.AddBuildTransform(pLightAnimsBuildSpawner, pContext);
	else
		delete pLightAnimsBuildSpawner;

	return numLightSkels;
}