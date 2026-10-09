// ============================================================================
// 共通の処理: 浸水した床（1面の西棟）の水面で跳ね返った懐中電灯の光が、壁や天井にゆらゆら映る効果（コースティクス）。
// 主な技術: 水面での鏡映（画素を水面で上下に折り返した点）、懐中電灯の配光の再利用、流れるノイズの網目模様
// ・壁や天井の点Pに水面から届く光は、「Pを水面で折り返した点P'」へ向かう懐中電灯の光が、水面で跳ね返ったもの。
//   そこで、懐中電灯がP'の方向を照らしているかを、ふつうの照明と同じ配光の式で求めている。
// ・光が水面に当たる点Wは、カメラとP'を結ぶ線と水面の交点。Wの位置で、波が光を集めてできる明るい網目模様を作っている。
// ・カメラの位置は、定数バッファで受け取ったプレイヤーの視点のビュー行列から求めている。
// common.hlsl・fastNoise.hlsli・flashlightLighting.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef WATER_CAUSTICS_INCLUDED
#define WATER_CAUSTICS_INCLUDED

// Renderer.hのWATER_CAUSTICS_BUFFERと同じ並び（b12）。Groundが毎フレーム設定している
cbuffer WaterCausticsBuffer : register(b12)
{
    // プレイヤーの視点のビュー行列。懐中電灯はこの視点から照らしているため、反射を描くときもこの行列を使っている
    // （ビュー行列のb1は頂点シェーダーにしか設定していないため、ピクセルシェーダーで使う分はここで受け取っている）
    matrix CausticView;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは最小が最大より大きい
    float4 CausticFloodRect;
    float CausticWaterY;        // 水面の高さ
    float CausticTime;          // 揺らぎを動かす時間（秒）
    float CausticStrength;      // 強さ（0なら計算しない）
    float CausticPadding;
}

// 水面の点（x・z）での、波が光を集めてできる網目模様（0〜1）を返している。
// 流れるノイズで座標をゆがめ、ノイズの値が0.5になる線（尾根）を細く明るくして、2枚を重ねている。
float GetCausticPattern(float2 waterPositionXZ, float time)
{
    const float2 position = waterPositionXZ * 0.12f;
    const float2 warp = float2(
        FastValueNoise(position * 0.6f + float2(time * 0.15f, time * 0.07f)),
        FastValueNoise(position * 0.6f + float2(-time * 0.12f, time * 0.10f) + 17.3f)) * 2.2f;
    const float first = FastValueNoise(position + warp + time * 0.20f);
    const float second = FastValueNoise(position * 1.37f - warp * 0.8f - time * 0.17f + 5.1f);
    // 1 - |2n - 1| は、ノイズが0.5の所で1になる尾根。6乗して細い光の線にしている
    const float firstRidge = 1.0f - abs(first * 2.0f - 1.0f);
    const float secondRidge = 1.0f - abs(second * 2.0f - 1.0f);
    return saturate(pow(firstRidge, 6.0f) * 0.85f + pow(secondRidge, 6.0f) * 0.65f);
}

// 壁や天井の点（ワールド座標）に、水面で跳ね返って届く懐中電灯の光の明るさ（色は掛ける前）を返している
float GetWaterCausticLight(float3 worldPosition, float3 worldNormal)
{
    // 効果がない面・懐中電灯を消しているとき・水面より下の点では計算しない
    if (CausticStrength <= 0.0f || !Light.Enable || !Light.FlashlightEnabled ||
        worldPosition.y <= CausticWaterY + 0.05f)
    {
        return 0.0f;
    }

    // カメラの位置をビュー行列から求めている（ビュー空間 = ワールド × CausticView なので、目の位置 = -(回転) × 平行移動）
    const float3 eye = -mul((float3x3)CausticView, CausticView[3].xyz);
    // 水面より下に目がある（水に潜っている）ときは計算しない
    if (eye.y <= CausticWaterY + 0.05f)
    {
        return 0.0f;
    }

    // 点を水面で上下に折り返した点P'と、カメラからP'へ向かう光が水面に当たる点W
    const float3 mirrored = float3(
        worldPosition.x, 2.0f * CausticWaterY - worldPosition.y, worldPosition.z);
    const float waterHit = (eye.y - CausticWaterY) / max(eye.y - mirrored.y, 0.001f);
    const float3 waterPoint = eye + (mirrored - eye) * waterHit;

    // 光が水面に当たる点が、浸水した範囲の中にあるときだけ（縁は少しずつ弱めている）
    const float2 fromMinimum = waterPoint.xz - CausticFloodRect.xy;
    const float2 toMaximum = CausticFloodRect.zw - waterPoint.xz;
    const float insideDistance = min(
        min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y));
    const float flood = smoothstep(0.0f, 6.0f, insideDistance);
    if (flood <= 0.0f)
    {
        return 0.0f;
    }

    // 懐中電灯がP'の方向を照らしているか（ふつうの照明と同じ円錐の形）と、水面を経由した道のりでの弱まり
    const float3 mirroredView = mul(float4(mirrored, 1.0f), CausticView).xyz;
    const float pathLength = length(mirroredView);
    if (pathLength <= 0.001f)
    {
        return 0.0f;
    }
    const float beamProfile = GetFlashlightBeamProfile(mirroredView / pathLength);
    if (beamProfile <= 0.001f)
    {
        return 0.0f;
    }
    const float normalizedDistance = saturate(pathLength / max(Light.Range, 0.001f));
    const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);

    // 光は水面の点Wから来るので、面がWの方を向いているほど明るい（天井はほぼ真上から、壁は斜めから受ける）
    const float3 toWater = normalize(waterPoint - worldPosition);
    const float facing = saturate(dot(worldNormal, toWater));

    // 全体をうっすら照らす分は小さくし、波が光を集めた網目の線を主役にしている
    const float pattern = GetCausticPattern(waterPoint.xz, CausticTime);
    const float reflected = 0.05f + pattern * 0.55f;

    return Light.Intensity * beamProfile * rangeFade * rangeFade *
        facing * reflected * flood * CausticStrength;
}

#endif
