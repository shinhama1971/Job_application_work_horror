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
#include "horrorGrade.hlsli"

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

// 文字の画像で実際に描く透明度の範囲と、デバッグ表示の明るさ。
#define WRITING_MASK_MIN_ALPHA 0.01f
#define WRITING_DEBUG_MIN_PRESENCE 0.001f
#define WRITING_DEBUG_BRIGHTNESS 2.2f

// 文字が現れる境目は、場所と時間でゆっくり変化する。
#define WRITING_EDGE_NOISE_XY_SCALE 0.21f
#define WRITING_EDGE_NOISE_Z_SCALE 0.17f
#define WRITING_EDGE_NOISE_FLOW_X 0.35f
#define WRITING_EDGE_NOISE_FLOW_Y 0.22f
#define WRITING_REVEAL_BASE_THRESHOLD 0.18f
#define WRITING_REVEAL_NOISE_WEIGHT 0.34f
#define WRITING_REVEAL_BLEND_WIDTH 0.30f
#define WRITING_REVEAL_LIGHT_GAIN 1.35f

// 薄すぎる部分は描かず、光の中心は文字を読めるように少し明るくする。
#define WRITING_MIN_VISIBLE_ALPHA 0.002f
#define WRITING_INK_BASE_GAIN 0.28f
#define WRITING_INK_RECEIVED_GAIN 0.72f
#define WRITING_INK_GLINT_START 0.55f
#define WRITING_INK_GLINT_GAIN 0.12f

float4 main(in LIT_PS_IN input) : SV_Target
{
    // 文字の形の外は描かない
    const float shape = g_Texture.Sample(g_SamplerState, input.tex).a;
    clip(shape - WRITING_MASK_MIN_ALPHA);

    const float presence = input.col.a;
    const float distanceFromCamera = length(input.viewPos);
    // フルブライト表示（デバッグ）では、ライトが当たっていなくても文字をそのまま見せ、配置を確かめられるようにしている。
    if (IsFullbrightDebugView())
    {
        clip(presence - WRITING_DEBUG_MIN_PRESENCE);
        return float4(Material.Diffuse.rgb * WRITING_DEBUG_BRIGHTNESS, saturate(shape * presence));
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
    const float edgeNoise = FastValueNoise(input.worldPos.xy * WRITING_EDGE_NOISE_XY_SCALE +
        input.worldPos.zz * WRITING_EDGE_NOISE_Z_SCALE + float2(time * WRITING_EDGE_NOISE_FLOW_X, -time * WRITING_EDGE_NOISE_FLOW_Y));
    const float threshold = WRITING_REVEAL_BASE_THRESHOLD + edgeNoise * WRITING_REVEAL_NOISE_WEIGHT;
    const float reveal = smoothstep(threshold, threshold + WRITING_REVEAL_BLEND_WIDTH, received * WRITING_REVEAL_LIGHT_GAIN);

    const float alpha = saturate(shape * reveal * presence);
    if (alpha <= WRITING_MIN_VISIBLE_ALPHA)
    {
        discard;
    }

    // 光を受けた量で明るさを変え、壁に染み込んだ暗い色が照らされているように見せている。
    // 光の中心は少しだけ自ら光らせ、暗い画面でも読めるようにしている。
    const float3 inkColor = Material.Diffuse.rgb;
    float3 color = inkColor * (WRITING_INK_BASE_GAIN + received * WRITING_INK_RECEIVED_GAIN) +
        inkColor * smoothstep(WRITING_INK_GLINT_START, 1.0f, received) * WRITING_INK_GLINT_GAIN;

    // 壁と同じ距離の霧をかけ、文字だけが浮いて見えないようにしている。
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color = lerp(color, fogColor, distanceFog);

    return float4(color, alpha);
}
