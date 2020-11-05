/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animmetadata.h"
#include "icelib/loadxml/readxml.h"
#include "icelib/common/filepath.h"

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {
			const Vec3BitPackingFormat kVbpfVec3Range_Default(16, 16, 16, 48);
			const Vec3BitPackingFormat kVbpfQuatSmallestThree_Default(16, 15, 15, 48);
			const Vec3BitPackingFormat kVbpfQuatLog_Default(16, 16, 16, 48);
		};
	};
};

/* strcasecmp() is called stricmp() on Windows (!) */
#ifdef _MSC_VER
#define strcasecmp(s1,s2) stricmp((s1), (s2))
#endif

/* Parse animation compression meta-data in format:
<?xml version="1.0"?>
<!--Setup compression settings for any matching animation;  If (animation="animname") is specified, only animations with
	the name "animname" will match.  If (bindpose="bindposename") is specified, only animations whose bind pose scene is
	named "bindposename" will match.
	If (include="file.xml") is specified, the file "file.xml" will be parsed to set default values before parsing this
	compression scope.  (include="label") or (include="file.xml:label") may be used to include a specific compression scope
	from the current or another file, based on a matching animation="label" attribute.  Multiple includes may be specified
	as a comma separated list.
	keys="(uniform|shared|unshared)" must be set in every compression scope that is directly used as a metadata file for
	some animation, but may be set by an included file.  Until keys is set, no keyframes tags may be processed.
	If (rootspaceerrors="1") is specified, local space errors for bit formats and keyframes will be ignored, and
		will be instead generated per frame from rootspaceerror error values, which must be converted to
		local space errors by accounting for the hierarchy and current pose at each frame.
	If (deltaoptimization="1") and (rootspaceerrors="1") are specified, errordelta and errorspeed values in
		rootspaceerror tags will additionally be applied.  The root space error value will be expanded by
		errordelta * (track rate of change)[iFrame] + errorspeed * (translation rate of change)[iFrame].
		errorspeed values are only used for joint rotation and scale parameter tracks and are not valid for
		translation or float channel tracks.
	If (globaloptimization="1") is specified, the bit compression process will be adjusted to attempt to minimize
		errors stacking up across multiple parents, by adjusting the target values of child joints to account for
		bit compression errors in parent joints.
 -->
<compression [include="(file.xml)|(label)|(file.xml:label)[,...]"] [animation="(animname)|(label)"] [bindpose="(bindposename)"] [keys="(uniform|shared|unshared)"]
			 [rootspaceerrors[="0|1"]] [deltaoptimization[="0|1"]] [globaloptimization[="0|1"]]>
	<!-- Optionally set up a palette of labeled compression scheme defaults:
			Defining a label is required to create a shared bitformat, but also allows
			shorter scale, rotation, and translation tags (override attributes only) -->
	<vectorformat label="(label)" constformat="(uncompressed|float16|auto)" format="(uncompressed|float16|range|auto)" bitformat=("x_bits,y_bits,z_bits"|"generate") [error="(float)"] [consterror="(float)"] [shared="1"]/>
	<quaternionformat label="(label)" constformat="(uncompressed|smallest3_48|auto)" format="(uncompressed|smallest3|log|logpca|auto)" bitformat=("x_bits,y_bits,z_bits"|"generate") [error="(float)"] [consterror="(float)"] [shared="1"]/>
	<floatformat label="(label)" constformat="(uncompressed|auto)" format="(uncompressed|auto)" bitformat=("bits"|"generate") [error="(float)"] [consterror="(float)"] [shared="1"]/>

	<!-- if (keys==shared) set up keyframe compression:
			If generate is enabled here for the clip, error values will be collected per
			joint and floatchannel and a default value may be specified here; these will
			be used to generate shared keyframes based on error tolerances.
			If deltaoptimization is enabled for the clip, error,
			errordelta, and errorspeed values will be collected per joint and default
			values may be specified here; these will be used to generate shared keyframes
			based on speed dependent error tolerances. -->
	<keyframes [generate[="(0|1)"]] [error="(float)"]>
		<!-- Directly define (additional) frames to skip : always applied before keeplist lists -->
		<skiplist>
			(int list)
		</skiplist>
		<!-- Directly define frames to keep : always applied after skiplist and generate lists -->
		<keeplist>
			(int list)
		</keeplist>
	</keyframes>
	<!-- if (keys==unshared) set up keyframe compression defaults:
			Key frame generation based on error tolerances may be enabled by default here,
			and modified on a channel by channel basis later.
			error values will be collected per joint and floatchannel and a default value
			may be specified here; these will be used to generate unshared keyframes based
			on error tolerances for any channels with key frame generation enabled.
			If deltaoptimization is enabled for the clip, errordelta values will also
			be collected per joint and a default value may be specified here; these will
			be used to generate unshared keyframes based on velocity dependent error
			tolerances for any channels with key frame generation enabled.
			maxkeysperblock and maxblocksize may also be specified here for the clip.
			-->
	<keyframes [generate[="(0|1)"]] [maxkeysperblock="(int)"] [maxblocksize="(int)"] [error="(float)"]/>

	<!--Set up or modify default compression scheme for all following joints or float channels;
		All defaults are uncompressed before they are modified.
		error, errordelta, errorspeed, and consterror values set the default error tolerances used for bit compression
		and for determining whether a channel is constant, respectively. -->
	<scale [format="(uncompressed|float16|range|auto|(vectorformat label))"] [constformat="(uncompressed|float16|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [consterror="(float)"]>
		<!-- if (keys==shared) set up keyframe error default for scales: -->
		<keyframes [error="(float)"]/>
		<!-- if (keys==unshared) set up keyframe compression defaults for scales: -->
		<keyframes [generate[="(0|1)"]] [error="(float)"]/>
		<!-- if rootspaceerrors=="1", set up default root space errors for all joint scale tracks: -->
		<rootspaceerror [error="(float)"] [errordelta="(float)"] [errorspeed="(float)"]/>
	</scale>
	<rotation [format="(uncompressed|smallest3|log|logpca|auto|(quaternionformat label))"] [constformat="(uncompressed|smallest3_48|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [consterror="(float)"]>
		<!-- if (keys==shared) set up keyframe error default for rotations: -->
		<keyframes [error="(float)"]/>
		<!-- if (keys==unshared) set up keyframe compression defaults for rotations: -->
		<keyframes [generate[="(0|1)"]] [error="(float)"]/>
		<!-- if rootspaceerrors=="1", set up default root space errors for all joint rotation tracks: -->
		<rootspaceerror [error="(float)"] [errordelta="(float)"] [errorspeed="(float)"]/>
	</rotation>
	<translation [format="(uncompressed|float16|range|auto|(vectorformat label))"] [constformat="(uncompressed|float16|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [errordelta="(float)"] [errorspeed="(float)"] [consterror="(float)"]>
		<!-- if (keys==shared) set up keyframe error default for translations: -->
		<keyframes [error="(float)"]/>
		<!-- if (keys==unshared) set up keyframe compression defaults for translations: -->
		<keyframes [generate[="(0|1)"]] [error="(float)"]/>
		<!-- if rootspaceerrors=="1", set up default root space errors for all joint translation tracks: -->
		<rootspaceerror [error="(float)"] [errordelta="(float)"]/>
	</translation>
	<float [format="(uncompressed|auto|(floatformat label))"] [constformat="(uncompressed|auto)"] [bitformat=("bits"|"generate")] [error="(float)"] [consterror="(float)"]>
		<!-- if (keys==shared) set up keyframe error default for float channels: -->
		<keyframes [error="(float)"]/>
		<!-- if (keys==unshared) set up keyframe compression defaults for float channels: -->
		<keyframes [generate[="(0|1)"]] [error="(float)"]/>
		<!-- if rootspaceerrors=="1", set up default root space errors for all float channel tracks: -->
		<rootspaceerror [error="(float)"] [errordelta="(float)"]/>
	</float>

	<!-- Define a labelled list of joints by full dag path (ex. "|root|spine00|spine01") or partial name (ex. "spine01" or "spine00|spine01")
	     Any name in the list beginning with a '|' character will be matched against the full dag path of each joint,
		 otherwise, the name will be matched against the end of the full dag path of each joint.
		 The label should be alphanumeric plus '_' or '-', and may be used later in a joint name list to include the defined list. -->
	<joint name="(jointdagpath|jointpartialdagpath|joint_label)[,...]" label="(label)"/>
	<!-- Define a labelled list of float channels by full dag path (ex. "|root|spine00|locator.floatChannel0") or partial name
	     (ex. "locator.floatChannel0" or "spine00|locator.floatChannel0").
	     Any name in the list beginning with a '|' character will be matched against the full dag path of each float channel,
		 otherwise, the name will be matched against the end of the full dag path of each float channel.
		 All names must contain one '.' separating the node name from the attribute name within the leaf name.
		 The label should be alphanumeric plus '_' or '-', and may be used later in a floatchannel name list to include the defined list. -->
	<floatchannel name="(floatchanneldagpath|floatchannelpartialdagpath|floatchannel_label)[,...]" label="(label)"/>

	<joint name="(jointdagpath|jointpartialdagpath|joint_label)[,...]" [label="(label)"]>
		<!--Set up or modify the compression scheme for any joint whose name is listed in the given name list;
			All values are initially set to the current default compression values the first time any valid
			scale, rotation or translation tag is found within a joint tag. -->
		<scale [format="(uncompressed|float16|range|auto|(vectorformat label))"] [constformat="(uncompressed|float16|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [consterror="(float)"]>
			<!-- if (keys==shared) set up keyframe compression error for scales: -->
			<keyframes [error="(float)"]/>
			<!-- if (keys==unshared) set up keyframe compression for scales: -->
			<keyframes [generate[="(0|1)"]] [error="(float)"]>
				<!-- Directly define (additional) frames to skip : always applied before keeplist lists -->
				<skiplist>
					(int list)
				</skiplist>
				<!-- Directly define frames to keep : always applied after skiplist and generate lists -->
				<keeplist>
					(int list)
				</keeplist>
			</keyframes>
			<!-- if rootspaceerrors=="1", set up root space errors for these joint scale tracks: -->
			<rootspaceerror [error="(float)"] [errordelta="(float)"] [errorspeed="(float)"]/>
		</scale>
		<rotation [format="(uncompressed|smallest3|log|logpca|auto|(quaternionformat label))"] [constformat="(uncompressed|smallest3_48|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [consterror="(float)"]>
			<!-- if (keys==shared) set up keyframe compression error for rotations: -->
			<keyframes [error="(float)"]/>
			<!-- if (keys==unshared) set up keyframe compression for scales: -->
			<keyframes [generate[="(0|1)"]] [error="(float)"]>
				<!-- Directly define (additional) frames to skip : always applied before keeplist lists -->
				<skiplist>
					(int list)
				</skiplist>
				<!-- Directly define frames to keep : always applied after skiplist and generate lists -->
				<keeplist>
					(int list)
				</keeplist>
			</keyframes>
			<!-- if rootspaceerrors=="1", set up root space errors for these joint rotation tracks: -->
			<rootspaceerror [error="(float)"] [errordelta="(float)"] [errorspeed="(float)"]/>
		</rotation>
		<translation [format="(uncompressed|float16|range|auto|(vectorformat label))"] [constformat="(uncompressed|float16|auto)"] [bitformat=("x_bits,y_bits,z_bits"|"generate")] [error="(float)"] [consterror="(float)"]>
			<!-- if (keys==shared) set up keyframe compression error for translations: -->
			<keyframes [error="(float)"]/>
			<!-- if (keys==unshared) set up keyframe compression for translations: -->
			<keyframes [generate[="(0|1)"]] [error="(float)"]>
				<!-- Directly define (additional) frames to skip : always applied before keeplist lists -->
				<skiplist>
					(int list)
				</skiplist>
				<!-- Directly define frames to keep : always applied after skiplist and generate lists -->
				<keeplist>
					(int list)
				</keeplist>
			</keyframes>
			<!-- if rootspaceerrors=="1", set up root space errors for these joint translation tracks: -->
			<rootspaceerror [error="(float)"] [errordelta="(float)"]/>
		</translation>
	</joint>

	<floatchannel name="(floatchanneldagpath|floatchannelpartialdagpath|floatchannel_label)[,...]" [label="(label)"]>
		<!--Set up or modify the compression scheme for any float channel whose name is listed in the given name list;
			All values are initially set to the current default compression values the first time any valid
			keyframes tag is found within a floatchannel tag. -->
		<float [format="(uncompressed|auto|(floatformat label))"] [constformat="(uncompressed|auto)"] [bitformat=("bits"|"generate")] [error="(float)"] [consterror="(float)"]>
			<!-- if (keys==shared) set up keyframe compression error for float channels: -->
			<keyframes [error="(float)"]/>
			<!-- if (keys==unshared) set up keyframe compression for float channels: -->
			<keyframes [generate[="(0|1)"]] [error="(float)"]>
				<!-- Directly define (additional) frames to skip : always applied before keeplist lists -->
				<skiplist>
					(int list)
				</skiplist>
				<!-- Directly define frames to keep : always applied after skiplist and generate lists -->
				<keeplist>
					(int list)
				</keeplist>
			</keyframes>
			<!-- if rootspaceerrors=="1", set up root space errors for these floatchannel tracks: -->
			<rootspaceerror [error="(float)"] [errordelta="(float)"]/>
		</float>
	</floatchannel>
</compression>
*/

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

const char *GetFormatAttribName(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctVec3Uncompressed:
	case kAcctQuatUncompressed:
	case kAcctFloatUncompressed:
	case kAcctConstVec3Uncompressed:
	case kAcctConstQuatUncompressed:
	case kAcctConstFloatUncompressed:
		return "uncompressed";
	case kAcctVec3Float16:
	case kAcctConstVec3Float16:
		return "float16";
	case kAcctVec3Range:
		return "range";
	case kAcctQuatSmallestThree:
		return "smallest3";
	case kAcctQuatLog:
		return "log";
	case kAcctQuatLogPca:
		return "logpca";
	case kAcctConstQuat48SmallestThree:
		return "smallest3_48";
	case kAcctVec3Auto:
	case kAcctQuatAuto:
	case kAcctFloatAuto:
	case kAcctConstVec3Auto:
	case kAcctConstQuatAuto:
	case kAcctConstFloatAuto:
		return "auto";
	default:
		return "INVALID";
	}
}

