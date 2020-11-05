//------------------------------------------------------------------------------------------------------------

struct PixelSrtData
{
	TextureCube		m_txCubeMap;
	SamplerState	m_sSamplerLinear; //	: register(s1);
	float4			m_ambientColor;
};

struct VertexStream
{
	float3 position		: ATTR0;
};

struct VertexOutput
{
    float4 hPosition	: SV_POSITION;
	float3 normal		: TEXCOORD0;
};

struct PixelOutput
{
	float4 col : SV_Target;
};

struct VertexSrtData
{
	DataBuffer<float4> m_posBuf;
	matrix    m_worldViewProjMat;
};

VertexOutput CubemapVS(VertexSrtData *pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutput OUT;

	float4 position = pSrt->m_posBuf[vertexId];

	OUT.hPosition = mul(position, pSrt->m_worldViewProjMat);
	OUT.normal    = normalize(position.xyz);

	return OUT;
}

PixelOutput CubePreviewPS(VertexOutput IN, PixelSrtData *pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 
	float4 environmentSample = pSrt->m_txCubeMap.Sample(pSrt->m_sSamplerLinear, IN.normal.xyz);
// 	float3 lightColor = environmentSample.rgb * pow(environmentSample.a, 2.2 * 2.2) * 4.0;
	float3 lightColor = environmentSample.rgb;
	OUT.col.rgb = lightColor;
	OUT.col.a = 1.0f;
	return OUT;
}
