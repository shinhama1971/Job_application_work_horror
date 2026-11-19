// ============================================================================
// 共通の処理: 浸水した床（1面の西棟）と水たまりの水面で跳ね返った懐中電灯の光が、壁や天井にゆらゆら映る効果（コースティクス）。
// 主な技術: 水面での鏡映（画素を水面で上下に折り返した点）、懐中電灯の配光の再利用、流れるノイズの網目模様
// ・壁や天井の点Pに水面から届く光は、「Pを水面で折り返した点P'」へ向かう懐中電灯の光が、水面で跳ね返ったもの。
//   そこで、懐中電灯がP'の方向を照らしているかを、ふつうの照明と同じ配光の式で求めている。
// ・光が水面に当たる点Wは、カメラとP'を結ぶ線と水面の交点。Wの位置で、波が光を集めてできる明るい網目模様を作っている。
// ・水たまりでは、足音や水滴でできた波（WaterRippleSimulationの高さ）のへこみが光を集め、光の輪が壁を横切る。
// ・カメラの位置は、定数バッファで受け取ったプレイヤーの視点のビュー行列から求めている。
// common.hlsl・fastNoise.hlsli・flashlightLighting.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef WATER_CAUSTICS_INCLUDED
#define WATER_CAUSTICS_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"
#include "waterCausticsLayout.hlsli"


// 水たまりの波のシミュレーションの高さ（t9。WaterRippleSimulationが毎フレーム計算している）。x = 今の高さ、y = 1つ前の高さ
Texture2D<float2> g_WaterRipples : register(t9);
// 波のマス目の数（1面・2面は256、外の森は768）。テクスチャの大きさから読んでいる
int GetWaterRippleResolution()
{
    uint width;
    uint height;
    g_WaterRipples.GetDimensions(width, height);
    return int(max(width, 1u));
}

// 波のマス目1つの高さを読んでいる（範囲の外は0）
float LoadRippleHeight(int2 texel)
{
    if (any(texel < 0) || any(texel >= GetWaterRippleResolution()))
    {
        return 0.0f;
    }
    return g_WaterRipples.Load(int3(texel, 0)).x;
}

// ワールドの点（x・z）の波の高さを、まわりの4マスから補間して返している
float SampleRippleHeight(float2 positionXZ)
{
    const float2 texelPosition =
        (positionXZ - CausticRipple.xy) / CausticRipple.z * GetWaterRippleResolution() - 0.5f;
    const int2 baseTexel = int2(floor(texelPosition));
    const float2 blend = texelPosition - baseTexel;
    return lerp(
        lerp(LoadRippleHeight(baseTexel), LoadRippleHeight(baseTexel + int2(1, 0)), blend.x),
        lerp(LoadRippleHeight(baseTexel + int2(0, 1)), LoadRippleHeight(baseTexel + int2(1, 1)), blend.x),
        blend.y);
}

// 波の高さ（x）と、x方向・z方向の傾き（yz）を返している。波がないとき（強さ0）はすべて0
float3 GetRippleHeightAndSlope(float2 positionXZ)
{
    if (CausticRipple.w <= 0.0f)
    {
        return 0.0f;
    }
    const float step = CausticRipple.z / GetWaterRippleResolution();
    const float center = SampleRippleHeight(positionXZ);
    const float right = SampleRippleHeight(positionXZ + float2(step, 0.0f));
    const float forward = SampleRippleHeight(positionXZ + float2(0.0f, step));
    return float3(center, (right - center) / step, (forward - center) / step);
}

// 波の面のへこみ具合（ラプラシアン）から、波が光を集めて明るくなる度合い（0〜1）を返している。
// へこんだ所は凹面鏡のように光を集め、盛り上がった所は光を散らすため、正の値の所だけを明るくしている。
// 波が光を集める強さ（曲がり具合に掛ける）
#define RIPPLE_CAUSTIC_FOCUS_GAIN 0.9f