bool AnimMetaDataIsUncompressed(AnimMetaData const& metaData)
{
	if (metaData.m_flags != 0)
		return false;
	if (metaData.m_defaultCompression.m_scale.m_format.m_compressionType != kAcctVec3Uncompressed
	||	metaData.m_defaultCompression.m_scale.m_format.m_constCompressionType != kAcctConstVec3Uncompressed
	||	metaData.m_defaultCompression.m_rotation.m_format.m_compressionType != kAcctQuatUncompressed
	||	metaData.m_defaultCompression.m_rotation.m_format.m_constCompressionType != kAcctConstQuatUncompressed
	||	metaData.m_defaultCompression.m_translation.m_format.m_compressionType != kAcctVec3Uncompressed
	||	metaData.m_defaultCompression.m_translation.m_format.m_constCompressionType != kAcctConstVec3Uncompressed
	||	metaData.m_defaultCompressionFloat.m_format.m_compressionType != kAcctFloatUncompressed
	||	metaData.m_defaultCompressionFloat.m_format.m_constCompressionType != kAcctConstFloatUncompressed)
		return false;
	for (std::vector<AnimMetaDataJointCompressionMethod>::const_iterator it = metaData.m_jointCompression.begin(), itEnd = metaData.m_jointCompression.end(); it != itEnd; ++it) {
		if (it->m_scale.m_format.m_compressionType != kAcctVec3Uncompressed
		||	it->m_scale.m_format.m_constCompressionType != kAcctConstVec3Uncompressed
		||	it->m_rotation.m_format.m_compressionType != kAcctQuatUncompressed
		||	it->m_rotation.m_format.m_constCompressionType != kAcctConstQuatUncompressed
		||	it->m_translation.m_format.m_compressionType != kAcctVec3Uncompressed
		||	it->m_translation.m_format.m_constCompressionType != kAcctConstVec3Uncompressed)
			return false;
	}
	for (std::vector<AnimMetaDataTrackCompressionMethod>::const_iterator it = metaData.m_floatCompression.begin(), itEnd = metaData.m_floatCompression.end(); it != itEnd; ++it) {
		if (it->m_format.m_compressionType != kAcctFloatUncompressed
		||	it->m_format.m_constCompressionType != kAcctConstFloatUncompressed)
			return false;
	}
	return true;
}

static int DEFAULT_Handler(const char* tagname, const char** atts, bool start, void* userdata);
static int CompressionHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int VectorFormatHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int QuaternionFormatHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int FloatFormatHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int ScaleHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int RotationHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int TranslationHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int FloatHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int KeyFramesHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int SkipListHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int KeepListHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int RootSpaceErrorHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int JointHandler(const char* tagname, const char** atts, bool start, void* userdata);
static int FloatChannelHandler(const char* tagname, const char** atts, bool start, void* userdata);

static const int kReadXmlAnimMetaDataError = 3;

enum AnimMetaDataNoteSeverity
{
	kAnimMetaDataSeverityNone =			0x00000000,
		kAnimMetaDataSeverity1 =			0x10000000,
		kAnimMetaDataSeverity2 =			0x20000000,
		kAnimMetaDataSeverity3 =			0x30000000,
	kAnimMetaDataSeverityNote =			0x40000000,
		kAnimMetaDataSeverity5 =			0x50000000,
		kAnimMetaDataSeverity6 =			0x60000000,
		kAnimMetaDataSeverity7 =			0x70000000,
	kAnimMetaDataSeverityWarning =		0x80000000,
		kAnimMetaDataSeverity9 =			0x90000000,
		kAnimMetaDataSeverity10=			0xA0000000,
		kAnimMetaDataSeverity11=			0xB0000000,
	kAnimMetaDataSeverityError =		0xC0000000,
		kAnimMetaDataSeverity13=			0xD0000000,
		kAnimMetaDataSeverity14=			0xE0000000,
	kAnimMetaDataSeverityFatalError =	0xF0000000,

	kAnimMetaDataMaskSeverity =			0xF0000000,
};
enum AnimMetaDataNoteCode
{
	// numeric codes, generally or'd with one of kAnimMetaDataSeverity*
	kAnimMetaDataNoError =				0x00000000,
	kAnimMetaDataUnexpectedTag =		0x00000001,
	kAnimMetaDataMissingTag =			0x00000002,
	kAnimMetaDataTagMismatch =			0x00000003,
	kAnimMetaDataUnexpectedAttrib =		0x00000004,
	kAnimMetaDataMissingAttrib =		0x00000005,
	kAnimMetaDataAttribFormat =			0x00000006,
	kAnimMetaDataIncludeLoop =			0x00000007,
	kAnimMetaDataFileNotFound =			0x00000008,
	kAnimMetaDataFileInvalid =			0x00000009,

	kAnimMetaDataFlagXmlError =			0x00008000 | kAnimMetaDataSeverityFatalError,
};

struct lt_string
{
	bool operator()(std::string s1, std::string s2) const{ return strcmp(s1.c_str(), s2.c_str()) < 0; }
};

enum AnimMetaDataTag
{
	kAnimMetaDataTagROOT = 0,
	kAnimMetaDataTagUNKNOWN = 1,
	kAnimMetaDataTagCompression = 2,
	kAnimMetaDataTagVectorFormat,
	kAnimMetaDataTagQuaternionFormat,
	kAnimMetaDataTagFloatFormat,
	kAnimMetaDataTagScale,
	kAnimMetaDataTagRotation,
	kAnimMetaDataTagTranslation,
	kAnimMetaDataTagFloat,
	kAnimMetaDataTagKeyFrames,
	kAnimMetaDataTagSkipList,
	kAnimMetaDataTagKeepList,
	kAnimMetaDataTagRootSpaceError,
	kAnimMetaDataTagJoint,
	kAnimMetaDataTagFloatChannel,
	kNumAnimMetaDataTags
};
enum AnimMetaDataStateFlags
{
	kStateInJointScope =							0x0001,
	kStateInFloatChannelScope =						0x0002,
	kStateKeyframeCompressionDefined =				0x0010,
	kStateDefaultKeyFramesDefined =					0x0020,
	kStateDefaultScaleCompressionDefined =			0x0100,
	kStateDefaultRotationCompressionDefined =		0x0200,
	kStateDefaultTranslationCompressionDefined =	0x0400,
	kStateDefaultFloatCompressionDefined =			0x0800,
	kStateDefaultScaleRootSpaceErrorDefined =		0x1000,
	kStateDefaultRotationRootSpaceErrorDefined =	0x2000,
	kStateDefaultTranslationRootSpaceErrorDefined =	0x4000,
	kStateDefaultFloatRootSpaceErrorDefined =		0x8000,

	kStateCompressionScopeMask =					0x0003,
	kStateDefinedMask =								0xFF30,
};

typedef std::vector<U16> ChannelList;
typedef std::map< std::string, AnimMetaDataTrackBitFormat, lt_string > CompressionLabelMap;
typedef std::map< std::string, std::set<size_t>, lt_string > ChannelSetLabelMap;
static const int kInvalidChannel = (U16)-1;
struct AnimMetaDataState
{
	U16									m_flags;	// union of AnimMetaDataStateFlags
	ChannelList							m_channels;	// current joint or float channels
	AnimMetaDataTag						m_trackTag;	// scale, rotation, translation, float or ROOT
	std::stack<AnimMetaDataTag>			m_stack;	// stack of tags
	unsigned							m_stackInvalidTop;	// top of the invalid part of the stack - if m_stack.size() > m_stackInvalidTop, we must ignore all tags
	CompressionLabelMap					m_vectorFormats;
	CompressionLabelMap					m_quaternionFormats;
	CompressionLabelMap					m_floatFormats;
	ChannelSetLabelMap					m_jointSets;
	ChannelSetLabelMap					m_floatChannelSets;
	std::set<size_t>					m_jointModifiedSet;	// set of joints for which custom values have been set
	std::set<size_t>					m_floatModifiedSet;	// set of float channels for which custom values have been set

	AnimMetaDataState() : m_flags(0), m_trackTag(kAnimMetaDataTagROOT), m_stack(), m_stackInvalidTop((unsigned)-1), m_vectorFormats(), m_quaternionFormats(), m_floatFormats() {}
};

const float kfConstErrorToleranceUseDefault = -1.0f;

// special value to select const channel tolerance so that the channel is encoded as a constant if it meets 
// the local space error tolerance & has fewer bits than all non-const options
const float kConstErrorToleranceAuto = -2.0f;

/*
-- LoadAnimMetaData
--
*/
class LoadAnimMetaData : public ReadXml::ParseXmlFile
{
public:
    typedef  std::string AttrVal;
	typedef std::pair<std::string, std::string> Include;
	typedef std::vector<Include> IncludeList;

    LoadAnimMetaData(AnimMetaData* metaData, std::string const& filename, std::string const& animname, std::string const& bindposename, IncludeList const& includes = IncludeList())
	:	m_metaData(metaData)
	,	m_filename(filename)
	,	m_animname(animname)
	,	m_bindposename(bindposename)
	,	m_includes(includes)
	,	m_eSeverity(kAnimMetaDataSeverityNone)
	{
		if (!filename.empty() && !animname.empty())
			m_includes.push_back( Include(filename, animname) );
	}
    ~LoadAnimMetaData(void);

    //sub-class implementation of ParseXml virtual methods
    int   Init(void);
    int   Reset(void);
    //implementations for baseclass's virtual methods
    void  StartElement(const char *tagname, const char **atts);
    void  EndElement(const char *tagname);

    /// returns the attribute with 'key' - e.g. 'type="<key>"'
    const AttrVal* GetAttr( const char* key ) const {
        return m_contxt.m_props.Value( key );
    }
    /// returns the type attribute from the tag's attribute list - e.g. 'type="dome"'
    const AttrVal* GetTypeAttr() const {
        return m_contxt.m_props.Value( "type" );
    }
    /// returns the name attribute from the tag's attribute list - e.g. 'name="Sky"'
    const AttrVal* GetNameAttr() const {
        return m_contxt.m_props.Value( "name" );
    }
    /// returns the size attribute from the tag's attribute list - e.g. 'size="3"'
    const AttrVal* GetSizeAttr() const {
        return m_contxt.m_props.Value( "size" );
    }
    /// kicks off the parse loop
	int  DoParse( );
    void DoOutput( );

	/// returns true if we are in a sub-scope of an invalid scope, false if not or if we are in the root invalid scope
	bool IsInsideInvalidScope() const { return (unsigned)m_state.m_stack.size() > m_state.m_stackInvalidTop; }
	/// Sets the current scope to be invalid
	void SetInvalidScope()
	{
		if (m_state.m_stackInvalidTop > (unsigned)m_state.m_stack.size())
			m_state.m_stackInvalidTop = (unsigned)m_state.m_stack.size();
	}

	/// Returns the current scope's tag
	AnimMetaDataTag GetScope() const { return m_state.m_stack.empty() ? kAnimMetaDataTagROOT : m_state.m_stack.top(); }
	/// enters a scope, returning the containing scope's tag
	AnimMetaDataTag EnterScope( AnimMetaDataTag tag )
	{
		AnimMetaDataTag tagContaining = m_state.m_stack.empty() ? kAnimMetaDataTagROOT : m_state.m_stack.top();
		m_state.m_stack.push( tag );
		return tagContaining;
	}
	/// exits a scope, returning true if it was valid, and clearing the invalid state if it was the root invalid scope
	bool ExitScope()
	{
		unsigned iScopeDepth = (unsigned)m_state.m_stack.size();
		m_state.m_stack.pop();
		if (m_state.m_stackInvalidTop > iScopeDepth)
			return true;
		if (m_state.m_stackInvalidTop == iScopeDepth)
			m_state.m_stackInvalidTop = (unsigned)-1;	// exitting root invalid scope
		return false;	// exitting an invalid scope
	}

	/// extracts the current charbuf as a file path:
	std::string GetCharBufAsFilename() const;

    /// returns worst severity among notes
	AnimMetaDataNoteSeverity GetSeverity() const { return m_eSeverity; }

	unsigned ReportFatalError( AnimMetaDataNoteCode eCode, std::string const& strMessage )
	{
		return Note( (unsigned)kAnimMetaDataSeverityFatalError | (unsigned)eCode, strMessage );
	}
	unsigned ReportError( AnimMetaDataNoteCode eCode, std::string const& strMessage )
	{
		return Note( (unsigned)kAnimMetaDataSeverityError | (unsigned)eCode, strMessage );
	}
	unsigned ReportWarning( AnimMetaDataNoteCode eCode, std::string const& strMessage )
	{
		return Note( (unsigned)kAnimMetaDataSeverityWarning | (unsigned)eCode, strMessage );
	}
	unsigned ReportNote( AnimMetaDataNoteCode eCode, std::string const& strMessage )
	{
		return Note( (unsigned)kAnimMetaDataSeverityNote | (unsigned)eCode, strMessage );
	}

	void ClearNotes()
	{
		m_notes.clear();
		m_eSeverity = kAnimMetaDataSeverityNone;
	}

	AnimMetaDataNoteSeverity PrintNotes(std::string const& strPrefix)
	{
		AnimMetaDataNoteSeverity eSeverity = m_eSeverity;
		for (std::vector<NoteInfo>::const_iterator it = m_notes.begin(), itEnd = m_notes.end(); it != itEnd; ++it) {
			IWARN( (strPrefix + it->m_string + "\n").c_str() );
		}
		return eSeverity;
	}
	AnimMetaDataNoteSeverity PrintAndClearNotes(std::string const& strPrefix)
	{
		AnimMetaDataNoteSeverity eSeverity = PrintNotes(strPrefix);
		ClearNotes();
		return eSeverity;
	}

	void CopyNotes(LoadAnimMetaData const& loader, std::string const& strPrefix)
	{
		NoteInfo note;
		for (std::vector<NoteInfo>::const_iterator it = loader.m_notes.begin(), itEnd = loader.m_notes.end(); it != itEnd; ++it) {
			note.m_code = it->m_code;
			note.m_lineNumber = it->m_lineNumber;
			note.m_string = strPrefix + it->m_string;
			m_notes.push_back(note);
		}
		if ((unsigned)m_eSeverity < (unsigned)loader.m_eSeverity)
			m_eSeverity = loader.m_eSeverity;
	}

	/// Parse the given file for compression tags matching the animation name "label".
	/// If filename is "", re-parse the current file.  If label is "", parse for the current animation name.
	/// Detects include loops.  Returns loadError (from readxml.h)
	int ParseIncludeFile(std::string const& filename, std::string const& label);

