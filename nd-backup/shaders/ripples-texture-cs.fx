//
// Compute Shader - water ripples
//

//#define kMaxVertexBlocks	1024
#define kMaxThreadX 32
#define kMaxThreadY 32

struct RipplesConsts
{
	int   m_active; 
	int   m_rippleType;
	float m_rippleScale;
	uint  m_setGridToZero;

	int   m_dux;        // movement
	int   m_duz;
	uint  m_num;        // num of grid elements per side
	uint  m_size;       // total size of elements in grid

	float m_kA;  // constant A
	float m_kC;  // constant C
	float m_bumpStrength;
	float m_heightStrength; 

	float m_clampHeight;
	float m_num2;
	float m_scale2;
	float m_scalePerUnit;
	
	uint  m_numImpulses;
	uint  m_numLineImpulses;
	float m_lineWidth;
	float m_paused; // not used

	float m_fpx; // fix_position.x
	float m_fpz; // fix_position.x
	float m_deltaTime;
	float m_deltaFactor;

	float4  m_impulseIndex[40];
	float4  m_lineImpulse[40];
	float4  m_impulseForce[40]; // shared between (.x) single and (.y) line impulses 
};

struct RipplesTextureData
{
	uint  m_num;        // num of grid elements per side
	float m_bumpStrength;
	float m_heightStrength; 
	float m_clampHeight;
	RegularBuffer<float> inputGridT0Data;
	RW_Texture2D<float4> outputTextureStream;
};

struct RipplesTextureSrt
{
	//RipplesConsts *pConsts;
	RipplesTextureData *pData;
};

[numthreads(kMaxThreadX, kMaxThreadY, 1)] 
void Cs_RipplesTexture(uint3 dispatchThreadId : SV_DispatchThreadID,
                       RipplesTextureSrt srt : S_SRT_DATA
                       )
{
	uint num                    = srt.pData->m_num;
	float bumpStrength          = srt.pData->m_bumpStrength;
	float heightStrength        = srt.pData->m_heightStrength;
	float clampHeight           = srt.pData->m_clampHeight;
	RegularBuffer<float> input  = srt.pData->inputGridT0Data;
	RW_Texture2D<float4> output = srt.pData->outputTextureStream;

	int r1 = dispatchThreadId.x; // current row
	int c1 = dispatchThreadId.y; // current col

	int r0 = (r1 + num - 1) % num;

	{
		int c2 = (c1 + 1) % num;

		int index  = (r1 * num) + c1;  // on			
		int index0 = (r0 * num) + c1;  // up
		int index2 = (r1 * num) + c2;  // right
		
		float gt1 = input[index]; // LoadAsFloat(input, index);  // center
		
		// Take differences of the heights and use them to create the normal map

		float gt1p0 = input[index0]; // LoadAsFloat(input, index0); // up
		float gt1p2 = input[index2]; // LoadAsFloat(input, index2); // right

		// convert the height field to a normal
		float diffX = gt1p2 - gt1;
		float diffZ = gt1p0 - gt1;

		// When using a RGBA8 texture need to convert into [-1..1] -> [0..255] range
		// normalize the values and center them to 127
		// diffX *= 127. + 127;				
		// diffZ *= 127 + 127;
		// the conversion to unsigned int should clamp out all negative values
		 // uint dx = max((uint) (diffX), 255);
		 // uint dz = max((uint) (diffZ), 255);
		// uint dx = (diffX) * 255.f + 0.5f;
		// uint dz = (diffZ) * 255.f + 0.5f;
		// uint color = (dx << 24) | (dz << 16) | 0xFF;
		float dd = diffX;

		diffX = (bumpStrength * diffX * .5f) + 0.5f;
		diffZ = (bumpStrength * diffZ * .5f) + 0.5f;

		diffX = (diffX  > 1.0f) ? 1.0f : ( diffX < 0) ? 0 : diffX;
		diffZ = (diffZ  > 1.0f) ? 1.0f : ( diffZ < 0) ? 0 : diffZ;

		float height = clamp(heightStrength * gt1, -clampHeight, clampHeight);

		uint2 txRowCol = dispatchThreadId.yx; 


		//heightStrength * dd
		output[txRowCol] = float4(diffX, diffZ, height, 1);

		//float4 red = float4(height,0,height,1);
		//float4 blue= float4(0,-height,height,1);
		//outputTextureStream[txRowCol] = (height > 0) ? red : blue;
	}
}
