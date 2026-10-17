// ============================================================================
// シェーダーの役割: 水たまりの水面の波を、波の式（2次元の波動方程式）で1歩進めている。
// 主な技術: Compute Shader、高さの場のシミュレーション（今と1つ前の高さから次の高さを求める）、
//           2枚のテクスチャを交互に読み書きする方法（ピンポン）、範囲をプレイヤーに合わせて動かす仕組み
//
// ・各マスの次の高さ = 2×今の高さ − 1つ前の高さ + 速さ²×（まわり4マスとの差の合計）。これを少し弱めている（減衰）。
//   足音や水滴は、その場所の高さを押し下げる「おもり」として足し、そこから輪が広がる。
// ・水たまりの外（乾いた床）と、床に立った柱や脚の中は高さを0に固定しているので、波は岸や柱で跳ね返り、隣の輪と重なって干渉する。
// ・計算する範囲はプレイヤーのまわりだけ。プレイヤーが動いたら、マス目の単位で中身をずらして範囲を付いて行かせている。
// ============================================================================

#include "fastNoise.hlsli"
#include "puddleMask.hlsli"

// 1スレッドグループのマスの数（16x16）と、波のマス目の数（WaterRippleSimulation::Resolutionと同じ）
#define GROUP_SIZE 16
#define RIPPLE_RESOLUTION 256
#define MAX_IMPULSES 16
#define MAX_OBSTACLES 32

// WaterRippleSimulation.cppのRippleParamsと同じ並び（b0）
cbuffer WaterRippleBuffer : register(b0)
{
    float2 Origin;              // 範囲の最小のx・z（ワールド座標）
    float TexelSize;            // マス目1つの長さ
    float WaveSpeedSquared;     // (波の速さ × 1歩の秒数 ÷ マス目の長さ)²。0.5以下でないと計算が発散する
    int2 Shift;                 // 前の歩から範囲が動いた分（マス目の数）
    float Damping;              // 1歩ごとに高さに掛ける値（1より少し小さい）
    int ImpulseCount;           // 今回足すおもりの数
    float4 FloodRect;           // 床一面が水に浸かった範囲（xy = 最小、zw = 最大。範囲がないときは最小が最大より大きい）
    float4 Impulses[MAX_IMPULSES];  // おもり（xy = x・z、z = 半径、w = 強さ。負なら押し下げる）
    int ObstacleCount;          // 波を跳ね返す物の数
    float3 ObstaclePadding;
    float4 Obstacles[MAX_OBSTACLES];    // 床に立った物を上から見た長方形（xy = 中心のx・z、zw = 半分の幅と奥行き）
};

Texture2D<float2> PreviousState : register(t0);     // 1歩前の（今の高さ, 1つ前の高さ）
RWTexture2D<float2> NextState : register(u0);       // 次の（今の高さ, 1つ前の高さ）

// 1歩前の状態を読んでいる（範囲の外は静かな水面＝0）
float2 LoadPrevious(int2 texel)
{
    if (any(texel < 0) || any(texel >= RIPPLE_RESOLUTION))
    {
        return 0.0f;
    }
    return PreviousState.Load(int3(texel, 0));
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 texel = int2(dispatchThreadId.xy);
    // 範囲が動いた分だけ、読む場所をずらしている（同じワールドの点の値を読むため）
    const int2 source = texel + Shift;
    const float2 current = LoadPrevious(source);
    const float laplacian =
        LoadPrevious(source + int2(1, 0)).x + LoadPrevious(source - int2(1, 0)).x +
        LoadPrevious(source + int2(0, 1)).x + LoadPrevious(source - int2(0, 1)).x -
        4.0f * current.x;
    float height = (2.0f * current.x - current.y + WaveSpeedSquared * laplacian) * Damping;

    // 足音・水滴のおもりを、その場所を中心にした山なりの形で足している
    const float2 worldPosition = Origin + (float2(texel) + 0.5f) * TexelSize;
    for (int index = 0; index < ImpulseCount; ++index)
    {
        const float4 impulse = Impulses[index];
        const float2 offset = worldPosition - impulse.xy;
        height += impulse.w * exp(-dot(offset, offset) / (impulse.z * impulse.z));
    }

    // 水がある所だけ波を残している（水たまりの形は濡れた床と同じ式。乾いた床では0にして、岸で跳ね返らせる）
    float shore = 0.0f;
    float pattern = 0.0f;
    const float puddle = GetPuddleMask(worldPosition, 0.0f, shore, pattern);
    const float2 fromMinimum = worldPosition - FloodRect.xy;
    const float2 toMaximum = FloodRect.zw - worldPosition;
    const float flood = step(0.0f, min(min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y)));
    const float water = saturate(max(puddle, flood) * 1.5f);
    // 床に立った柱や脚の中も高さを0に固定し、波がその周りで跳ね返るようにしている
    float blocked = 0.0f;
    for (int obstacle = 0; obstacle < ObstacleCount; ++obstacle)
    {
        const float4 box = Obstacles[obstacle];
        blocked = max(blocked, step(abs(worldPosition.x - box.x), box.z) * step(abs(worldPosition.y - box.y), box.w));
    }
    // 範囲の縁の8マスでは少しずつ弱め、縁で跳ね返った波が戻ってこないようにしている
    const int2 edgeDistance = min(texel, RIPPLE_RESOLUTION - 1 - texel);
    const float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) / 8.0f);
    const float keep = water * edgeFade * (1.0f - blocked);

    NextState[texel] = float2(clamp(height, -4.0f, 4.0f) * keep, current.x * keep);
}