	/// Check if the data in m_metaData is valid.
	bool Validate();

protected:
    void DoCharacterData(void);

	/// report a note from a handler function:
	unsigned Note(unsigned code, std::string const& strMessage)
	{
		return Note(code, (unsigned)(m_parser ? GetCurrentLineNumber() : -1), strMessage);
	}
	unsigned Note(unsigned code, unsigned lineNumber, std::string const& strMessage)
	{
		char buf[80];
		std::string strSeverity, strErrorCode;
		NoteInfo note;
		AnimMetaDataNoteSeverity eSeverity = (AnimMetaDataNoteSeverity)(code & kAnimMetaDataMaskSeverity);
		if ((unsigned)eSeverity >= (unsigned)kAnimMetaDataSeverityError) {
			if ((unsigned)eSeverity >= (unsigned)kAnimMetaDataSeverityFatalError && HasNoError())
				m_loadError = kReadXmlAnimMetaDataError;
			strSeverity = "ERROR ";
		} else if ((unsigned)eSeverity >= (unsigned)kAnimMetaDataSeverityWarning) {
			strSeverity = "WARNING ";
		} else {
			strSeverity = "NOTE ";
		}
		note.m_code = code;
		sprintf(buf, "0x%x", code);
		strErrorCode = std::string(buf);
		note.m_lineNumber = lineNumber;
		if (lineNumber != (unsigned)-1) {
			std::string strLineNumber;
			sprintf(buf, "%u", lineNumber);
			strLineNumber = std::string(buf);
			note.m_string = strSeverity + strErrorCode +" at "+ m_filename +":"+ strLineNumber +": " + strMessage;
		} else {
			note.m_string = strSeverity + strErrorCode +": " + strMessage;
		}
		if ((unsigned)m_eSeverity < (unsigned)eSeverity)
			m_eSeverity = eSeverity;
		m_notes.push_back(note);
		return code;
	}

	virtual void PrintError(int xml_error, int lineNumber, const char* szErrorMsg)
	{
		Note( (unsigned)kAnimMetaDataSeverityFatalError | (unsigned)kAnimMetaDataFlagXmlError | (unsigned)xml_error, (unsigned)lineNumber, std::string(szErrorMsg) );
	}

public:     //public data members
	AnimMetaData*				m_metaData;			//!< our meta data
	AnimMetaDataState			m_state;			//!< state data
	std::string					m_filename;			//!< the name of the file we are parsing
	std::string					m_animname;			//!< the name of the animation we will match while parsing compression tags
	std::string					m_bindposename;		//!< the name of the bindpose we will match while parsing compression tags
	IncludeList					m_includes;			//!< list of includes (file.xml, animname|label) read, for loop detection

	struct NoteInfo {
		unsigned				m_code;			//!< numeric code for note type
		unsigned				m_lineNumber;	//!< line number where note was generated
		std::string				m_string;		//!< text message describing note
	};
	std::vector<NoteInfo>		m_notes;		//!< note listing
	AnimMetaDataNoteSeverity	m_eSeverity;	//!< highest severity in m_notes
}; //end of LoadAnimMetaData

LoadAnimMetaData::~LoadAnimMetaData()
{
}

//sub-class implementation of ParseXml virtual methods
int LoadAnimMetaData::Init(void)
{
	// define handler for unknown tags:
	SetDefaultHandler( DEFAULT_Handler );
    // define handlers for known tags:
    AddHandler( "compression",			CompressionHandler );
	AddHandler( "vectorformat",			VectorFormatHandler );
    AddHandler( "quaternionformat",		QuaternionFormatHandler );
    AddHandler( "floatformat",			FloatFormatHandler );
	AddHandler( "scale",				ScaleHandler );
    AddHandler( "rotation",				RotationHandler );
    AddHandler( "translation",			TranslationHandler );
    AddHandler( "float",				FloatHandler );
    AddHandler( "keyframes",			KeyFramesHandler );
    AddHandler( "skiplist",				SkipListHandler );
    AddHandler( "keeplist",				KeepListHandler );
    AddHandler( "rootspaceerror",		RootSpaceErrorHandler );
    AddHandler( "joint",				JointHandler );
    AddHandler( "floatchannel",			FloatChannelHandler );
    return ParseXmlFile::Init();
}

static void InitMetaData(AnimMetaData *pMetaData);

int LoadAnimMetaData::Reset(void)
{
    m_loadError = kAnimMetaDataNoError;
	if (m_metaData)
		InitMetaData(m_metaData);
	m_state = AnimMetaDataState();
	m_includes.resize(0);
    return ParseXmlFile::Reset();
}

/// kicks off the parse loop
int LoadAnimMetaData::DoParse()
{
	int result = ParseXml::DoParse();
	if (result)
		return result;
	return m_loadError & ReadXml::kReadXmlErrMask;
}

//implementations for baseclass's virtual methods
void LoadAnimMetaData::StartElement(const char *tagname,
                        const char **atts)
{
    TagHandler handler = GetTagHandler( tagname );
    //dispatch to the handler:
    if (handler) {
        handler(tagname, atts, true, this);
    }
    else {
        /// retrieves the default handler installed by subclass
        TagHandler defHandler = GetDefaultHandler();
        if (defHandler) {
            defHandler(tagname, atts, true, this);
        }
    }
}

void LoadAnimMetaData::EndElement(const char *tagname)
{
    const TagType& tag = TopTag();
    const char* nullatts[1] = {NULL};

    if (tag == tagname) {
        TagHandler handler = GetTagHandler( tag );
        //dispatch to the handler:
        if (handler) {
            handler(tagname, nullatts, false, this);
        }
        else {
            /// retrieves the default handler installed by subclass
            TagHandler defHandler = GetDefaultHandler();
            if (defHandler) {
                defHandler(tagname, nullatts, false, this);
            }
        }
    }
    else {
        m_loadError = kAnimMetaDataTagMismatch;
    }
}

void LoadAnimMetaData::DoCharacterData(void)
{
   //floatList();
   //stringList();
}

/// extracts the current charbuf as a file path:
std::string LoadAnimMetaData::GetCharBufAsFilename() const
{
    std::string filename = GetCharBuf();
    ITFILE::FilePath::CleanPath( filename );
    return filename;
}

int LoadAnimMetaData::ParseIncludeFile(std::string const& filename, std::string const& label)
{
	std::string const& file = filename.empty() ? m_filename : filename;
	std::string const& anim = label.empty() ? m_animname : label;
	{
		for (IncludeList::const_iterator it = m_includes.begin(); it != m_includes.end(); ++it) {
			if (it->first == file && it->second == anim) {
				ReportError(kAnimMetaDataIncludeLoop, std::string("tag 'compression' attrib 'include=") + file + ":" + anim + "' creates dependency loop; ignoring include...");
				return kReadXmlAnimMetaDataError;
			}
		}
	}
	LoadAnimMetaData loadAnimMetaData_Include(m_metaData, file, anim, m_bindposename, m_includes);
	bool result = loadAnimMetaData_Include.Open(file.c_str());
	if (!result) {
		ReportError(kAnimMetaDataFileNotFound, std::string("ProcessMetadataFile: file '") +file+ "' included by '" +m_filename+":"+m_animname+ "' not found\n");
		return kReadXmlAnimMetaDataError;
	}
	int loadError = loadAnimMetaData_Include.DoParse();
	loadAnimMetaData_Include.Close();
	if (loadError) {
		ReportFatalError(kAnimMetaDataFileInvalid, std::string("Include '") +file+":"+anim+ " included by " +m_filename+":"+m_animname+" has fatal errors:");
		CopyNotes( loadAnimMetaData_Include, "\t" );
		return loadError;
	}
	if ((unsigned)loadAnimMetaData_Include.m_eSeverity > (unsigned)kAnimMetaDataSeverityNone) {
		if ((unsigned)loadAnimMetaData_Include.m_eSeverity >= (unsigned)kAnimMetaDataSeverityError) {
			ReportError(kAnimMetaDataFileInvalid, std::string("Include '") +file+":"+anim+ " included by " +m_filename+":"+m_animname+" has errors:");
		} else if ((unsigned)loadAnimMetaData_Include.m_eSeverity >= (unsigned)kAnimMetaDataSeverityWarning) {
			ReportWarning(kAnimMetaDataFileInvalid, std::string("Include '") +file+":"+anim+ " included by " +m_filename+":"+m_animname+" has warnings:");
		} else {
			ReportNote(kAnimMetaDataFileInvalid, std::string("Include '") +file+":"+anim+ " included by " +m_filename+":"+m_animname+":");
		}
		CopyNotes( loadAnimMetaData_Include, "\t" );
	}

	{	// inherit defines from our include
		m_state.m_flags |= (loadAnimMetaData_Include.m_state.m_flags & kStateDefinedMask);
		CompressionLabelMap::const_iterator it = loadAnimMetaData_Include.m_state.m_vectorFormats.begin(), itEnd = loadAnimMetaData_Include.m_state.m_vectorFormats.end();
		for (; it != itEnd; ++it) {
			CompressionLabelMap::iterator itFind = m_state.m_vectorFormats.find( it->first );
			if (itFind != m_state.m_vectorFormats.end()) {
				ReportWarning(kAnimMetaDataAttribFormat, std::string("file '")+file+":"+anim+"' included by '"+m_filename+":"+m_animname+"' redefines vectorformat '"+it->first+"'");
				itFind->second = it->second;
			} else
				m_state.m_vectorFormats[ it->first ] = it->second;
		}
		it = loadAnimMetaData_Include.m_state.m_quaternionFormats.begin(), itEnd = loadAnimMetaData_Include.m_state.m_quaternionFormats.end();
		for (; it != itEnd; ++it) {
			CompressionLabelMap::iterator itFind = m_state.m_quaternionFormats.find( it->first );
			if (itFind != m_state.m_quaternionFormats.end()) {
				ReportWarning(kAnimMetaDataAttribFormat, std::string("file '")+file+":"+anim+"' included by '"+m_filename+":"+m_animname+"' redefines quaternionformat '"+it->first+"'");
				itFind->second = it->second;
			} else
				m_state.m_quaternionFormats[ it->first ] = it->second;
		}
		it = loadAnimMetaData_Include.m_state.m_floatFormats.begin(), itEnd = loadAnimMetaData_Include.m_state.m_floatFormats.end();
		for (; it != itEnd; ++it) {
			CompressionLabelMap::iterator itFind = m_state.m_floatFormats.find( it->first );
			if (itFind != m_state.m_floatFormats.end()) {
				ReportWarning(kAnimMetaDataAttribFormat, std::string("file '")+file+":"+anim+"' included by '"+m_filename+":"+m_animname+"' redefines floatformat '"+it->first+"'");
				itFind->second = it->second;
			} else
				m_state.m_floatFormats[ it->first ] = it->second;
		}
	}
	{
		ChannelSetLabelMap::const_iterator it = loadAnimMetaData_Include.m_state.m_jointSets.begin(), itEnd = loadAnimMetaData_Include.m_state.m_jointSets.end();
		for (; it != itEnd; ++it) {
			ChannelSetLabelMap::iterator itFind = m_state.m_jointSets.find( it->first );
			if (itFind != m_state.m_jointSets.end()) {
				ReportWarning(kAnimMetaDataAttribFormat, std::string("file '")+file+":"+anim+"' included by '"+m_filename+":"+m_animname+"' redefines joint name list '"+it->first+"'");
				itFind->second = it->second;
			} else
				m_state.m_jointSets[ it->first ] = it->second;
		}
		it = loadAnimMetaData_Include.m_state.m_floatChannelSets.begin(), itEnd = loadAnimMetaData_Include.m_state.m_floatChannelSets.end();
		for (; it != itEnd; ++it) {
			ChannelSetLabelMap::iterator itFind = m_state.m_floatChannelSets.find( it->first );
			if (itFind != m_state.m_floatChannelSets.end()) {
				ReportWarning(kAnimMetaDataAttribFormat, std::string("file '")+file+":"+anim+"' included by '"+m_filename+":"+m_animname+"' redefines floatchannel name list '"+it->first+"'");
				itFind->second = it->second;
			} else
				m_state.m_floatChannelSets[ it->first ] = it->second;
		}
	}
	m_state.m_jointModifiedSet.insert(loadAnimMetaData_Include.m_state.m_jointModifiedSet.begin(), loadAnimMetaData_Include.m_state.m_jointModifiedSet.end());
	m_state.m_floatModifiedSet.insert(loadAnimMetaData_Include.m_state.m_floatModifiedSet.begin(), loadAnimMetaData_Include.m_state.m_floatModifiedSet.end());
	return ReadXml::kReadXmlNoError;
}

bool LoadAnimMetaData::Validate()
{
	if (!(m_state.m_flags & kStateKeyframeCompressionDefined)) {
		ReportWarning(kAnimMetaDataAttribFormat, std::string("metadata '")+m_filename+":"+m_animname+"' for bindpose '"+m_bindposename+"' does not define a keyframe compression type; using uniform...");
	}
	return true;
}

//------------------------------------------------------------------------------------------------

