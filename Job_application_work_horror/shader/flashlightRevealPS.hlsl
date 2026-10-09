// ============================================================================
// シェーダーの役割: 懐中電灯の光が当たっている部分だけ、壁の文字を浮かび上がらせている。
// 光の円錐・距離による減衰・影は壁（litTexturePS）と同じ計算を使うため、文字は光の輪と
// ぴったり重なって現れる。円錐の外や物陰では完全に消える。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
//
// マテリアル（b4）の使い方
//   Diffuse.rgb : 文字の色
// 文字の濃さ（0〜1。書き換わった直後に0から戻して、じわりと現している）
// 経過時間（秒）。現れる境目をゆっくり揺らしている
// 画像（t0）は、透明度だけを文字の形として使っている。
// ============================================================================

#include "common.hlsl"

// 文字の画像（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"

#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"

// 頂点シェーダー（litTextureVS）から受け取る値（反射の座標は使わないので省いている）
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
};

// 懐中電灯の標準の明るさ（Player.cppのIntensityの最大値）。電池切れやちらつきで
// 光が弱まると、文字も同じだけ薄くなる。
static const float NominalFlashlightIntensity = 1.5f;

float4 main(in LIT_PS_IN input) : SV_Target
{
    // 文字の形の外は描かない
    const float shape = g_Texture.Sample(g_SamplerState, input.tex).a;
    clip(shape - 0.01f);

    const float presence = input.col.a;
    const float distanceFromCamera = length(input.viewPos);
    // フルブライト表示（デバッグ）では、ライトが当たっていなくても文字をそのまま見せ、配置を確かめられるようにしている。
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT)
    {
        clip(presence - 0.001f);
        return float4(Material.Diffuse.rgb * 2.2f, saturate(shape * presence));
    }
    if (!Light.Enable || !Light.FlashlightEnabled ||
        presence <= 0.001f || distanceFromCamera <= 0.001f)
    {
        discard;
    }

    // 壁と同じ配光・減衰・影で、「この画素に届いている懐中電灯の光」を求めている。
    const float3 pixelDirection = input.viewPos / distanceFromCamera;
    const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
    if (beamProfile <= 0.001f)
    {
        discard;
    }
    const float normalizedDistance = saturate(
        distanceFromCamera / max(Light.Range, 0.001f));
    const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
    const float received = beamProfile * rangeFade * rangeFade *
        GetFlashlightShadow(input.shadowPos) *
        saturate(Light.Intensity / NominalFlashlightIntensity);

    // 光の中心ほど濃く現れる。境目はノイズで崩し、染みが広がるように見せている。
    const float time = Material.Emission.x;
    const float edgeNoise = FastValueNoise(input.worldPos.xy * 0.21f +
        input.worldPos.zz * 0.17f + float2(time * 0.35f, -time * 0.22f));
    const float threshold = 0.18f + edgeNoise * 0.34f;
    const float reveal = smoothstep(threshold, threshold + 0.30f, received * 1.35f);

    const float alpha = saturate(shape * reveal * presence);
    if (alpha <= 0.002f)
    {
        discard;
    }

    // 光を受けた量で明るさを変え、壁に染み込んだ暗い色が照らされているように見せている。
    // 光の中心は少しだけ自ら光らせ、暗い画面でも読めるようにしている。
    const float3 inkColor = Material.Diffuse.rgb;
    float3 color = inkColor * (0.28f + received * 0.72f) +
        inkColor * smoothstep(0.55f, 1.0f, received) * 0.12f;

    // 壁と同じ距離の霧をかけ、文字だけが浮いて見えないようにしている。
    const float distanceFog = smoothstep(110.0f, 390.0f, distanceFromCamera) * 0.72f;
    const float3 fogColor = max(Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color = lerp(color, fogColor, distanceFog);

    return float4(color, alpha);
}
