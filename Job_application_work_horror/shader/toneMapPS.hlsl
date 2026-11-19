// ============================================================================
// シェーダーの役割: HDRで描いた画面（明るさが1を超えてもよい）を、画面に出せる0〜1の明るさへ移している（トーンマッピング）。
//                   画面効果（露出・ブルーム・光の筋・ノイズ）を全部足した後、HUDを描く前に1回だけ掛けている。
// 主な技術: ACES のフィルムの曲線（toneMapping.hlsli）
//
// ・以前は、壁や床のシェーダーがそれぞれ曲線を28%だけ混ぜて1で切っていたため、電球や懐中電灯の当たった所が白く平らに飛んでいた。
//   今は壁や床が1を超える明るさをそのまま書き、ここで1回だけ曲線を掛けるので、明るい所がなだらかに丸まり、濃淡が残る。
// ・ToneMapMode が1のときは、HDRにする前に近い曲線（28%だけ混ぜて1で切る）にしている（デバッグ画面と --tonemap legacy で見比べる用）。
// ============================================================================

#include "toneMapping.hlsli"
#include "renderingConfig.hlsli"

// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// FullScreenQuad.cppのToneMapBufferと同じ並び（b0）
cbuffer ToneMapBuffer : register(b0)
{
    uint ToneMapMode;   // 0 = ACES、1 = HDRにする前に近い曲線、2 = 曲線なし（デバッグ表示）、3 = Reinhard（Effect::ToneMapMode）
    uint3 ToneMapPadding;
};

// HDRで描いた画面（t0）
Texture2D hdrScene : register(t0);

static const uint TONE_MAP_MODE_LEGACY = SIGNAL_LOST_TONE_MAP_LEGACY;
static const uint TONE_MAP_MODE_PASSTHROUGH = SIGNAL_LOST_TONE_MAP_PASSTHROUGH;
static const uint TONE_MAP_MODE_REINHARD = SIGNAL_LOST_TONE_MAP_REINHARD;

// HDRにする前の曲線：ACES を混ぜていた割合
static const float LEGACY_ACES_BLEND = 0.28f;

float4 main(PS_IN input) : SV_TARGET
{
    // 画面と同じ大きさなので、補間せずに同じ画素を読んでいる
    const float3 color = max(hdrScene.Load(int3(int2(input.pos.xy), 0)).rgb, 0.0f);
    if (ToneMapMode == TONE_MAP_MODE_PASSTHROUGH)
    {
        return float4(saturate(color), 1.0f);
    }
    if (ToneMapMode == TONE_MAP_MODE_LEGACY)
    {
        return float4(saturate(lerp(color, AcesFilmic(color), LEGACY_ACES_BLEND)), 1.0f);
    }
    if (ToneMapMode == TONE_MAP_MODE_REINHARD)
    {
        // ACESと入力の倍率をそろえ、RGB比を保ったまま輝度 L を L/(1+L) に圧縮する。
        const float3 exposed = color * ACES_EXPOSURE;
        const float luminance = dot(exposed, TONE_MAP_LUMINANCE_WEIGHTS);
        return float4(saturate(exposed / (1.0f + luminance)), 1.0f);
    }
    return float4(ToneMapAces(color), 1.0f);
}