bool ProcessMetadataFile( AnimMetaData* metaData, std::string const& filename, std::vector<std::string> const& additionalIncludeFiles, std::string const& animname, std::string const& bindposename )
{
	LoadAnimMetaData loadAnimMetaData(metaData, filename, animname, bindposename);
	loadAnimMetaData.Reset();

	int loadError;
	std::vector<std::string>::const_iterator itInclude = additionalIncludeFiles.begin(), itIncludeEnd = additionalIncludeFiles.end();
	for (; itInclude != itIncludeEnd; ++itInclude) {
		// We allow the auto file is listed last in the -metadata list as a convenience; if so, ignore it here and parse it normally below:
		if (itInclude+1 == itIncludeEnd && *itInclude == filename)
			break;
		// This will detect any inclusion of filename:animname by a file in the -metadata list as a loop;
		// Technically, this is not a real loop, but we're probably OK treating it as one.
		loadError = loadAnimMetaData.ParseIncludeFile( *itInclude, animname );
		if (loadError) {
			IWARN("ProcessMetadataFile: metadata include '%s:%s' has fatal errors:\n", itInclude->c_str(), animname.c_str());
			loadAnimMetaData.PrintAndClearNotes("\t");
			return false;
		}
	}
	if (!filename.empty()) {
		bool bFileOpened = loadAnimMetaData.Open( filename.c_str() );
		if (!bFileOpened) {
			IWARN("ProcessMetadataFile: file '%s' not found\n", filename.c_str());
			return false;
		}
		loadError = loadAnimMetaData.DoParse();
		loadAnimMetaData.Close();
		if (loadError) {
			IWARN("ProcessMetadataFile: metadata '%s:%s' has fatal errors:\n", filename.c_str(), animname.c_str());
			loadAnimMetaData.PrintAndClearNotes("\t");
			return false;
		}
	}
	bool bValid = loadAnimMetaData.Validate();
	AnimMetaDataNoteSeverity eSeverity = loadAnimMetaData.GetSeverity();
	if ((unsigned)eSeverity > (unsigned)kAnimMetaDataSeverityNone) {
		if ((unsigned)eSeverity >= (unsigned)kAnimMetaDataSeverityError) {
			IWARN("ProcessMetadataFile: metadata '%s:%s' (plus includes) has errors:\n", filename.c_str(), animname.c_str());
			loadAnimMetaData.PrintAndClearNotes("\t");
			bValid = false;
		} else if ((unsigned)eSeverity >= (unsigned)kAnimMetaDataSeverityWarning) {
			IWARN("ProcessMetadataFile: metadata '%s:%s' (plus includes) has warnings:\n", filename.c_str(), animname.c_str());
		} else {
			IWARN("ProcessMetadataFile: metadata '%s:%s' (plus includes) has notes:\n", filename.c_str(), animname.c_str());
		}
		loadAnimMetaData.PrintAndClearNotes("\t");
	}
	return bValid;
}

//------------------------------------------------------------------------------------------------

static bool BoolFromString(const char *string, bool &value)
{
	if (!*string		// allow bare attributes - i.e. "rootspaceerrors" without "=1"
	||	!strcmp(string, "1")) {
		value = true;
		return true;
	}
	if (!strcmp(string, "0")) {
		value = false;
		return true;
	}
	if (!strcasecmp(string, "yes") || !strcasecmp(string, "true") || !strcasecmp(string, "on")) {
		value = true;
		return true;
	}
	if (!strcasecmp(string, "no") || !strcasecmp(string, "false") || !strcasecmp(string, "off")) {
		value = false;
		return true;
	}
	return false;
}
static bool IntFromString(const char *string, int &value)
{
	const char* p = string;
	int mask_sign = 0;
	unsigned absvalue = 0;
	value = 0;
	if (!*p)
		return false;
	if (*p == '-') {		// parse initial '-'
		mask_sign = -1;
		++p;
	} else if (*p == '+')	// allow initial '+'
		++p;
	for (; *p; ++p) {
		if (*p < '0' || *p > '9')
			return false;
		if (absvalue > 0x7fffffff/10)
			return false;
		absvalue = absvalue * 10 + (unsigned)(*p - '0');
		if (absvalue > (unsigned)(0x7fffffff - mask_sign))
			return false;
	}
	value = (int)( (absvalue ^ (unsigned)mask_sign) + 1 );
	return true;
}
static bool UintFromString(const char *string, unsigned &value)
{
	const char* p = string;
	unsigned absvalue = 0;
	value = 0;
	if (!*p)
		return false;
	if (*p == '+')			// allow initial '+'
		++p;
	for (; *p; ++p) {
		if (*p < '0' || *p > '9')
			return false;
		if (absvalue > 0xffffffff/10)
			return false;
		absvalue *= 10;
		unsigned value_digit = (unsigned)(*p - '0');
		if (absvalue > 0xffffffff - value_digit)
			return false;
		absvalue += value_digit;
	}
	value = absvalue;
	return true;
}
static bool FloatFromString(const char *string, float &value)
{
	const char* p = string;
	double dfAbsValue = 0.0f;
	float fSign = 1.0f;
	value = 0.0f;
	if (!*p)
		return false;
	if (*p == '-') {		// parse initial '-'
		fSign = -1.0f;
		++p;
	} else if (*p == '+') {	// allow initial '+'
		++p;
	}
	// initial character parsing
	if (*p == '.' && *(p+1) >= '0' && *(p+1) <= '9') {
		// allow mantissa to start with decimal point if it does not end after the decimal point - i.e. ".1" or "-.2e-2"
	} else {
		if (*p < '0' || *p > '9')
			return false;
		dfAbsValue = (double)(*p - '0');
		// mantissa integer parsing
		for (++p; ; ++p) {
			if (*p == '\0')
				goto FloatFromString_parse_done;
			if (*p == '.')
				break;
			if ((*p | 0x20) == 'e') // *p == 'e' || *p == 'E'
				goto FloatFromString_parse_exponent;
			if (*p < '0' || *p > '9')
				return false;
			dfAbsValue = dfAbsValue * 10.0 + (double)(*p - '0');
		}
	}
	// mantissa fractional part parsing
	// allows mantissa to end with a decimal point - i.e. "1." or "-2.e+2"
	++p;
	for (double dfDecimalFactor = 0.1; *p; ++p, dfDecimalFactor *= 0.1) {
		if ((*p | 0x20) == 'e') // *p == 'e' || *p == 'E'
			goto FloatFromString_parse_exponent;
		if (*p < '0' || *p > '9')
			return false;
		dfAbsValue += dfDecimalFactor * (double)(*p - '0');
	}
FloatFromString_parse_done:
	value = fSign * (float)dfAbsValue;
	return true;
	// exponent parsing
FloatFromString_parse_exponent:
	int exp = 0;
	if (!IntFromString(++p, exp))
		return false;
	double dfExp = pow(10.0, (double)exp);
	value = fSign * (float)(dfAbsValue * dfExp);
	return true;
}

static std::string AnimMetaDataKeyCompressionFlagsToString(U16 flags)
{
	switch (flags & kAnimMetaDataFlagKeysMask) {
	case kAnimMetaDataFlagKeysUniform:		return std::string("uniform");
	case kAnimMetaDataFlagKeysShared:		return std::string("shared");
	case kAnimMetaDataFlagKeysUnshared:		return std::string("unshared");
	case kAnimMetaDataFlagKeysHermite:		return std::string("hermite");
	default:								return std::string("<UNKNOWN>");
	}
}
static bool AnimMetaDataKeyCompressionFlagsFromString(const char *string, U16 &flags)
{
	if (!strcasecmp(string, "unshared")) {
		flags = kAnimMetaDataFlagKeysUnshared;
		return true;
	}
	if (!strcasecmp(string, "shared")) {
		flags = kAnimMetaDataFlagKeysShared;
		return true;
	}
	if (!strcasecmp(string, "uniform")) {
		flags = kAnimMetaDataFlagKeysUniform;
		return true;
	}
	if (!strcasecmp(string, "hermite")) {
		flags = kAnimMetaDataFlagKeysHermite;
		return true;
	}
	return false;
}

/*
static std::string VectorAnimCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctVec3Uncompressed:		return std::string("uncompressed");
	case kAcctVec3Float16:			return std::string("float16");
	case kAcctVec3Range:			return std::string("range");
	case kAcctVec3Auto:				return std::string("auto");
	default:						break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
static std::string VectorConstCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctConstVec3Uncompressed:	return std::string("uncompressed");
	case kAcctConstVec3Float16:			return std::string("float16");
	case kAcctConstVec3Auto:			return std::string("auto");
	default:							break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
*/
static bool VectorAnimCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctVec3Uncompressed;
		return true;
	}
	if (!strcasecmp(name, "float16")) {
		compressionType = kAcctVec3Float16;
		return true;
	}
	if (!strcasecmp(name, "range")) {
		compressionType = kAcctVec3Range;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctVec3Auto;
		return true;
	}
	return false;
}
static bool VectorConstCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctConstVec3Uncompressed;
		return true;
	}
	if (!strcasecmp(name, "float16")) {
		compressionType = kAcctConstVec3Float16;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctConstVec3Auto;
		return true;
	}
	return false;
}
static bool VectorCompressionBitFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!VectorAnimCompressionKindFromString(string, format.m_compressionType))
		return false;
	format.m_flags = (unsigned)( format.m_compressionType - kAcctVec3Uncompressed ) & kAnimMetaDataMaskFormatLabelIndex;
	format.m_fErrorTolerance = 0.0f;
	switch (format.m_compressionType) {
	case kAcctVec3Uncompressed:	format.m_bitFormat = kVbpfVec3Uncompressed;		break;
	case kAcctVec3Float16:		format.m_bitFormat = kVbpfVec3Float16;			break;
	case kAcctVec3Range:		format.m_bitFormat = kVbpfVec3Range_Default;	break;
	case kAcctVec3Auto:
	default:					format.m_bitFormat = kVbpfNone;					break;
	}
	return true;
}
static bool VectorConstCompressionFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!VectorConstCompressionKindFromString(string, format.m_constCompressionType))
		return false;
	format.m_fConstErrorTolerance = kfConstErrorToleranceUseDefault;
	return true;
}
static bool VectorCompressionBitFormatReadProps(const ITDATAMGR::PropList& tagProps, AnimMetaDataTrackBitFormat &format, LoadAnimMetaData *pLoader, std::string const& strPrefix)
{
	bool bError = false;
	std::string const *constformat = tagProps.Value( "constformat" );
	if (constformat) {
		if (!VectorConstCompressionKindFromString( constformat->c_str(), format.m_constCompressionType )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'constformat' invalid value '"+ *constformat +"'" );
			bError = true;
		}
	}
	std::string const *consterror = tagProps.Value( "consterror" );
	if (consterror) {
		if (!strcasecmp(consterror->c_str(), "auto")) {
			format.m_fConstErrorTolerance = kConstErrorToleranceAuto;
		} else if (!FloatFromString( consterror->c_str(), format.m_fConstErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'consterror' badly formed float value '"+ *consterror +"'" );
			bError = true;
		} else {
			format.m_fConstErrorTolerance = fabsf( format.m_fConstErrorTolerance );
		}
	}

	if (IsVariableBitPackedFormat(format.m_compressionType)) {
		std::string const *bitformat = tagProps.Value( "bitformat" );
		if (bitformat) {
			if (!strcasecmp(bitformat->c_str(), "generate")) {
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormat;
			} else {
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormat;
				int nx, ny, nz;
				if (3 != sscanf(bitformat->c_str(), "%d,%d,%d", &nx, &ny, &nz) || (nx < 0 || ny < 0 || nz < 0)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' badly formed value '"+ *bitformat +"'" );
					bError = true;
				} else if (nx > 32 || ny > 32 || nz > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for vector compression (component >32 bits)" );
					bError = true;
				} else if ((nx + ny + nz) > 64) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for vector compression (total >64 bytes)" );
					bError = true;
				} else if ((nx & 0x7) + ny > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for vector compression (y crosses >4 bytes)" );
					bError = true;
				} else if (((nx + ny) & 0x7) + nz > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for vector compression (z crosses >4 bytes)" );
					bError = true;
				} else {
					format.m_bitFormat = Vec3BitPackingFormat((U8)nx, (U8)ny, (U8)nz, (U8)(nx+ny+nz));
				}
			}
		}
	}
	std::string const *error = tagProps.Value( "error" );
	if (error) {
		if (!FloatFromString( error->c_str(), format.m_fErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'error' badly formed float value '"+ *error +"'" );
			bError = true;
		} else
			format.m_fErrorTolerance = fabsf( format.m_fErrorTolerance );
	}
	std::string const *shared = tagProps.Value( "shared" );
	if (shared) {
		bool value;
		if (!BoolFromString(shared->c_str(), value)) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'shared' badly formed bool value '"+ *shared +"'" );
			bError = true;
		} else {
			if (value)
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormatShared;
			else
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormatShared;
		}
	}
	return !bError;
}

