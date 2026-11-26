// ============================================================================
// シェーダーの部品: 目の慣れ（露出・暗順応・まぶしさ）を画面の色に掛ける処理。
// 露出のシェーダー（exposurePS）と、恐怖でピントが奪われる画面効果（crtOverlayPS のぼかし）が、同じ見え方にするため共通で使う。
// 読み込む側で、自動露出の結果のバッファ AutoExposureState（autoExposureCS.hlsl が書く）と、画面効果の定数（postProcessBuffer.hlsli）を
// 用意しておくこと。
// ============================================================================

#ifndef SIGNAL_LOST_EYE_ADAPTATION_HLSLI
#define SIGNAL_LOST_EYE_ADAPTATION_HLSLI

#include "computeDispatchConfig.hlsli"

// 驚かせる演出のフラッシュで露出を上げる量
static const float HORROR_PULSE_EXPOSURE_GAIN = 0.08f;
// 暗順応：暗い所の色を移す先（桿体の感じ方に近い、青緑に重い明るさ）と、その色味、色を抜く最大の割合、
// 照らされて明るい所は色を残す（錐体が働く）明るさの範囲、明るさの重み（Rec. 709）
#define DARK_ADAPTATION_ROD_WEIGHTS float3(0.05f, 0.55f, 0.40f)
#define DARK_ADAPTATION_TINT float3(0.78f, 0.90f, 1.05f)
#define DARK_ADAPTATION_MAX_BLEND 0.85f
#define DARK_ADAPTATION_CONE_START 0.04f
#define DARK_ADAPTATION_CONE_END 0.35f
#define EYE_LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)
// まぶしさ：露出を上げる量
#define DAZZLE_EXPOSURE_GAIN 1.4f

// シーンが決めた露出に、自動露出の倍率を掛けた値（目の慣れを含む露出）
float GetAdaptedExposure()
{
    const float autoGain = AutoExposureState[SIGNAL_LOST_EXPOSURE_GAIN_INDEX];
    return exposure * (autoGain > 0.0f ? autoGain : 1.0f);
}

// 画面の色 scene に、目の慣れを掛けた色を返している
float3 ApplyEyeAdaptation(float3 scene)
{
    // 驚かせる演出のフラッシュもこの倍率に含め、後から描くUIには掛けない
    const float eventExposure = horrorPulseStrength * HORROR_PULSE_EXPOSURE_GAIN;
    // まぶしさ：暗さに慣れた目で急に明るい所を見ると、露出が一瞬上がって白く飛ぶ
    const float dazzle = AutoExposureState[SIGNAL_LOST_EXPOSURE_DAZZLE_INDEX];
    float3 exposed = scene * (GetAdaptedExposure() + eventExposure) * (1.0f + dazzle * DAZZLE_EXPOSURE_GAIN);
    // 暗順応：照らされていない暗い所ほど、色が抜けて青白い白黒になる（照らされた所は色が残る）
    const float darkAdaptation = AutoExposureState[SIGNAL_LOST_EXPOSURE_DARK_ADAPTATION_INDEX];
    [branch]
    if (darkAdaptation > 0.0f)
    {
        const float rodLuminance = dot(exposed, DARK_ADAPTATION_ROD_WEIGHTS);
        const float coneVision = smoothstep(DARK_ADAPTATION_CONE_START, DARK_ADAPTATION_CONE_END, dot(exposed, EYE_LUMINANCE_WEIGHTS));
        exposed = lerp(exposed, rodLuminance * DARK_ADAPTATION_TINT, darkAdaptation * DARK_ADAPTATION_MAX_BLEND * (1.0f - coneVision));
    }
    return exposed;
}

#endif
