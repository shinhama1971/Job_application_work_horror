// ============================================================================
// シェーダーの役割: 外の森の空の画素に、地面や木と同じ霧（距離の霧・高さの霧・床の近くの霧）と色調を掛けている（SkyBackdrop.cpp）。
// 空の画素を「視線の先の、描画の奥の端にある黒い面」とみなしている。地平線のすぐ上の空は、すぐ下の遠い地面と
// ほぼ同じ距離・同じ高さの霧になるので、霧の灰色と黒い空の境目の線が消える。上を見るほど霧の層を短く通るので、空は暗いまま。
// ============================================================================

#include "common.hlsl"
#include "fastNoise.hlsli"
#include "flashlightShadow.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"

struct SKY_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewDirection : TEXCOORD0;
    float3 worldDirection : TEXCOORD1;
};

// 描画の奥の端（Camera.cpp の MainCameraFarPlane と同じ）と、真上を見たときに霧を通る長さ
static const float SkyFarDistance = 1000.0f;
static const float SkyZenithDistance = 380.0f;
// 背景の色（Renderer.cpp の画面を消す色と同じ、わずかに青みのある黒）
#define SKY_BACKDROP_COLOR float3(0.003f, 0.005f, 0.008f)
// 霧の距離を地平線（奥の端まで）から真上の距離へ変える、向きの上向きの成分の範囲の終わり（約30度）
#define SKY_FOG_ZENITH_DIRECTION 0.5f

float4 main(SKY_PS_IN input) : SV_TARGET
{
    const float3 worldDirection = normalize(input.worldDirection);
    const float3 viewDirection = normalize(input.viewDirection);
    // 地平線の近くは奥の端まで、上を見るほど短い距離の霧にしている
    const float distanceFromCamera = lerp(SkyFarDistance, SkyZenithDistance, smoothstep(0.0f, SKY_FOG_ZENITH_DIRECTION, worldDirection.y));
    const float3 eye = GetMistEyePosition();
    const float3 worldPosition = eye + worldDirection * distanceFromCamera;
    const float3 viewPosition = viewDirection * distanceFromCamera;

    // 背景の色（Renderer.cpp の画面を消す色と同じ、わずかに青みのある黒）
    float3 color = SKY_BACKDROP_COLOR;
    // 以下は litTexturePS・grassPS と同じ霧の式
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightDensity = exp(-max(worldPosition.y - SCENE_BASE_FLOOR_Y, 0.0f) * HEIGHT_FOG_DECAY);
    const float heightFog = heightDensity * smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera) *
        (HEIGHT_FOG_BASE_VARIATION + FastValueNoise(worldPosition.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET) * HEIGHT_FOG_NOISE_WEIGHT) *
        HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
    color = lerp(color, GetHorrorFogColor(Light.Ambient.rgb, LoopDepth), saturate(distanceFog + heightFog));
    color = ApplyHorrorMist(color, worldPosition, viewPosition, 0.0f);
    color = ApplyFilmicHorrorGrade(color, LoopDepth);
    return float4(color, 1.0f);
}