/*
static std::string QuaternionAnimCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctQuatUncompressed:		return std::string("uncompressed");
	case kAcctQuatSmallestThree:	return std::string("smallest3");
	case kAcctQuatLog:				return std::string("log");
	case kAcctQuatLogPca:			return std::string("logpca");
	case kAcctQuatAuto:				return std::string("auto");
	default:						break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
static std::string QuaternionConstCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctConstQuatUncompressed:	return std::string("uncompressed");
	case kAcctConstQuat48SmallestThree:	return std::string("smallest3_48");
	case kAcctConstQuatAuto:			return std::string("auto");
	default:							break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
*/
static bool QuaternionAnimCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctQuatUncompressed;
		return true;
	}
	if (!strcasecmp(name, "smallest3")) {
		compressionType = kAcctQuatSmallestThree;
		return true;
	}
	if (!strcasecmp(name, "log")) {
		compressionType = kAcctQuatLog;
		return true;
	}
	if (!strcasecmp(name, "logpca")) {
		compressionType = kAcctQuatLogPca;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctQuatAuto;
		return true;
	}
	return false;
}
static bool QuaternionConstCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctConstQuatUncompressed;
		return true;
	}
	if (!strcasecmp(name, "smallest3_48")) {
		compressionType = kAcctConstQuat48SmallestThree;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctConstQuatAuto;
		return true;
	}
	return false;
}
static bool QuaternionCompressionBitFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!QuaternionAnimCompressionKindFromString(string, format.m_compressionType))
		return false;
	format.m_flags = (unsigned)( format.m_compressionType - kAcctQuatUncompressed ) & kAnimMetaDataMaskFormatLabelIndex;
	format.m_fErrorTolerance = 0.0f;
	switch (format.m_compressionType) {
	case kAcctQuatUncompressed:		format.m_bitFormat = kVbpfQuatUncompressed;				break;
	case kAcctQuatSmallestThree:	format.m_bitFormat = kVbpfQuatSmallestThree_Default;	break;
	case kAcctQuatLog:
	case kAcctQuatLogPca:			format.m_bitFormat = kVbpfQuatLog_Default;				break;
	case kAcctQuatAuto:
	default:						format.m_bitFormat = kVbpfNone;							break;
	}
	return true;
}
static bool QuaternionConstCompressionFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!QuaternionConstCompressionKindFromString(string, format.m_constCompressionType))
		return false;
	format.m_fConstErrorTolerance = kfConstErrorToleranceUseDefault;
	return true;
}
static bool QuaternionCompressionBitFormatReadProps(const ITDATAMGR::PropList& tagProps, AnimMetaDataTrackBitFormat &format, LoadAnimMetaData *pLoader, std::string const& strPrefix)
{
	bool bError = false;
	std::string const *constformat = tagProps.Value( "constformat" );
	if (constformat) {
		if (!QuaternionConstCompressionKindFromString( constformat->c_str(), format.m_constCompressionType )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'constformat' invalid value '"+ *constformat +"'" );
			bError = true;
		}
	}
	std::string const *consterror = tagProps.Value( "consterror" );
	if (consterror) {
		if (!strcasecmp(consterror->c_str(), "auto")) {
			format.m_fConstErrorTolerance = kConstErrorToleranceAuto;
		} else if (!FloatFromString( consterror->c_str(), format.m_fConstErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'consterror' badly formed float value '"+ *consterror +"'" );
			bError = true;
		} else {
			format.m_fConstErrorTolerance = fabsf( format.m_fConstErrorTolerance );
		}
	}

	if (IsVariableBitPackedFormat(format.m_compressionType)) {
		std::string const *bitformat = tagProps.Value( "bitformat" );
		if (bitformat) {
			format.m_bitFormat = kVbpfNone;
			if (!strcasecmp(bitformat->c_str(), "generate")) {
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormat;
			} else {
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormat;
				int nx, ny, nz;
				if (3 != sscanf(bitformat->c_str(), "%d,%d,%d", &nx, &ny, &nz) || (nx < 0 || ny < 0 || nz < 0)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' badly formed value '"+ *bitformat +"'" );
					bError = true;
				} else if (nx > 32 || ny > 32 || nz > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion compression (component >32 bits)" );
					bError = true;
				} else if (nx + ny + nz > 64) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion compression (total >64 bits)" );
					bError = true;
				} else if ((format.m_compressionType == kAcctQuatSmallestThree) && (nz > 30)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion smallest3 compression (z+index > 32 bits)" );
					bError = true;
				} else if ((format.m_compressionType == kAcctQuatSmallestThree) && ((nx + ny + nz + 2) > 64)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion smallest3 compression (total+index >64 bits)" );
					bError = true;
				} else if ((nx & 0x7) + ny > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion compression (y crosses >4 bytes)" );
					bError = true;
				} else if (((nx + ny) & 0x7) + nz > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion compression (z crosses >4 bytes)" );
					bError = true;
				} else if ((format.m_compressionType == kAcctQuatSmallestThree) && (((nx + ny) & 0x7) + nz + 2 > 32)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'bitformat' value '"+ *bitformat +"' not valid for quaternion smallest3 compression (z+index crosses >4 bytes)" );
					bError = true;
				} else {
					if (format.m_compressionType == kAcctQuatSmallestThree) {
						format.m_bitFormat = Vec3BitPackingFormat((U8)nx, (U8)ny, (U8)nz, (U8)(nx+ny+nz+2/*index*/));
					} else {
						format.m_bitFormat = Vec3BitPackingFormat((U8)nx, (U8)ny, (U8)nz, (U8)(nx+ny+nz));
					}
				}
			}
		}
	}
	std::string const *error = tagProps.Value( "error" );
	if (error) {
		if (!FloatFromString( error->c_str(), format.m_fErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'error' badly formed float value '"+ *error +"'" );
			bError = true;
		} else
			format.m_fErrorTolerance = fabsf( format.m_fErrorTolerance );
	}
	std::string const *shared = tagProps.Value( "shared" );
	if (shared) {
		bool value;
		if (!BoolFromString(shared->c_str(), value)) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'shared' badly formed bool value '"+ *shared +"'" );
			bError = true;
		} else {
			if (value)
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormatShared;
			else
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormatShared;
		}
	}
	return !bError;
}

/*
static std::string FloatAnimCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctFloatUncompressed:	return std::string("uncompressed");
	case kAcctFloatAuto:			return std::string("auto");
	default:						break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
static std::string FloatConstCompressionKindToString(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctConstFloatUncompressed:	return std::string("uncompressed");
	case kAcctConstFloatAuto:			return std::string("auto");
	default:							break;
	}
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)compressionType);
	return std::string("<UNKNOWN:") + buf + ">";
}
*/
static bool FloatAnimCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctFloatUncompressed;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctFloatAuto;
		return true;
	}
	return false;
}
static bool FloatConstCompressionKindFromString(const char *name, AnimChannelCompressionType &compressionType)
{
	if (!strcasecmp(name, "uncompressed")) {
		compressionType = kAcctConstFloatUncompressed;
		return true;
	}
	if (!strcasecmp(name, "auto")) {
		compressionType = kAcctConstFloatAuto;
		return true;
	}
	return false;
}
static bool FloatCompressionBitFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!FloatAnimCompressionKindFromString(string, format.m_compressionType))
		return false;
	format.m_flags = (unsigned)( format.m_compressionType - kAcctFloatUncompressed ) & kAnimMetaDataMaskFormatLabelIndex;
	format.m_fErrorTolerance = 0.0f;
	switch (format.m_compressionType) {
	case kAcctFloatUncompressed:	format.m_bitFormat = kVbpfFloatUncompressed;	break;
	case kAcctFloatAuto:
	default:						format.m_bitFormat = kVbpfNone;					break;
	}
	return true;
}
static bool FloatConstCompressionFormatFromString(const char *string, AnimMetaDataTrackBitFormat &format)
{
	if (!FloatConstCompressionKindFromString(string, format.m_constCompressionType))
		return false;
	format.m_fConstErrorTolerance = kfConstErrorToleranceUseDefault;
	return true;
}
static bool FloatCompressionBitFormatReadProps(const ITDATAMGR::PropList& tagProps, AnimMetaDataTrackBitFormat &format, LoadAnimMetaData *pLoader, std::string const& strPrefix)
{
	bool bError = false;
	std::string const *constformat = tagProps.Value( "constformat" );
	if (constformat) {
		if (!FloatConstCompressionKindFromString( constformat->c_str(), format.m_constCompressionType )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'constformat' invalid value '"+ *constformat +"'" );
			bError = true;
		}
	}
	std::string const *consterror = tagProps.Value( "consterror" );
	if (consterror) {
		if (!strcasecmp(consterror->c_str(), "auto")) {
			format.m_fConstErrorTolerance = kConstErrorToleranceAuto;
		} else if (!FloatFromString( consterror->c_str(), format.m_fConstErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'consterror' badly formed float value '"+ *consterror +"'" );
			bError = true;
		} else {
			format.m_fConstErrorTolerance = fabsf( format.m_fConstErrorTolerance );
		}
	}

	if (IsVariableBitPackedFormat(format.m_compressionType)) {
		// technically, this code is unused, as we have no error based float compression so far...
		std::string const *bitformat = tagProps.Value( "bitformat" );
		if (bitformat) {
			if (!strcasecmp(bitformat->c_str(), "generate")) {
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormat;
			} else {
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormat;
				int nbits;
				if (1 != sscanf(bitformat->c_str(), "%d", &nbits) || (nbits < 0)) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"(float) attrib 'bitformat' badly formed value '"+ *bitformat +"'" );
					bError = true;
				} else if (nbits > 32) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"(float) attrib 'bitformat' value '"+ *bitformat +"' not valid for float compression (>32 bits)" );
					bError = true;
				} else {
					format.m_bitFormat = Vec3BitPackingFormat((U8)nbits, (U8)0, (U8)0, (U8)nbits);
				}
			}
		}
	}
	std::string const *error = tagProps.Value( "error" );
	if (error) {
		if (!FloatFromString( error->c_str(), format.m_fErrorTolerance )) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'error' badly formed float value '"+ *error +"'" );
			bError = true;
		} else
			format.m_fErrorTolerance = fabsf( format.m_fErrorTolerance );
	}
	std::string const *shared = tagProps.Value( "shared" );
	if (shared) {
		bool value;
		if (!BoolFromString(shared->c_str(), value)) {
			pLoader->ReportError(kAnimMetaDataAttribFormat, strPrefix +"attrib 'shared' badly formed bool value '"+ *shared +"'" );
			bError = true;
		} else {
			if (value)
				format.m_flags |= kAnimMetaDataFlagGenerateBitFormatShared;
			else
				format.m_flags &=~kAnimMetaDataFlagGenerateBitFormatShared;
		}
	}
	return true;
}

static const char *s_szTagName[] = {
	"<ROOT>", "<UNKNOWN:0x00000001>",
	"compression",
	"vectorformat", "quaternionformat", "floatformat",
	"scale", "rotation", "translation", "float"
	"keyframes", "skiplist", "keeplist",
	"rootspaceerror",
	"joint", "floatchannel" };
std::string GetTagName(AnimMetaDataTag tag)
{
	if ((int)tag >= (int)kAnimMetaDataTagROOT || (int)tag < (int)kNumAnimMetaDataTags)
		return std::string(s_szTagName[tag]);
	char buf[16];
	sprintf(buf, "0x%08x", (unsigned)tag);
	return std::string("<UNKNOWN:") + buf + ">";
}
std::string GetTagScopeName(AnimMetaDataTag tag)
{
	if (tag == kAnimMetaDataTagROOT)
		return std::string("<ROOT>");
	return std::string("tag '") + GetTagName(tag) + std::string("'");
}

static void InitMetaData(AnimMetaData *pMetaData)
{
	// Set MetaData defaults - all uncompressed
	*pMetaData = AnimMetaData();
	VectorCompressionBitFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_scale.m_format);
	VectorConstCompressionFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_scale.m_format);
	pMetaData->m_defaultCompression.m_scale.m_keyFrames = AnimMetaDataTrackKeyFrames();
	QuaternionCompressionBitFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_rotation.m_format);
	QuaternionConstCompressionFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_rotation.m_format);
	pMetaData->m_defaultCompression.m_rotation.m_keyFrames = AnimMetaDataTrackKeyFrames();
	VectorCompressionBitFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_translation.m_format);
	VectorConstCompressionFormatFromString("uncompressed", pMetaData->m_defaultCompression.m_translation.m_format);
	pMetaData->m_defaultCompression.m_translation.m_keyFrames = AnimMetaDataTrackKeyFrames();
	FloatCompressionBitFormatFromString("uncompressed", pMetaData->m_defaultCompressionFloat.m_format);
	FloatConstCompressionFormatFromString("uncompressed", pMetaData->m_defaultCompressionFloat.m_format);
	pMetaData->m_defaultCompressionFloat.m_keyFrames = AnimMetaDataTrackKeyFrames();
	pMetaData->m_sharedKeyFrames = AnimMetaDataTrackKeyFrames();
}

//------------------------------------------------------------------------------------------------

static int DEFAULT_Handler(const char* tagname, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		pLoader->EnterScope( kAnimMetaDataTagUNKNOWN );
		pLoader->SetInvalidScope();		//unknown tag - ignore all scopes within this one...
		pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("unrecognized tag '")+ tagname +"'; ignoring scope...");
		return 0;
	} else {
		pLoader->ExitScope();
	}
	return 0;
}

static int CompressionHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagCompression );
		if (tag != kAnimMetaDataTagROOT) {
			pLoader->SetInvalidScope();	// invalid compression scope - ignore all scopes within this one...
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'compression' not expected within ") + GetTagScopeName(tag) +" scope");
			return 0;
		}

		AnimMetaData *pMetaData = pLoader->m_metaData;
		// handle attributes - animation, bindpose, include, keys
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::string const *bindpose = tagProps.Value( "bindpose" );
		if (bindpose) {
			if (pLoader->m_bindposename != *bindpose) {
				pLoader->SetInvalidScope();	// invalid compression scope - ignore all scopes within this one...
				return 0;
			}
		}
		std::string const *animation = tagProps.Value( "animation" );
		if (animation) {
			if (pLoader->m_animname != *animation) {
				pLoader->SetInvalidScope();	// invalid compression scope - ignore all scopes within this one...
				return 0;
			}
		}

		std::string const *include = tagProps.Value( "include" );
		if (include) {
			std::string const& includeList = *include;
			std::string::size_type pos = 0;
			do {
				std::string::size_type posComma = includeList.find_first_of(',', pos);
				std::string inc = (posComma != std::string::npos) ? includeList.substr(pos, posComma-pos) : includeList.substr(pos);
				std::string::size_type posColon = inc.find_first_of(':');
				std::string file, label;
				if (posColon != std::string::npos) {
					file = inc.substr(0, posColon);
					label = inc.substr(posColon+1);
				} else {
					std::string::size_type posExt = inc.find_last_of('.');
					if (posExt != std::string::npos && !strcasecmp(inc.substr(posExt).c_str(), ".xml"))
						file = inc;
					else
						label = inc;
				}
				int error = pLoader->ParseIncludeFile( file, label );
				if (error)
					return error;
				pos = posComma;
			} while (pos != std::string::npos);
		}

		// handle attributes - keys, globaloptimization
		std::string const *keys = tagProps.Value( "keys" );
		if (keys) {
			U16 keyframeCompressionFlags;	// default value
			if (!AnimMetaDataKeyCompressionFlagsFromString(keys->c_str(), keyframeCompressionFlags)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'compression' attrib 'keys' unknown value '") + *keys + "'");
			} else if (state.m_flags & kStateKeyframeCompressionDefined) {
				if ((pMetaData->m_flags & kAnimMetaDataFlagKeysMask) != keyframeCompressionFlags)
					pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'compression' attrib 'keys' value '") + *keys + "' does not match previously defined value '"+ AnimMetaDataKeyCompressionFlagsToString(pMetaData->m_flags) +"'; ignoring...");
			} else {
				pMetaData->m_flags |= keyframeCompressionFlags;
				if (keyframeCompressionFlags == kAnimMetaDataFlagKeysUnshared) {
					// set defaults for unshared keys
					pMetaData->m_maxKeysPerBlock = 33;
					pMetaData->m_maxBlockSize = 2048;
				}
				state.m_flags |= kStateKeyframeCompressionDefined;
			}
		}
		std::string const *rootspaceerrors = tagProps.Value( "rootspaceerrors" );
		if (rootspaceerrors) {
			bool bEnable;
			if (!BoolFromString(rootspaceerrors->c_str(), bEnable)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'compression' attrib 'rootspaceerrors' badly formed bool value '") + *rootspaceerrors + "'");
			} else {
				if (bEnable)
					pMetaData->m_flags |= kAnimMetaDataFlagRootSpaceErrors;
				else
					pMetaData->m_flags &=~kAnimMetaDataFlagRootSpaceErrors;
			}
		}
		std::string const *deltaoptimization = tagProps.Value( "deltaoptimization" );
		if (deltaoptimization) {
			bool bEnable;
			if (!BoolFromString(deltaoptimization->c_str(), bEnable)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'compression' attrib 'deltaoptimization' badly formed bool value '") + *deltaoptimization + "'");
			} else {
				if (bEnable)
					pMetaData->m_flags |= (kAnimMetaDataFlagRootSpaceErrors|kAnimMetaDataFlagDeltaOptimization);
				else
					pMetaData->m_flags &=~kAnimMetaDataFlagDeltaOptimization;
			}
		}
		std::string const *globaloptimization = tagProps.Value( "globaloptimization" );
		if (globaloptimization) {
			bool bEnable;
			if (!BoolFromString(globaloptimization->c_str(), bEnable)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'compression' attrib 'globaloptimization' badly formed bool value '") + *globaloptimization + "'");
			} else {
				if (bEnable)
					pMetaData->m_flags |= kAnimMetaDataFlagGlobalOptimization;
				else
					pMetaData->m_flags &=~kAnimMetaDataFlagGlobalOptimization;
			}
		}
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			AnimMetaData *pMetaData = pLoader->m_metaData;
			// update defaults for all unmodified joints and float channels,
			// but leave them marked as unmodified for inheritance purposes:
			unsigned i;
			for (i = 0; i < pMetaData->m_jointNames.size(); ++i)
				if (!state.m_jointModifiedSet.count(i))
					pMetaData->m_jointCompression[i] = pMetaData->m_defaultCompression;
			for (i = 0; i < pMetaData->m_floatNames.size(); ++i)
				if (!state.m_floatModifiedSet.count(i))
					pMetaData->m_floatCompression[i] = pMetaData->m_defaultCompressionFloat;
		}
	}
	return 0;
}

