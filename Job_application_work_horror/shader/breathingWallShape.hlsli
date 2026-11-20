// 呼吸する壁の変位を、描画と当たり判定で同じ式から求める。
#ifndef SIGNAL_LOST_BREATHING_WALL_SHAPE
#define SIGNAL_LOST_BREATHING_WALL_SHAPE

#include "renderingConfig.hlsli"

#ifdef __cplusplus
namespace BreathingWallShape
{
    inline float saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }
    inline float smoothstep(float low, float high, float value)
    {
        const float t = saturate((value - low) / (high - low));
        return t * t * (3.0f - 2.0f * t);
    }
    using std::cos;
    using std::sin;
    using std::pow;
#endif

static const float BreathRadiansPerCycle = 6.28318530f;
static const float BreathUniformShare = 0.25f;
static const float BreathLumpShare = 0.75f;
static const float BreathLumpSharpness = 2.0f;
static const float BreathLumpGain = 2.0f;
// 面全体が動く段階では、境目を細く固定し、面ごとの小さな波をその上に重ねる。
static const float BreathWholeFaceSeamWidth = 4.0f;
static const float BreathWholeFaceUniformShare = 1.05f;
static const float BreathWholeFaceLumpShare = 0.15f;
static const float BreathLocalMaximumRatio = BreathUniformShare + BreathLumpShare * BreathLumpGain;
static const float BreathWholeFaceMaximumRatio = BreathWholeFaceUniformShare + BreathWholeFaceLumpShare * BreathLumpGain;
// 面に沿った横と高さで、膨らみが移動する2つの波を重ねる。
static const float BreathScaleAX = 0.045f;
static const float BreathScaleAY = 0.085f;
static const float BreathScaleBX = 0.105f;
static const float BreathScaleBY = 0.16f;
static const float BreathSpeedAX = 0.31f;
static const float BreathSpeedAY = -0.23f;
static const float BreathSpeedBX = -0.47f;
static const float BreathSpeedBY = 0.37f;
static const float BreathLumpBWeight = 0.5f;
// 0で割らないための小さな長さ
static const float BreathMinDistance = 0.001f;

// 廊下に沿った位相と時間を異なる速さでずらし、ふくらむ場所が毎回同じにならないようにする。
static const float BreathSlowWaveScale = 0.035f;
static const float BreathSlowWaveHeight = 0.021f;
static const float BreathSlowWaveSpeed = 0.67f;
static const float BreathSlowWavePhase = 0.85f;
static const float BreathFastWaveScale = 0.079f;
static const float BreathFastWaveHeight = 0.047f;
static const float BreathFastWaveSpeed = 1.13f;
static const float BreathFastWavePhase = 0.40f;

inline float BreathOffset(float time, float amplitude, float edgeWidth, bool wholeFace, float edgeAlong, float edgeY,
    float planeAlong, float planeY)
{
    const float seamWidth = wholeFace ? BreathWholeFaceSeamWidth : edgeWidth;
    const float pinned = smoothstep(0.0f, seamWidth, edgeAlong) * smoothstep(0.0f, seamWidth, edgeY);
    float phase = time * (BreathRadiansPerCycle / SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS);
    if (wholeFace)
    {
        phase += BreathSlowWavePhase * sin(planeAlong * BreathSlowWaveScale + planeY * BreathSlowWaveHeight - time * BreathSlowWaveSpeed);
        phase += BreathFastWavePhase * sin(planeAlong * BreathFastWaveScale - planeY * BreathFastWaveHeight + time * BreathFastWaveSpeed);
    }
    const float breath = 0.5f - 0.5f * cos(phase);
    const float waveA = sin(planeAlong * BreathScaleAX + BreathSpeedAX * time) *
        sin(planeY * BreathScaleAY + BreathSpeedAY * time);
    const float waveB = sin(planeAlong * BreathScaleBX + BreathSpeedBX * time) *
        sin(planeY * BreathScaleBY + BreathSpeedBY * time);
    const float lumps = saturate(0.5f + 0.5f * (waveA + BreathLumpBWeight * waveB) / (1.0f + BreathLumpBWeight));
    const float shapedLumps = pow(lumps, BreathLumpSharpness) * BreathLumpGain;
    const float profile = wholeFace ? BreathWholeFaceUniformShare + BreathWholeFaceLumpShare * shapedLumps
        : BreathUniformShare + BreathLumpShare * shapedLumps;
    return amplitude * pinned * breath * profile;
}

#ifdef __cplusplus
}
#endif
#endif
