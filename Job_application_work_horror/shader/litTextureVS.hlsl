// ============================================================================
// シェーダーの役割: 3Dの頂点をワールド・ビュー・射影の行列で変換し、照明の計算に使う情報をピクセルシェーダーへ渡している。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// 影の計算に使う、ライトのビュー×射影の行列（ShadowMap.cppと同じ並び）
cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// 水面の反射のUVを求めるための、反射カメラのビュー×射影の行列（PlanarReflection.cppと同じ並び）
cbuffer ReflectionBuffer : register(b9)
{
    matrix ReflectionViewProjection;
}

// ピクセルシェーダーへ渡す値（位置・色・UV・深度・ビュー空間とワールド空間の位置と法線・影と反射の座標）
struct LIT_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
    float4 reflectionPos : TEXCOORD7;
};

// 頂点を画面の座標へ変換している
LIT_PS_IN main(in VS_IN input)
{
    LIT_PS_IN output;
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);

    // ビュー空間の位置（カメラからの距離として、懐中電灯や霧の計算に使っている）
    matrix wv = mul(World, View);
    float4 viewPos = mul(input.pos, wv);
    output.depth = viewPos.z;
    output.viewPos = viewPos.xyz;

    // ワールド空間の位置と法線、ライトから見た座標（影）、反射カメラから見た座標（水面の反射）
    float4 worldPosition = mul(input.pos, World);
    output.worldPos = worldPosition.xyz;
    output.worldNormal = normalize(mul(input.nrm.xyz, (float3x3)World));
    output.shadowPos = mul(worldPosition, ShadowViewProjection);
    output.reflectionPos = mul(worldPosition, ReflectionViewProjection);

    // ビュー空間の法線
    float3x3 normalMatrix = (float3x3)wv;
    output.viewNormal = normalize(mul(input.nrm.xyz, normalMatrix));

    // 頂点の色に、マテリアルの透明度を掛けている
    output.col = input.col;
    output.col.a *= Material.Diffuse.a;
    output.tex = input.tex;

    return output;
}

