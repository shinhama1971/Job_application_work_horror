// ============================================================================
// 共通の処理: 一人称の手と懐中電灯（FirstPersonHands）の描画で、頂点シェーダーとピクセルシェーダーが共有する値と、照らし方。
// 主な技術: 手元だけの射影行列（画角を狭め、手前の面まで描く）、ワールド空間の点光源と半球の環境光、
//           懐中電灯の光が目の前の壁で跳ね返って手を照らす光（間接光の近似）
// ・手と懐中電灯は、カメラに付いて動く。ワールド行列は「手元の空間 → ワールド」なので、
//   天井の照明・環境光・部屋の角の暗がりは、壁と同じワールドの位置で計算できる。
// ・懐中電灯の光そのもの（目の位置から前へ出る円錐）は、懐中電灯の本体と手には当てない（光は手の前から出ているため）。
//   代わりに、光が当たった前の面から返ってくる光（ViewModelBounce）で、手の前側と懐中電灯の頭を照らしている。
// common.hlsl の後に読み込んでいる。
// ============================================================================
#ifndef VIEW_MODEL_INCLUDED
#define VIEW_MODEL_INCLUDED

// FirstPersonHands.cpp の VIEW_MODEL_BUFFER と同じ並び（b9。頂点シェーダーとピクセルシェーダーの両方で使う）
cbuffer ViewModelBuffer : register(b9)
{
    // 手元を描く射影行列（本描画より画角を狭め、手前の面を0.1まで描く）
    matrix ViewModelProjection;
    // xyz = カメラの位置（ワールド）、w = 経過時間（秒）
    float4 ViewModelEye;
    // xyz = 前の面で跳ね返った懐中電灯の光の色と強さ、w = 未使用
    float4 ViewModelBounce;
    // xyz = 跳ね返った光が来る向き（ワールド。カメラの前から手の方へ）、w = 未使用
    float4 ViewModelBounceDirection;
    // x = レンズの光（0〜1。点灯の度合いと電池のちらつき）、y = 電池の弱り（0〜1）、z = 手の汚れ（0〜1）、w = 未使用
    float4 ViewModelState;
}

// 頂点シェーダーからピクセルシェーダーへ渡す値
struct VIEW_MODEL_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;            // r = 環境遮蔽、g・b = 部位の印（build_flashlight_hand.py）
    float2 tex : TEXCOORD0;
    float3 localPos : TEXCOORD1;    // 手元の空間の位置（模様が手と一緒に動くように使う）
    float3 localNormal : TEXCOORD2;
    float3 worldPos : TEXCOORD3;
    float3 worldNormal : TEXCOORD4;
};

// ここから下はピクセルシェーダーだけで使う（pbrLighting.hlsli などの後に読み込んだときだけ）
#if defined(PBR_LIGHTING_INCLUDED)

// 点光源（天井の照明・非常灯・裸電球）の拡散反射と鏡面反射を足している。
// wrap は面の裏側へ回り込む光の量（肌や布は0.3〜0.5、金属は0）。
// 手は画面の端の小さな範囲なので、タイルのリストではなく全部の光源を順に調べている
// （タイルのリストは壁の深度で作っているので、手前の手を照らす光源が外れていることがある）。
void AccumulateViewModelPointLights(
    float3 worldPosition, float3 worldNormal, float3 toView, float wrap, PbrSurface surface,
    inout float3 diffuse, inout float3 specular)
{
    [loop]
    for (uint i = 0; i < PointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = g_PointLights[i];
        const float3 offset = pointLight.PositionRange.xyz - worldPosition;
        const float range = max(pointLight.PositionRange.w, 0.001f);
        const float distanceSquared = dot(offset, offset);
        [branch]
        if (distanceSquared >= range * range)
        {
            continue;
        }
        const float lightDistance = sqrt(distanceSquared);
        const float3 toLight = offset / max(lightDistance, 0.001f);
        float attenuation = saturate(1.0f - lightDistance / range);
        attenuation *= attenuation;
        // 天井の照明は下向きに広がる面の光源なので、横に近い向き（目の高さの手）には弱くしか届かない（litTexturePS と同じ近似）
        const float fixtureDistribution = lerp(0.22f, 1.0f, smoothstep(0.04f, 0.72f, toLight.y));
        const float3 radiance = pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * attenuation *
            fixtureDistribution * GetPointLightShadow(worldPosition, worldNormal, pointLight.PositionRange.xyz);
        const float lambert = saturate((dot(worldNormal, toLight) + wrap) / (1.0f + wrap));
        diffuse += radiance * lambert;
        specular += radiance * EvaluateSpecularGGX(surface, worldNormal, toView, toLight);
    }
}

// 懐中電灯の光が目の前の面で跳ね返って、手と懐中電灯を前から照らす光
float3 GetViewModelBounce(float3 worldNormal, float wrap)
{
    const float facing = saturate((dot(worldNormal, ViewModelBounceDirection.xyz) + wrap) / (1.0f + wrap));
    return ViewModelBounce.rgb * facing;
}

// 環境光（上下で色を変えた環境光と、部屋の角の暗がり）
float3 GetViewModelAmbient(float3 worldPosition, float3 worldNormal, float occlusion)
{
    return GetHemisphereAmbient(worldNormal) * occlusion * GetRoomOcclusion(worldPosition, worldNormal);
}

// 環境の映り込み（金属と濡れた面）。環境マップのない場面でも、環境光だけは映している
float3 GetViewModelReflection(PbrSurface surface, float3 worldNormal, float3 toView, float occlusion)
{
    const float nDotV = saturate(dot(worldNormal, toView));
    const float3 reflected = reflect(-toView, worldNormal);
    const float3 environment = max(GetHemisphereAmbient(reflected), SampleEnvironment(reflected, surface.roughness));
    return environment * EnvironmentBrdfApprox(surface.f0, surface.roughness, nDotV) *
        GetSpecularOcclusion(nDotV, occlusion, surface.roughness);
}

#endif

#endif