static int VectorFormatHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagVectorFormat );
		if (tag != kAnimMetaDataTagCompression) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'vectorformat' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - label, format, bitformat, error, consterror, shared
//		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::string const *label = tagProps.Value( "label" );
		if (!label) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'vectorformat' attrib 'label' is required; ignoring tag..."));
			return 0;
		}
		// read default compression type
		AnimChannelCompressionType compressionType;
		AnimMetaDataTrackBitFormat vectorFormat(kActVec3);
		if (VectorAnimCompressionKindFromString(label->c_str(), compressionType)) {	// label conflict
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'vectorformat' attrib 'label' value '") + *label + std::string("' conflicts with builtin type; ignoring tag..."));
			return 0;
		}
		unsigned label_index;
		{
			CompressionLabelMap::iterator itFind = state.m_vectorFormats.find(*label);
			if (itFind != state.m_vectorFormats.end()) {
				pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'vectorformat' label '")+ *label +"' overwrites previously defined label\n");
				vectorFormat = itFind->second;
				label_index = vectorFormat.m_flags & kAnimMetaDataMaskFormatLabelIndex;
			} else {
				label_index = (unsigned)( (kAcctVec3NumMetadataCompressionTypes + state.m_vectorFormats.size()) & kAnimMetaDataMaskFormatLabelIndex );
			}
		}

		std::string const *format = tagProps.Value("format");
		if (!format) {
			VectorCompressionBitFormatFromString("uncompressed", vectorFormat);
		} else if (!VectorCompressionBitFormatFromString(format->c_str(), vectorFormat)) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'vectorformat' attrib 'format' unknown value '") + *format + std::string("'"));
			return 0;
		}
		// read overrides
		if (!VectorCompressionBitFormatReadProps(tagProps, vectorFormat, pLoader, "tag 'vectorformat' ")) {
			// continue after these errors...
		}
		vectorFormat.m_flags = (vectorFormat.m_flags &~ kAnimMetaDataMaskFormatLabelIndex) | label_index;

		// store compression[label]
		state.m_vectorFormats[*label] = vectorFormat;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static int QuaternionFormatHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagQuaternionFormat );
		if (tag != kAnimMetaDataTagCompression) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'quaternionformat' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - label, format, bitformat, error, consterror, shared
//		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::string const *label = tagProps.Value( "label" );
		if (!label) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'quaternionformat' attrib 'label' is required; ignoring tag..."));
			return 0;
		}

		// read default compression type
		AnimChannelCompressionType compressionType;
		AnimMetaDataTrackBitFormat quaternionFormat(kActQuat);
		if (QuaternionAnimCompressionKindFromString(label->c_str(), compressionType)) {	// label conflict
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'quaternionformat' attrib 'label' value '") + *label + std::string("' conflicts with builtin type; ignoring tag..."));
			return 0;
		}
		unsigned label_index;
		{
			CompressionLabelMap::iterator itFind = state.m_quaternionFormats.find(*label);
			if (itFind != state.m_quaternionFormats.end()) {
				pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'quaternionformat' label '")+ *label +"' overwrites previously defined label\n");
				quaternionFormat = itFind->second;
				label_index = quaternionFormat.m_flags & kAnimMetaDataMaskFormatLabelIndex;
			} else {
				label_index = (unsigned)( (kAcctQuatNumMetadataCompressionTypes + state.m_quaternionFormats.size() ) & kAnimMetaDataMaskFormatLabelIndex );
			}
		}

		std::string const *format = tagProps.Value("format");
		if (!format) {
			QuaternionCompressionBitFormatFromString("uncompressed", quaternionFormat);
		} else if (!QuaternionCompressionBitFormatFromString(format->c_str(), quaternionFormat)) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'quaternionformat' attrib 'format' unknown value '") + *format + std::string("'; ignoring tag..."));
			return 0;
		}
		// read overrides
		if (!QuaternionCompressionBitFormatReadProps(tagProps, quaternionFormat, pLoader, "tag 'quaternionformat' ")) {
			// continue after these errors...
		}
		quaternionFormat.m_flags = (quaternionFormat.m_flags &~ kAnimMetaDataMaskFormatLabelIndex) | label_index;

		// store compression[label]
		state.m_quaternionFormats[*label] = quaternionFormat;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static int FloatFormatHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagFloatFormat );
		if (tag != kAnimMetaDataTagCompression) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'floatformat' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - label, format, bitformat, error, consterror, shared
//		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::string const *label = tagProps.Value( "label" );
		if (!label) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'floatformat' attrib 'label' is required; ignoring tag..."));
			return 0;
		}

		// read default compression type
		AnimChannelCompressionType compressionType;
		AnimMetaDataTrackBitFormat floatFormat(kActFloat);
		if (FloatAnimCompressionKindFromString(label->c_str(), compressionType)) {	// label conflict
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'floatformat' attrib 'label' value '") + *label + std::string("' conflicts with builtin type; ignoring tag..."));
			return 0;
		}
		unsigned label_index;
		{
			CompressionLabelMap::iterator itFind = state.m_floatFormats.find(*label);
			if (itFind != state.m_floatFormats.end()) {
				pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'floatformat' label '")+ *label +"' overwrites previously defined label\n");
				floatFormat = itFind->second;
				label_index = floatFormat.m_flags & kAnimMetaDataMaskFormatLabelIndex;
			} else {
				label_index = (unsigned)( (kAcctFloatNumMetadataCompressionTypes + state.m_floatFormats.size() ) & kAnimMetaDataMaskFormatLabelIndex );
			}
		}

		std::string const *format = tagProps.Value("format");
		if (!format) {
			FloatCompressionBitFormatFromString("uncompressed", floatFormat);
		} else if (!FloatCompressionBitFormatFromString(format->c_str(), floatFormat)) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'floatformat' attrib 'format' unknown value '") + *format + std::string("'; ignoring tag..."));
			return 0;
		}
		// read overrides
		if (!FloatCompressionBitFormatReadProps(tagProps, floatFormat, pLoader, "tag 'floatformat' ")) {
			// continue after these errors...
		}
		floatFormat.m_flags = (floatFormat.m_flags &~ kAnimMetaDataMaskFormatLabelIndex) | label_index;

		// store compression[label]
		state.m_floatFormats[*label] = floatFormat;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static int ScaleHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagScale );
		state.m_trackTag = kAnimMetaDataTagScale;
		if (tag != kAnimMetaDataTagCompression &&
			tag != kAnimMetaDataTagJoint) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'scale' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - format, bitformat, error, consterror, shared
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		// read base compression type
		AnimMetaDataTrackCompressionMethod method = pMetaData->m_defaultCompression.m_scale;
		std::string const *format = tagProps.Value("format");
		if (format && !VectorCompressionBitFormatFromString(format->c_str(), method.m_format)) {
			CompressionLabelMap::const_iterator itLabel = state.m_vectorFormats.find(*format);
			if (itLabel != state.m_vectorFormats.end()) {
				method.m_format = itLabel->second;
			} else {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'scale' attrib 'format' unknown value '") + *format + std::string("'; using default compression..."));
			}
		}
		// read overrides
		if (!VectorCompressionBitFormatReadProps(tagProps, method.m_format, pLoader, "tag 'scale' ")) {
			// continue after these errors...
		}

		if (state.m_flags & kStateInJointScope) {
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_jointModifiedSet.count(*it)) {
					pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
					state.m_jointModifiedSet.emplace(*it);
				}
				pMetaData->m_jointCompression[ *it ].m_scale = method;
			}
		} else
			pMetaData->m_defaultCompression.m_scale = method;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
				// once we process the default scale tag, the default value is set for the rest of the compression block :
				state.m_flags |= kStateDefaultScaleCompressionDefined;
			}
			state.m_trackTag = kAnimMetaDataTagROOT;
		}
	}
	return 0;
}

static int RotationHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagRotation );
		state.m_trackTag = kAnimMetaDataTagRotation;
		if (tag != kAnimMetaDataTagCompression &&
			tag != kAnimMetaDataTagJoint) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'rotation' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - format, bitformat, error, consterror, shared
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		// read base compression type
		AnimMetaDataTrackCompressionMethod method = pMetaData->m_defaultCompression.m_rotation;
		std::string const *format = tagProps.Value("format");
		if (format && !QuaternionCompressionBitFormatFromString(format->c_str(), method.m_format)) {
			CompressionLabelMap::const_iterator itLabel = state.m_quaternionFormats.find(*format);
			if (itLabel != state.m_quaternionFormats.end()) {
				method.m_format = itLabel->second;
			} else {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'rotation' attrib 'format' unknown value '") + *format + std::string("'; using default compression..."));
			}
		}
		// read overrides
		if (!QuaternionCompressionBitFormatReadProps(tagProps, method.m_format, pLoader, "tag 'rotation' ")) {
			// continue after these errors...
		}

		if (state.m_flags & kStateInJointScope) {
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_jointModifiedSet.count(*it)) {
					pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
					state.m_jointModifiedSet.emplace(*it);
				}
				pMetaData->m_jointCompression[ *it ].m_rotation = method;
			}
		} else
			pMetaData->m_defaultCompression.m_rotation = method;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
				// once we process the default rotation tag, the default value is set for the rest of the compression block :
				state.m_flags |= kStateDefaultRotationCompressionDefined;
			}
			state.m_trackTag = kAnimMetaDataTagROOT;
		}
	}
	return 0;
}

static int TranslationHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagTranslation );
		state.m_trackTag = kAnimMetaDataTagTranslation;
		if (tag != kAnimMetaDataTagCompression &&
			tag != kAnimMetaDataTagJoint) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'translation' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - format, bitformat, error, consterror, shared
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		// read base compression type
		AnimMetaDataTrackCompressionMethod method = pMetaData->m_defaultCompression.m_translation;
		std::string const *format = tagProps.Value("format");
		if (format && !VectorCompressionBitFormatFromString(format->c_str(), method.m_format)) {
			CompressionLabelMap::const_iterator itLabel = state.m_vectorFormats.find(*format);
			if (itLabel != state.m_vectorFormats.end()) {
				method.m_format = itLabel->second;
			} else {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'translation' attrib 'format' unknown value '") + *format + std::string("'; using default compression..."));
			}
		}
		// read overrides
		if (!VectorCompressionBitFormatReadProps(tagProps, method.m_format, pLoader, "tag 'translation' ")) {
			// continue after these errors...
		}

		if (state.m_flags & kStateInJointScope) {
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_jointModifiedSet.count(*it)) {
					pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
					state.m_jointModifiedSet.emplace(*it);
				}
				pMetaData->m_jointCompression[ *it ].m_translation = method;
			}
		} else
			pMetaData->m_defaultCompression.m_translation = method;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
				// once we process the default trans tag, the default value is set for the rest of the compression block :
				state.m_flags |= kStateDefaultTranslationCompressionDefined;
			}
			state.m_trackTag = kAnimMetaDataTagROOT;
		}
	}
	return 0;
}

static int FloatHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagFloat );
		state.m_trackTag = kAnimMetaDataTagFloat;
		if (tag != kAnimMetaDataTagCompression &&
			tag != kAnimMetaDataTagFloatChannel) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'float' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - format, bitformat, error, consterror, shared
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		// read base compression type
		AnimMetaDataTrackCompressionMethod method = pMetaData->m_defaultCompressionFloat;
		std::string const *format = tagProps.Value("format");
		if (format && !FloatCompressionBitFormatFromString(format->c_str(), method.m_format)) {
			CompressionLabelMap::const_iterator itLabel = state.m_floatFormats.find(*format);
			if (itLabel != state.m_floatFormats.end()) {
				method.m_format = itLabel->second;
			} else {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'float' attrib 'format' unknown value '") + *format + std::string("'; using default compression..."));
			}
		}
		// read overrides
		if (!FloatCompressionBitFormatReadProps(tagProps, method.m_format, pLoader, "tag 'float' ")) {
			// continue after these errors...
		}

		if (state.m_flags & kStateInFloatChannelScope) {
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_floatModifiedSet.count(*it)) {
					pMetaData->m_floatCompression[ *it ] = pMetaData->m_defaultCompressionFloat;
					state.m_floatModifiedSet.emplace(*it);
				}
				pMetaData->m_floatCompression[ *it ] = method;
			}
		} else
			pMetaData->m_defaultCompressionFloat = method;
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
				// once we process the default trans tag, the default value is set for the rest of the compression block :
				state.m_flags |= kStateDefaultFloatCompressionDefined;
			}
			state.m_trackTag = kAnimMetaDataTagROOT;
		}
	}
	return 0;
}