float GetRippleCausticFocus(float2 positionXZ)
{
    if (CausticRipple.w <= 0.0f)
    {
        return 0.0f;
    }
    const float step = CausticRipple.z / GetWaterRippleResolution();
    const float center = SampleRippleHeight(positionXZ);
    const float laplacian = (
        SampleRippleHeight(positionXZ + float2(step, 0.0f)) +
        SampleRippleHeight(positionXZ - float2(step, 0.0f)) +
        SampleRippleHeight(positionXZ + float2(0.0f, step)) +
        SampleRippleHeight(positionXZ - float2(0.0f, step)) - 4.0f * center) / (step * step);
    return saturate(laplacian * RIPPLE_CAUSTIC_FOCUS_GAIN) * CausticRipple.w;
}

// 揺らぎで網目の位置を曲げ、二層のノイズを違う速さで動かす。
#define CAUSTIC_GRID_SCALE 0.12f
#define CAUSTIC_WARP_NOISE_SCALE 0.6f
#define CAUSTIC_WARP_FLOW_AX 0.15f
#define CAUSTIC_WARP_FLOW_AZ 0.07f
#define CAUSTIC_WARP_FLOW_BX 0.12f
#define CAUSTIC_WARP_FLOW_BZ 0.10f
#define CAUSTIC_WARP_NOISE_SEED 17.3f
#define CAUSTIC_WARP_AMPLITUDE 2.2f
#define CAUSTIC_FIRST_LAYER_FLOW 0.20f
#define CAUSTIC_SECOND_LAYER_SCALE 1.37f
#define CAUSTIC_SECOND_LAYER_WARP 0.8f
#define CAUSTIC_SECOND_LAYER_FLOW 0.17f
#define CAUSTIC_SECOND_LAYER_SEED 5.1f

// ノイズの尾根だけを細い光にし、重なる二層の明るさを別に調整する。
#define CAUSTIC_RIDGE_EXPONENT 6.0f
#define CAUSTIC_FIRST_RIDGE_WEIGHT 0.85f
#define CAUSTIC_SECOND_RIDGE_WEIGHT 0.65f

// 水面付近では交差が不安定になりやすいため、視点と投影される面に同じ余白を設ける。
#define CAUSTIC_WATER_SURFACE_MARGIN 0.05f

// 浸水範囲の縁では、壁へ映る光を急に切らずになじませる。
#define CAUSTIC_FLOOD_EDGE_WIDTH 6.0f

// 水面の点（x・z）での、波が光を集めてできる網目模様（0〜1）を返している。
// 流れるノイズで座標をゆがめ、ノイズの値が0.5になる線（尾根）を細く明るくして、2枚を重ねている。
float GetCausticPattern(float2 waterPositionXZ, float time)
{
    const float2 position = waterPositionXZ * CAUSTIC_GRID_SCALE;
    const float2 warp = float2(
        FastValueNoise(position * CAUSTIC_WARP_NOISE_SCALE + float2(time * CAUSTIC_WARP_FLOW_AX, time * CAUSTIC_WARP_FLOW_AZ)),
        FastValueNoise(position * CAUSTIC_WARP_NOISE_SCALE + float2(-time * CAUSTIC_WARP_FLOW_BX, time * CAUSTIC_WARP_FLOW_BZ) + CAUSTIC_WARP_NOISE_SEED)) * CAUSTIC_WARP_AMPLITUDE;
    const float first = FastValueNoise(position + warp + time * CAUSTIC_FIRST_LAYER_FLOW);
    const float second = FastValueNoise(position * CAUSTIC_SECOND_LAYER_SCALE - warp * CAUSTIC_SECOND_LAYER_WARP - time * CAUSTIC_SECOND_LAYER_FLOW + CAUSTIC_SECOND_LAYER_SEED);
    // 1 - |2n - 1| は、ノイズが0.5の所で1になる尾根。6乗して細い光の線にしている
    const float firstRidge = 1.0f - abs(first * 2.0f - 1.0f);
    const float secondRidge = 1.0f - abs(second * 2.0f - 1.0f);
    return saturate(pow(firstRidge, CAUSTIC_RIDGE_EXPONENT) * CAUSTIC_FIRST_RIDGE_WEIGHT + pow(secondRidge, CAUSTIC_RIDGE_EXPONENT) * CAUSTIC_SECOND_RIDGE_WEIGHT);
}

