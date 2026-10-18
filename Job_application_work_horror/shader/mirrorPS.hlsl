// ============================================================================
// シェーダーの役割: 2面の壁の鏡を描いている。鏡の面で反転したカメラが描いた景色（t13）を、面の上の点ごとに投影して読んでいる。
// 古い鏡らしく、縁から銀が剥げた黒いしみ、全体のくもり、古いガラスのわずかなうねりを足し、
// 懐中電灯を正面から当てたときの照り返しを返している。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"
#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"

SamplerState g_SamplerState : register(s0);
// 鏡に映す景色（Game::Draw が m_WallMirror で描いたもの）
Texture2D g_WallMirror : register(t13);

// 鏡に映す景色を描いたカメラのビュー×射影の行列（PlanarReflection.cppと同じ並び）。
// b0〜b13 はすべて使われているため、床の反射が頂点シェーダー側でだけ使っているb9の、ピクセルシェーダー側に置いている。
cbuffer MirrorBuffer : register(b9)
{
    matrix MirrorViewProjection;
}

// 頂点シェーダー（litTextureVS）から受け取る値
struct MIRROR_PS_IN
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

float4 main(MIRROR_PS_IN input) : SV_Target
{
    // 面の上の点を、反転したカメラの画像へ投影して、映る景色を読む位置を求めている
    const float2 face = saturate(input.tex);
    const float4 mirrorClip = mul(float4(input.worldPos, 1.0f), MirrorViewProjection);
    const float2 mirrorNdc = mirrorClip.xy / max(mirrorClip.w, 0.0001f);
    float2 mirrorUV = float2(mirrorNdc.x * 0.5f + 0.5f, -mirrorNdc.y * 0.5f + 0.5f);
    // 古いガラスは平らではないため、映りをほんの少しうねらせている
    const float2 wobble = float2(
        FastValueNoise(face * float2(3.0f, 4.0f) + 5.1f),
        FastValueNoise(face * float2(3.0f, 4.0f) - 2.7f)) - 0.5f;
    mirrorUV = saturate(mirrorUV + wobble * 0.004f);
    float3 color = g_WallMirror.Sample(g_SamplerState, mirrorUV).rgb;

    // くすんだ銀：少し緑がかった古いガラスの色で、映りを2割ほど暗くしている
    color *= float3(0.80f, 0.84f, 0.80f);

    // 縁から銀が剥げた黒いしみ（縁に近いほど多く、ノイズで不規則に広がる）と、面の途中の小さな剥げ
    const float2 edge = min(face, 1.0f - face);
    const float edgeDistance = min(edge.x, edge.y);
    const float blotch =
        FastValueNoise(face * float2(7.0f, 11.0f) + 13.0f) * 0.6f +
        FastValueNoise(face * 23.0f - 4.0f) * 0.4f;
    const float edgeLoss = smoothstep(0.15f, 0.0f, edgeDistance - blotch * 0.09f);
    const float spotLoss = smoothstep(0.80f, 0.90f, FastValueNoise(face * float2(5.0f, 7.0f) + 31.0f)) * 0.65f;
    const float desilver = saturate(edgeLoss + spotLoss);
    color = lerp(color, float3(0.030f, 0.026f, 0.022f), desilver * 0.88f);

    // 埃のくもり：映りを少し白っぽく、薄くしている
    const float haze = FastValueNoise(face * float2(2.0f, 3.0f) + 7.0f);
    color = lerp(color, color * 0.72f + float3(0.010f, 0.011f, 0.010f), haze * 0.35f);

    // 懐中電灯を正面から当てると、跳ね返った光が目に戻ってきて、鏡の一点がまぶしく光る（懐中電灯は目の位置にある）
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float3 toPixel = normalize(input.viewPos);
        const float3 bounced = reflect(toPixel, normalize(input.viewNormal));
        const float towardEye = saturate(dot(bounced, -toPixel));
        const float beam = GetFlashlightBeamProfile(toPixel);
        const float glare = (pow(towardEye, 2500.0f) * 0.55f + pow(towardEye, 90.0f) * 0.05f) * beam;
        // くもった所は、光を広くにじませている
        color += Light.Diffuse.rgb * Light.Intensity * glare * (1.0f - desilver) * (1.0f + haze * 0.5f);
    }
    return float4(color, 1.0f);
}