static int KeyFramesHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagKeyFrames );
		if (tag != kAnimMetaDataTagCompression &&
			tag != kAnimMetaDataTagScale &&
			tag != kAnimMetaDataTagRotation &&
			tag != kAnimMetaDataTagTranslation &&
			tag != kAnimMetaDataTagFloat) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'keyframes' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		if (!(state.m_flags & kStateKeyframeCompressionDefined)) {
			pLoader->SetInvalidScope();
			pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, std::string("tag 'keyframes' not expected before compression keys attribute is defined; ignoring tag..."));
			return 0;
		}
		// handle attributes - generate, error, maxkeysperblock, maxblocksize
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		AnimMetaData *pMetaData = pLoader->m_metaData;
		std::vector<AnimMetaDataTrackKeyFrames*> apKeyFrames;
		unsigned keyCompression = (pMetaData->m_flags & kAnimMetaDataFlagKeysMask);
		if (keyCompression != kAnimMetaDataFlagKeysShared && keyCompression != kAnimMetaDataFlagKeysUnshared) {
			pLoader->SetInvalidScope();
			pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'keyframes' not expected with uniform keyframe compression; ignoring tag...");
			return 0;
		}
		if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
			// read defaults
			if (tag == kAnimMetaDataTagCompression) {
				if (state.m_flags & kStateDefaultKeyFramesDefined) {
					pLoader->SetInvalidScope();
					pLoader->ReportError(kAnimMetaDataUnexpectedTag, "tag 'keyframes' expected only once in ROOT scope; ignoring tag...");
					return 0;
				}
				if (keyCompression == kAnimMetaDataFlagKeysUnshared) {
//					U32 loadError = 0;
					std::string const *maxkeysperblock = tagProps.Value("maxkeysperblock");
					if (maxkeysperblock) {
						unsigned value;
						if (!UintFromString(maxkeysperblock->c_str(), value)) {
							pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'keyframes' attrib 'maxkeysperblock' badly formed uint value '") + *maxkeysperblock + std::string("'"));
						} else if (value >= 0xFFFF) {
							pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'keyframes' attrib 'maxkeysperblock' out of range value '") + *maxkeysperblock + std::string("'"));
						} else {
							pMetaData->m_maxKeysPerBlock = (U16)value;
						}
					}
					std::string const *maxblocksize = tagProps.Value("maxblocksize");
					if (maxblocksize) {
						unsigned value;
						if (!UintFromString(maxblocksize->c_str(), value)) {
							pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'keyframes' attrib 'maxblocksize' badly formed uint value '") + *maxblocksize + std::string("'"));
						} else {
							pMetaData->m_maxBlockSize = value;
						}
					}
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_scale.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_rotation.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_translation.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompressionFloat.m_keyFrames);
				} else {
					// Store keyframe generate flag to shared key frame:
					apKeyFrames.push_back(&pMetaData->m_sharedKeyFrames);
					// Store error tolerance values in individual channels for inheritance:
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_scale.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_rotation.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_translation.m_keyFrames);
					apKeyFrames.push_back(&pMetaData->m_defaultCompressionFloat.m_keyFrames);
				}
			} else if (tag == kAnimMetaDataTagScale) {
				apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_scale.m_keyFrames);
			} else if (tag == kAnimMetaDataTagRotation) {
				apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_rotation.m_keyFrames);
			} else if (tag == kAnimMetaDataTagTranslation) {
				apKeyFrames.push_back(&pMetaData->m_defaultCompression.m_translation.m_keyFrames);
			} else if (tag == kAnimMetaDataTagFloat) {
				apKeyFrames.push_back(&pMetaData->m_defaultCompressionFloat.m_keyFrames);
			}
		} else {
			if (keyCompression != kAnimMetaDataFlagKeysUnshared) {
				pLoader->SetInvalidScope();
				pLoader->ReportError(kAnimMetaDataUnexpectedTag, "tag 'keyframes' not expected in joint or floatchannel scope with shared keyframe compression; ignoring tag...");
				return 0;
			}
			if (state.m_flags & kStateInJointScope) {
				for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
					if (!state.m_jointModifiedSet.count(*it)) {
						pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
						state.m_jointModifiedSet.emplace(*it);
					}
					if (tag == kAnimMetaDataTagScale) {
						apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_scale.m_keyFrames);
					} else if (tag == kAnimMetaDataTagRotation) {
						apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_rotation.m_keyFrames);
					} else if (tag == kAnimMetaDataTagTranslation) {
						apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_translation.m_keyFrames);
					}
				}
			} else if (state.m_flags & kStateInFloatChannelScope) {
				if (!state.m_channels.empty()) {
					if (tag == kAnimMetaDataTagFloat) {
						for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
							if (!state.m_floatModifiedSet.count(*it)) {
								pMetaData->m_floatCompression[ *it ] = pMetaData->m_defaultCompressionFloat;
								state.m_floatModifiedSet.emplace(*it);
							}
							apKeyFrames.push_back(&pMetaData->m_floatCompression[*it].m_keyFrames);
						}
					}
				}
			}
		}
		if (apKeyFrames.empty())
			return 0;

		if ((keyCompression == kAnimMetaDataFlagKeysUnshared) || (tag == kAnimMetaDataTagCompression)) {
			std::string const *generate = tagProps.Value("generate");
			if (generate) {
				bool value;
				if (!BoolFromString(generate->c_str(), value)) {
					pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'keyframes' attrib 'generate' badly formed bool value '") + *generate + std::string("'"));
				} else {
					if (value) {
						for (std::vector<AnimMetaDataTrackKeyFrames*>::const_iterator it = apKeyFrames.begin(), itEnd = apKeyFrames.end(); it != itEnd; ++it)
							(*it)->m_flags |= kAnimMetaDataFlagGenerateSkipList;
					} else {
						for (std::vector<AnimMetaDataTrackKeyFrames*>::const_iterator it = apKeyFrames.begin(), itEnd = apKeyFrames.end(); it != itEnd; ++it)
							(*it)->m_flags &=~kAnimMetaDataFlagGenerateSkipList;
					}
				}
			}
		}
		std::string const *error = tagProps.Value("error");
		if (error) {
			float fError;
			if (!FloatFromString(error->c_str(), fError)) {
				pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'keyframes' attrib 'error' badly formed float value '") + *error + std::string("'"));
			} else {
				fError = fabsf(fError);
				for (std::vector<AnimMetaDataTrackKeyFrames*>::const_iterator it = apKeyFrames.begin(), itEnd = apKeyFrames.end(); it != itEnd; ++it)
					(*it)->m_fErrorTolerance = fError;
			}
		}
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			if (pLoader->GetScope() == kAnimMetaDataTagCompression) {
				// once we close the root keyframes tag, the default value is set for the rest of the compression block :
				state.m_flags |= kStateDefaultKeyFramesDefined;
			}
		}
	}
	return 0;
}

static int SkipListHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start==false) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagSkipList );
		if (tag != kAnimMetaDataTagKeyFrames) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'skiplist' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}

		// get base keyframes
		AnimMetaData *pMetaData = pLoader->m_metaData;
		std::vector<AnimMetaDataTrackKeyFrames*> apKeyFrames;
		unsigned keyCompression = (pMetaData->m_flags & kAnimMetaDataFlagKeysMask);
		if (state.m_flags & kStateInJointScope) {
			if (keyCompression != kAnimMetaDataFlagKeysUnshared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'skiplist' not expected in joint keyframes except for unshared keys; ignoring tag...");
				return 0;
			}
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_jointModifiedSet.count(*it)) {
					pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
					state.m_jointModifiedSet.emplace(*it);
				}
				if (tag == kAnimMetaDataTagScale) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_scale.m_keyFrames);
				} else if (tag == kAnimMetaDataTagRotation) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_rotation.m_keyFrames);
				} else if (tag == kAnimMetaDataTagTranslation) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_translation.m_keyFrames);
				}
			}
		} else if (state.m_flags & kStateInFloatChannelScope) {
			if (keyCompression != kAnimMetaDataFlagKeysUnshared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'skiplist' not expected in floatchannel keyframes except for unshared keys; ignoring tag...");
				return 0;
			}
			if (state.m_trackTag == kAnimMetaDataTagFloat) {
				for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
					if (!state.m_floatModifiedSet.count(*it)) {
						pMetaData->m_floatCompression[ *it ] = pMetaData->m_defaultCompressionFloat;
						state.m_floatModifiedSet.emplace(*it);
					}
					apKeyFrames.push_back(&pMetaData->m_floatCompression[*it].m_keyFrames);
				}
			}
		} else {
			if (keyCompression != kAnimMetaDataFlagKeysShared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'skiplist' not expected in default keyframes except for shared keys; ignoring tag...");
				return 0;
			}
			apKeyFrames.push_back( &pMetaData->m_sharedKeyFrames );
		}
		if (apKeyFrames.empty())
			return 0;

		// handle attributes - NONE
		// handle contents - int list of frames
		int numInts = pLoader->IntList();
		for (int i = 0; i < numInts; i++) {
			if (pLoader->m_integers[i] < 0 || pLoader->m_integers[i] > 65535) {
				continue;
			}
			for (std::vector<AnimMetaDataTrackKeyFrames*>::const_iterator it = apKeyFrames.begin(), itEnd = apKeyFrames.end(); it != itEnd; ++it)
				(*it)->m_skipFrames.emplace((size_t)pLoader->m_integers[i]);
		}
	//} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static int KeepListHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start==false) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagKeepList );
		if (tag != kAnimMetaDataTagKeyFrames) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'keeplist' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}

		// get base keyframes
		AnimMetaData *pMetaData = pLoader->m_metaData;
		std::vector<AnimMetaDataTrackKeyFrames*> apKeyFrames;
		unsigned keyCompression = (pMetaData->m_flags & kAnimMetaDataFlagKeysMask);
		if (state.m_flags & kStateInJointScope) {
			if (keyCompression != kAnimMetaDataFlagKeysUnshared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'keeplist' not expected in joint keyframes except for unshared keys; ignoring tag...");
				return 0;
			}
			for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
				if (!state.m_jointModifiedSet.count(*it)) {
					pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
					state.m_jointModifiedSet.emplace(*it);
				}
				if (tag == kAnimMetaDataTagScale) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_scale.m_keyFrames);
				} else if (tag == kAnimMetaDataTagRotation) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_rotation.m_keyFrames);
				} else if (tag == kAnimMetaDataTagTranslation) {
					apKeyFrames.push_back(&pMetaData->m_jointCompression[*it].m_translation.m_keyFrames);
				}
			}
		} else if (state.m_flags & kStateInFloatChannelScope) {
			if (keyCompression != kAnimMetaDataFlagKeysUnshared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'keeplist' not expected in floatchannel keyframes except for unshared keys; ignoring tag...");
				return 0;
			}
			if (state.m_trackTag == kAnimMetaDataTagFloat) {
				for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
					if (!state.m_floatModifiedSet.count(*it)) {
						pMetaData->m_floatCompression[ *it ] = pMetaData->m_defaultCompressionFloat;
						state.m_floatModifiedSet.emplace(*it);
					}
					apKeyFrames.push_back(&pMetaData->m_floatCompression[*it].m_keyFrames);
				}
			}
		} else {
			if (keyCompression != kAnimMetaDataFlagKeysShared) {
				pLoader->SetInvalidScope();
				pLoader->ReportWarning(kAnimMetaDataUnexpectedTag, "tag 'keeplist' not expected in default keyframes except for shared keys; ignoring tag...");
				return 0;
			}
			apKeyFrames.push_back( &pMetaData->m_sharedKeyFrames );
		}
		if (apKeyFrames.empty())
			return 0;

		// handle attributes - NONE
		// handle contents - int list of frames
		int numInts = pLoader->IntList();
		for (int i = 0; i < numInts; i++) {
			if (pLoader->m_integers[i] < 0 || pLoader->m_integers[i] > 65535) {
				continue;
			}
			for (std::vector<AnimMetaDataTrackKeyFrames*>::const_iterator it = apKeyFrames.begin(), itEnd = apKeyFrames.end(); it != itEnd; ++it)
				(*it)->m_keepFrames.emplace((size_t)pLoader->m_integers[i]);
		}
	//} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static int RootSpaceErrorHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagRootSpaceError );
		if (tag != kAnimMetaDataTagScale &&
			tag != kAnimMetaDataTagRotation &&
			tag != kAnimMetaDataTagTranslation &&
			tag != kAnimMetaDataTagFloat) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'rootspaceerror' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		AnimMetaData *pMetaData = pLoader->m_metaData;

		// handle attributes - generate, error, maxkeysperblock, maxblocksize
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::vector<AnimMetaDataTrackRootSpaceError*> apRootSpaceErrors;
		if (!(state.m_flags & (kStateInJointScope|kStateInFloatChannelScope))) {
			// read defaults
			if (state.m_trackTag == kAnimMetaDataTagScale) {
				apRootSpaceErrors.push_back(&pMetaData->m_defaultCompression.m_scale.m_rootSpaceError);
			} else if (state.m_trackTag == kAnimMetaDataTagRotation) {
				apRootSpaceErrors.push_back(&pMetaData->m_defaultCompression.m_rotation.m_rootSpaceError);
			} else if (state.m_trackTag == kAnimMetaDataTagTranslation) {
				apRootSpaceErrors.push_back(&pMetaData->m_defaultCompression.m_translation.m_rootSpaceError);
			} else if (state.m_trackTag == kAnimMetaDataTagFloat) {
				apRootSpaceErrors.push_back(&pMetaData->m_defaultCompressionFloat.m_rootSpaceError);
			}
		} else {
			if (state.m_flags & kStateInJointScope) {
				for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
					if (!state.m_jointModifiedSet.count(*it)) {
						pMetaData->m_jointCompression[ *it ] = pMetaData->m_defaultCompression;
						state.m_jointModifiedSet.emplace(*it);
					}
					if (state.m_trackTag == kAnimMetaDataTagScale) {
						apRootSpaceErrors.push_back(&pMetaData->m_jointCompression[*it].m_scale.m_rootSpaceError);
					} else if (state.m_trackTag == kAnimMetaDataTagRotation) {
						apRootSpaceErrors.push_back(&pMetaData->m_jointCompression[*it].m_rotation.m_rootSpaceError);
					} else if (state.m_trackTag == kAnimMetaDataTagTranslation) {
						apRootSpaceErrors.push_back(&pMetaData->m_jointCompression[*it].m_translation.m_rootSpaceError);
					}
				}
			} else if (state.m_flags & kStateInFloatChannelScope) {
				if (state.m_trackTag == kAnimMetaDataTagFloat) {
					for (ChannelList::const_iterator it = state.m_channels.begin(), itEnd = state.m_channels.end(); it != itEnd; ++it) {
						if (!state.m_floatModifiedSet.count(*it)) {
							pMetaData->m_floatCompression[ *it ] = pMetaData->m_defaultCompressionFloat;
							state.m_floatModifiedSet.emplace(*it);
						}
						apRootSpaceErrors.push_back(&pMetaData->m_floatCompression[*it].m_rootSpaceError);
					}
				}
			}
		}
		if (apRootSpaceErrors.empty())
			return 0;

		std::string const *error = tagProps.Value("error");
		if (error) {
			float fError;
			if (!FloatFromString(error->c_str(), fError)) {
				pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'rootspaceerror' attrib 'error' badly formed float value '") + *error + std::string("'"));
			} else {
				fError = fabsf(fError);
				for (std::vector<AnimMetaDataTrackRootSpaceError*>::const_iterator it = apRootSpaceErrors.begin(), itEnd = apRootSpaceErrors.end(); it != itEnd; ++it)
					(*it)->m_fErrorTolerance = fError;
			}
		}
		std::string const *errordelta = tagProps.Value("errordelta");
		if (errordelta) {
//			if (!(pMetaData->m_flags & kAnimMetaDataFlagDeltaOptimization)) {
//				pLoader->ReportWarning(kAnimMetaDataUnexpectedAttrib, std::string("tag 'rootspaceerror' attrib 'errordelta' not expected without compression deltaoptimizations=1"));
//			}
			float fError;
			if (!FloatFromString(errordelta->c_str(), fError)) {
				pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'rootspaceerror' attrib 'errordelta' badly formed float value '") + *errordelta + std::string("'"));
			} else {
				fError = fabsf(fError);
				for (std::vector<AnimMetaDataTrackRootSpaceError*>::const_iterator it = apRootSpaceErrors.begin(), itEnd = apRootSpaceErrors.end(); it != itEnd; ++it)
					(*it)->m_fErrorDeltaFactor = fError;
			}
		}
		std::string const *errorspeed = tagProps.Value("errorspeed");
		if (errorspeed) {
			if (state.m_trackTag == kAnimMetaDataTagTranslation) {
				pLoader->ReportWarning(kAnimMetaDataUnexpectedAttrib, std::string("tag 'rootspaceerror' attrib 'errorspeed' not used for joint translation tracks; ignoring attrib..."));
			} else if (state.m_trackTag == kAnimMetaDataTagFloat) {
				pLoader->ReportWarning(kAnimMetaDataUnexpectedAttrib, std::string("tag 'rootspaceerror' attrib 'errorspeed' not used for float channel tracks; ignoring attrib..."));
			} else {
//				if (!(pMetaData->m_flags & kAnimMetaDataFlagDeltaOptimization)) {
//					pLoader->ReportWarning(kAnimMetaDataUnexpectedAttrib, std::string("tag 'rootspaceerror' attrib 'errorspeed' not expected without compression deltaoptimizations=1"));
//				}
				float fError;
				if (!FloatFromString(errorspeed->c_str(), fError)) {
					pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'rootspaceerror' attrib 'errorspeed' badly formed float value '") + *errorspeed + std::string("'"));
				} else {
					fError = fabsf(fError);
					for (std::vector<AnimMetaDataTrackRootSpaceError*>::const_iterator it = apRootSpaceErrors.begin(), itEnd = apRootSpaceErrors.end(); it != itEnd; ++it)
						(*it)->m_fErrorSpeedFactor = fError;
				}
			}
		}
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		pLoader->ExitScope();
	}
	return 0;
}

