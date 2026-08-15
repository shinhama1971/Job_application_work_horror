cbuffer WorldBuffer : register(b0)
{
	matrix World;
}
cbuffer ViewBuffer : register(b1)
{
	matrix View;
}
cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

struct VS_IN
{
    float4 pos : POSITION0;
	float4 nrm : NORMAL0;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    
};

struct PS_IN
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
	float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
};

struct LIGHT
{
    bool Enable;
    bool FlashlightEnabled;
    float Intensity;
    float Range;
    float4 Direction;
    float4 Diffuse;
    float4 Ambient;
    float4 SpotParams;
};

cbuffer DebugViewBuffer : register(b7)
{
    int DebugViewMode;
    float WallDampStrength;
    float2 DebugViewPadding;
};

cbuffer LightBuffer : register(b3)
{
    LIGHT Light;
}

struct MATERIAL
{
    float4 Ambuent;
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float Shiness;
    bool TextureEnable;
    bool2 Dummy;
};
struct ENVIRONMENT_POINT_LIGHT
{
    float4 PositionRange;
    float4 ColorIntensity;
};

cbuffer EnvironmentLightBuffer : register(b6)
{
    ENVIRONMENT_POINT_LIGHT EnvironmentLights[8];
    int EnvironmentLightCount;
    float3 EnvironmentLightPadding;
};

cbuffer MaterialBuffer : register(b4)
{
  MATERIAL Material;
}

//UV座標移動行列
cbuffer TextureBuffer : register(b5)
{
    matrix matrixTex;
}