// 壁や天井の点（ワールド座標）に、水面で跳ね返って届く懐中電灯の光の明るさ（色は掛ける前）を返している
// 0で割らないための下限、懐中電灯が届いているとみなす下限、浸水した範囲の光（全体の明るさ・網目の強さ）、波の輪の光の強さ
#define CAUSTIC_MIN_DIVISOR 0.001f
#define CAUSTIC_MIN_BEAM 0.001f
#define CAUSTIC_FLOOD_BASE_LIGHT 0.05f
#define CAUSTIC_FLOOD_PATTERN_LIGHT 0.55f
#define CAUSTIC_RIPPLE_LIGHT 0.55f

float GetWaterCausticLight(float3 worldPosition, float3 worldNormal)
{
    // 効果がない面（浸水した範囲も水たまりの波もない）・懐中電灯を消しているとき・水面より下の点では計算しない
    if ((CausticStrength <= 0.0f && CausticRipple.w <= 0.0f) || !Light.Enable || !Light.FlashlightEnabled ||
        worldPosition.y <= CausticWaterY + CAUSTIC_WATER_SURFACE_MARGIN)
    {
        return 0.0f;
    }

    // カメラの位置をビュー行列から求めている（ビュー空間 = ワールド × CausticView なので、目の位置 = -(回転) × 平行移動）
    const float3 eye = -mul((float3x3)CausticView, CausticView[3].xyz);
    // 水面より下に目がある（水に潜っている）ときは計算しない
    if (eye.y <= CausticWaterY + CAUSTIC_WATER_SURFACE_MARGIN)
    {
        return 0.0f;
    }

    // 点を水面で上下に折り返した点P'と、カメラからP'へ向かう光が水面に当たる点W
    const float3 mirrored = float3(
        worldPosition.x, 2.0f * CausticWaterY - worldPosition.y, worldPosition.z);
    const float waterHit = (eye.y - CausticWaterY) / max(eye.y - mirrored.y, CAUSTIC_MIN_DIVISOR);
    const float3 waterPoint = eye + (mirrored - eye) * waterHit;

    // 光が水面に当たる点が、浸水した範囲の中にあるときだけ（縁は少しずつ弱めている）
    const float2 fromMinimum = waterPoint.xz - CausticFloodRect.xy;
    const float2 toMaximum = CausticFloodRect.zw - waterPoint.xz;
    const float insideDistance = min(
        min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y));
    const float flood = smoothstep(0.0f, CAUSTIC_FLOOD_EDGE_WIDTH, insideDistance) * CausticStrength;
    // 水たまりの波（足音・水滴でできた本物の波）が光を集めている所。波のない所・乾いた床では0になる
    const float rippleFocus = GetRippleCausticFocus(waterPoint.xz);
    if (flood <= 0.0f && rippleFocus <= 0.0f)
    {
        return 0.0f;
    }

    // 懐中電灯がP'の方向を照らしているか（ふつうの照明と同じ円錐の形）と、水面を経由した道のりでの弱まり
    const float3 mirroredView = mul(float4(mirrored, 1.0f), CausticView).xyz;
    const float pathLength = length(mirroredView);
    if (pathLength <= CAUSTIC_MIN_DIVISOR)
    {
        return 0.0f;
    }
    const float beamProfile = GetFlashlightBeamProfile(mirroredView / pathLength);
    if (beamProfile <= CAUSTIC_MIN_BEAM)
    {
        return 0.0f;
    }
    const float normalizedDistance = saturate(pathLength / max(Light.Range, CAUSTIC_MIN_DIVISOR));
    const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);

    // 光は水面の点Wから来るので、面がWの方を向いているほど明るい（天井はほぼ真上から、壁は斜めから受ける）
    const float3 toWater = normalize(waterPoint - worldPosition);
    const float facing = saturate(dot(worldNormal, toWater));

    // 浸水した範囲：全体をうっすら照らす分は小さくし、波が光を集めた網目の線を主役にしている
    float reflected = 0.0f;
    if (flood > 0.0f)
    {
        const float pattern = GetCausticPattern(waterPoint.xz, CausticTime);
        reflected += (CAUSTIC_FLOOD_BASE_LIGHT + pattern * CAUSTIC_FLOOD_PATTERN_LIGHT) * flood;
    }
    // 水たまりの波：広がっていく輪が、光の輪になって壁を横切る
    reflected += rippleFocus * CAUSTIC_RIPPLE_LIGHT;

    return Light.Intensity * beamProfile * rangeFade * rangeFade *
        facing * reflected;
}

#endif