static bool IsValidLabel( std::string const& name )
{
	if (name.empty())
		return false;
	for (const char *p = name.c_str(); *p; ++p)
		if (*p != '_' && *p != '-' && !isalnum(*p))
			return false;		// invalid character
	return true;
}
static bool IsValidJointName( std::string const& name )
{
	if (name.empty())
		return false;
	const char *pLastBar = NULL;
	for (const char *p = name.c_str(); *p; ++p) {
		if (*p == '|') {
			if ((pLastBar && p - pLastBar == 1) || (*(p+1) == '\0'))
				return false;	// empty node name
			pLastBar = p;
		} else if (isspace(*p) || !isprint(*p) || *p == '.')
			return false;		// invalid character - allow just about anything for now, since I don't know what Maya allows
	}
	return true;
}
static bool IsValidFloatChannelName( std::string const& name )
{
	if (name.empty())
		return false;
	const char *pLastBar = NULL;
	const char *pLastDot = NULL;
	for (const char *p = name.c_str(); *p; ++p) {
		if (*p == '.') {
			if (pLastDot)
				return false;	// dot in attribute name
			if ((pLastBar && p - pLastBar == 1) || p == name.c_str())
				return false;	// empty node name
			if (*(p+1) == '\0')
				return false;	// empty attribute name
			pLastDot = p;
		} else if (*p == '|') {
			if (pLastDot)
				return false;	// dot in node name
			if ((pLastBar && p - pLastBar == 1) || (*(p+1) == '\0'))
				return false;	// empty node name
			pLastBar = p;
		} else if (isspace(*p) || !isprint(*p))
			return false;		// invalid character - allow just about anything for now, since I don't know what Maya allows
	}
	return true;
}

std::vector<std::string>::const_iterator FindMatchingName(std::string const& name, std::vector<std::string> const& nameList)
{
	std::string::size_type namelen = name.length();
	std::vector<std::string>::const_iterator it = nameList.begin(), itEnd = nameList.end();
	for (; it != itEnd; ++it) {
		if (*it == name)
			return it;
		if ((*it)[0] != '|') {	// for listname, check if name ends in "|listname"
			std::string::size_type len = it->length();
			if (len+1 <= namelen && name[ namelen - len - 1 ] == '|' && !name.compare(namelen - len, len, *it))
				return it;
		}
		if (name[0] != '|') {	// check if listname ends in "|name"
			std::string::size_type len = it->length();
			if (namelen+1 <= len && (*it)[ len - namelen - 1 ] == '|' && !it->compare(len - namelen, namelen, name))
				return it;
		}
	}
	return itEnd;
}

bool NameMatchesAnimMetaDataName(std::string const& name, std::string const& partialName)
{
	if (name == partialName)
		return true;
	if (partialName.empty() || partialName[0] == '|')
		return false;
	std::string::size_type namelen = name.length();
	std::string::size_type len = partialName.length();
	if (namelen > len+1 && name[namelen - len - 1] == '|' && !name.compare(namelen-len, len, partialName))
		return true;
	return false;
}

int FindAnimMetaDataIndexForName(std::string const& name, std::vector<std::string> const& partialNameList)
{
	std::vector<std::string>::const_iterator it = partialNameList.begin(), itEnd = partialNameList.end();
	for (int i = 0; it != itEnd; ++it, ++i) {
		if (NameMatchesAnimMetaDataName(name, *it)) {
			return i;
		}
	}
	return -1;
}

static int JointHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagJoint );
		if (tag != kAnimMetaDataTagCompression) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'joint' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - name, label
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();

		std::string const *name = tagProps.Value("name");
		if (!name) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'joint' attrib 'name' is required; ignoring tag..."));
			return 0;
		}

		std::set<size_t> channelSet;
		// tokenize name by ',':
		std::string::size_type posStart = name->find_first_not_of(" \t"), posEnd;
		while (posStart != std::string::npos) {
			posEnd = name->find_first_of(',', posStart);
			std::string::size_type posEndSubstr = name->find_last_not_of(" \t", (posEnd!=std::string::npos)?posEnd-1:std::string::npos);
			if (posEndSubstr != std::string::npos && posEndSubstr > posStart) {
				std::string nameEntry = name->substr(posStart, posEndSubstr-posStart+1);

				ChannelSetLabelMap::const_iterator itFind;
				// check to see if this is a valid label
				if (IsValidLabel(nameEntry)
				&&	((itFind = state.m_jointSets.find( nameEntry )) != state.m_jointSets.end())) {
					channelSet.insert(itFind->second.begin(), itFind->second.end());
				} else if (IsValidJointName(nameEntry)) {
					// check to see if this is a valid float channel
					std::vector<std::string>::const_iterator itFind = FindMatchingName( nameEntry, pMetaData->m_jointNames );
					if (itFind == pMetaData->m_jointNames.end()) {
						channelSet.emplace( pMetaData->m_jointNames.size() );
						pMetaData->m_jointNames.push_back( nameEntry );
						pMetaData->m_jointCompression.push_back( pMetaData->m_defaultCompression );
					} else {
						if (*itFind != nameEntry) {
							pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'joint' attrib 'name' entry '")+ *itFind +"' matches existing '"+ nameEntry +"', but not exactly");
						}
						channelSet.emplace( itFind - pMetaData->m_jointNames.begin() );
					}
				} else {
					pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'joint' attrib 'name' entry '")+ nameEntry +"' is not a valid joint name or label value");
				}
			}
			if (posEnd == std::string::npos)
				break;
			posStart = name->find_first_not_of(" \t", posEnd+1);
		}
		if (channelSet.empty()) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'joint' attrib 'name' value '") + *name +"' contains no valid joint names or labels; ignoring tag...");
			return 0;
		}

		state.m_flags |= kStateInJointScope;
		state.m_channels.clear();
		for (std::set<size_t>::const_iterator channelSetIt = channelSet.begin(); channelSetIt != channelSet.end(); channelSetIt++)
			state.m_channels.push_back((U16)*channelSetIt);

		std::string const *label = tagProps.Value("label");
		if (label) {
			if (!IsValidLabel(*label)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'joint' attrib 'label' value '")+ *label +"' is not a valid label value; ignoring attrib...");
			} else {
				ChannelSetLabelMap::const_iterator itFind = state.m_jointSets.find( *label );
				if (itFind != state.m_jointSets.end()) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'joint' attrib 'label' value '")+ *label +"' conflicts with an existing label; discarding old label...");
				}
				state.m_jointSets[*label] = channelSet;
			}
		}
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			state.m_flags &=~ kStateInJointScope;
			state.m_channels.clear();
		}
	}
	return 0;
}

static int FloatChannelHandler(const char* /*tagname*/, const char** /*atts*/, bool start, void* userdata)
{
	LoadAnimMetaData *pLoader = (LoadAnimMetaData *)userdata;
	AnimMetaDataState& state = pLoader->m_state;
	if (pLoader->IsInsideInvalidScope())
		return 0;

	if (start) {
		AnimMetaDataTag tag = pLoader->EnterScope( kAnimMetaDataTagFloatChannel );
		if (tag != kAnimMetaDataTagCompression) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataUnexpectedTag, std::string("tag 'floatchannel' not expected within ") + GetTagScopeName(tag) + std::string(" scope; ignoring tag..."));
			return 0;
		}
		// handle attributes - name, label
		AnimMetaData *pMetaData = pLoader->m_metaData;
		const ITDATAMGR::PropList& tagProps = pLoader->GetPropList();
		std::string const *name = tagProps.Value("name");
		if (!name) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'floatchannel' attrib 'name' is required; ignoring tag..."));
			return 0;
		}

		std::set<size_t> channelSet;
		// tokenize name by ',':
		std::string::size_type posStart = name->find_first_not_of(" \t"), posEnd;
		while (posStart != std::string::npos) {
			posEnd = name->find_first_of(',', posStart);
			std::string::size_type posEndSubstr = name->find_last_not_of(" \t", (posEnd!=std::string::npos)?posEnd-1:std::string::npos);
			if (posEndSubstr != std::string::npos && posEndSubstr > posStart) {
				std::string nameEntry = name->substr(posStart, posEndSubstr-posStart+1);

				ChannelSetLabelMap::const_iterator itFind;
				// check to see if this is a valid label
				if (IsValidLabel(nameEntry)
				&&	((itFind = state.m_floatChannelSets.find( nameEntry )) != state.m_floatChannelSets.end())) {
					channelSet.insert(itFind->second.begin(), itFind->second.end());
				} else if (IsValidFloatChannelName(nameEntry)) {
					// check to see if this is a valid float channel
					std::vector<std::string>::const_iterator itFind = FindMatchingName( nameEntry, pMetaData->m_floatNames );
					if (itFind == pMetaData->m_floatNames.end()) {
						channelSet.emplace( pMetaData->m_floatNames.size() );
						pMetaData->m_floatNames.push_back( nameEntry );
						pMetaData->m_floatCompression.push_back( pMetaData->m_defaultCompressionFloat );
					} else {
						if (*itFind != nameEntry) {
							pLoader->ReportWarning(kAnimMetaDataAttribFormat, std::string("tag 'floatchannel' attrib 'name' entry '")+ *itFind +"' matches existing '"+ nameEntry +"', but not exactly");
						}
						channelSet.emplace( itFind - pMetaData->m_floatNames.begin() );
					}
				} else {
					pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'floatchannel' attrib 'name' entry '")+ nameEntry +"' is not a valid floatchannel name or label value");
				}
			}
			if (posEnd == std::string::npos)
				break;
			posStart = name->find_first_not_of(" \t", posEnd+1);
		}
		if (channelSet.empty()) {
			pLoader->SetInvalidScope();
			pLoader->ReportError(kAnimMetaDataMissingAttrib, std::string("tag 'floatchannel' attrib 'name' value '") + *name +"' contains no valid floatchannel names or labels; ignoring tag...");
			return 0;
		}

		state.m_flags |= kStateInFloatChannelScope;
		state.m_channels.clear();
		for (std::set<size_t>::const_iterator channelSetIt = channelSet.begin(); channelSetIt != channelSet.end(); channelSetIt++)
			state.m_channels.push_back((U16)*channelSetIt);

		std::string const *label = tagProps.Value("label");
		if (label) {
			if (!IsValidLabel(*label)) {
				pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'floatchannel' attrib 'label' value '")+ *label +"' is not a valid label value; ignoring attrib...");
			} else {
				ChannelSetLabelMap::const_iterator itFind = state.m_floatChannelSets.find( *label );
				if (itFind != state.m_floatChannelSets.end()) {
					pLoader->ReportError(kAnimMetaDataAttribFormat, std::string("tag 'floatchannel' attrib 'label' value '")+ *label +"' conflicts with an existing label; discarding old label...");
				}
				state.m_floatChannelSets[*label] = channelSet;
			}
		}
	} else {
		//NOTE: ParseXML handles closing tag mismatch errors, so we don't have to...
		if (pLoader->ExitScope()) {
			state.m_flags &=~ kStateInFloatChannelScope;
			state.m_channels.clear();
		}
	}
	return 0;
}

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim
