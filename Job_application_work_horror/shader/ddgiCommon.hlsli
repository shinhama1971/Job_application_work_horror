// ============================================================================
// 共通の処理: DDGI（Dynamic Diffuse Global Illumination。Majercik ほか 2019「Dynamic Diffuse Global Illumination
// with Ray-Traced Irradiance Fields」）の、プローブの格子・八面体マップ・光線の向き・放射照度の読み方。
// DDGIVolume.cpp の DDGIParameters と同じ並びの定数バッファ（b0）を使う、ddgiTraceCS・ddgiIrradianceUpdateCS・
// ddgiDistanceUpdateCS・ddgiApplyCS で共有している。
//
// ・部屋の範囲に、光を測る点（プローブ）を格子に並べ、各プローブから光線を飛ばして、周りから届く光（放射照度）と、
//   周りの面までの距離を、八面体マップ（球の向きを正方形に開いた画像）に少しずつ混ぜて覚えている。
// ・ある点の間接光は、まわりの8つのプローブの放射照度を、位置（3方向の線形補間）・向き（裏から見たプローブを弱める）・
//   見えるかどうか（距離の平均と分散から、チェビシェフの不等式で壁の向こうのプローブを弱める）で重み付けして求める。
//   さらに、点とプローブの間に壁があるプローブは使わない（部屋の形との交差で直接調べる。b11 が要る）。
// ============================================================================
#ifndef DDGI_COMMON_INCLUDED
#define DDGI_COMMON_INCLUDED

// DDGIVolume.cpp の DDGIParameters と同じ並び（b0）
cbuffer DDGIBuffer : register(b0)
{
    float3 ProbeOrigin;         // 一番手前・左・下のプローブの位置
    uint RaysPerProbe;          // 1つのプローブから飛ばす光線の数
    float3 ProbeSpacing;        // プローブの間隔（x・y・z）
    float Hysteresis;           // 前の値を残す割合（大きいほどなめらかだが、変化に遅れる）
    uint3 ProbeCounts;          // プローブの数（x・y・z）
    uint LightCount;            // 点光源の数（t0）
    float4x4 RayRotation;       // 光線の向きを毎フレーム回す行列（同じ向きばかり調べて模様が出ないように）
    float3 FlashlightPosition;  // 懐中電灯の位置（ワールド）と届く距離
    float FlashlightRange;
    float3 FlashlightDirection; // 懐中電灯の向き（ワールド）と明るさ
    float FlashlightIntensity;
    float3 FlashlightColor;     // 懐中電灯の色と、点いているか（1なら点いている）
    float FlashlightEnabled;
    float4 FlashlightCone;      // x = 内側の円錐のcos、y = 外側の円錐のcos
    float NormalBias;           // 読む位置を面の向きへずらす距離（面の裏のプローブを避ける）
    float ViewBias;             // 読む位置を視線の向きへずらす距離
    float BounceStrength;       // 前のフレームの間接光を、光線の当たった面にどれだけ足すか（2回目以降の跳ね返り）
    float ResetHistory;         // 1なら前の値を使わない（格子を作り直した最初のフレーム）
    float4x4 InverseProjection; // 画面 → 視点の空間（ddgiApplyCS）
    float4x4 InverseView;       // 視点の空間 → ワールド（ddgiApplyCS）
    float2 ViewportSize;        // 描く範囲の大きさと、間接光の画像の大きさ（縦横半分）
    float2 OutputSize;
    float2 ViewportOffset;
    float MaxRayDistance;       // 光線を追う一番遠い距離（どこにも当たらなければこの距離）
    uint ProbeUpdateStride;     // 全プローブを何フレームに分けて更新するか
    uint ProbeUpdatePhase;      // このフレームで更新する、通し番号の余り
    uint3 DDGIPadding;
};

// 放射照度の画像（1プローブ = 8x8画素 + まわり1画素の縁）と、距離の画像（16x16画素 + 縁。x = 平均、y = 2乗の平均）。
// スレッドの数（numthreads）にも使うので、#define にしている（DDGIVolume.h と同じ値）
#define DDGI_IRRADIANCE_TEXELS 8
#define DDGI_DISTANCE_TEXELS 16
static const uint DDGI_BORDER = 1;

// 光線の数の上限（光線を書く画像の幅。DDGIVolume.h の RaysPerProbe と同じ）
#define DDGI_MAX_RAYS 64
static const float DDGI_PI = 3.14159265f;
// 黄金比（球の上にほぼ均等に向きを並べるフィボナッチ格子に使う）
static const float DDGI_GOLDEN_RATIO = 1.61803399f;
// 裏から見たプローブの重みの下限と、見えにくいプローブの重みを急に小さくし始める値（Majercik 2019）
static const float DDGI_BACKFACE_MIN_WEIGHT = 0.2f;
static const float DDGI_CHEBYSHEV_MIN_WEIGHT = 0.05f;
static const float DDGI_WEIGHT_CRUSH_THRESHOLD = 0.2f;
// 0で割らないための小さな値
static const float DDGI_EPSILON = 1e-4f;

// 部屋の形（壁を上から見た長方形と、床・天井の高さ。b11）。光線を当てる形と、壁の向こうのプローブを使わない判定に使う
#include "roomOcclusion.hlsli"

// 壁との交差で、面から浮かせる距離（自分の乗っている面や、ちょうど面の上の点を「間の壁」と見ないため）
static const float DDGI_SURFACE_OFFSET = 0.1f;

// 全部の壁の印（32枚まで。1ビット = 1枚）
static const uint DDGI_ALL_WALLS = 0xFFFFFFFFu;

// 上から見た長方形（minimum〜maximum）に重なる壁の印を返している。
// 画面に当てるとき、8つのプローブとの間の壁を調べる前に、プローブの範囲に重なる壁だけに絞るのに使う
uint GetWallsOverlapping(float2 minimum, float2 maximum)
{
    uint mask = 0;
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float4 box = OcclusionBoxes[i];
        if (all(box.xy + box.zw >= minimum) && all(box.xy - box.zw <= maximum))
        {
            mask |= 1u << i;
        }
    }
    return mask;
}

// 点 from から点 to までの間に、印の付いた壁（wallMask）があるか
// （壁は床から天井までの箱なので、上から見た長方形と線分の交差だけで分かる）
bool IsWallBetween(float3 from, float3 to, uint wallMask)
{
    const float3 offset = to - from;
    const float segmentLength = length(offset);
    const float3 direction = offset / max(segmentLength, DDGI_EPSILON);
    const float2 safeDirection = float2(
        abs(direction.x) > DDGI_EPSILON ? direction.x : DDGI_EPSILON,
        abs(direction.z) > DDGI_EPSILON ? direction.z : DDGI_EPSILON);
    const float2 inverseDirection = 1.0f / safeDirection;
    uint remaining = wallMask & (OcclusionBoxCount >= MaxOcclusionBoxes
        ? DDGI_ALL_WALLS : ((1u << OcclusionBoxCount) - 1u));
    [loop]
    while (remaining != 0)
    {
        // 一番下の印の番号（配列の外を読まないよう、上限に収めている）
        const uint i = min(firstbitlow(remaining), MaxOcclusionBoxes - 1u);
        remaining &= remaining - 1u;
        const float4 box = OcclusionBoxes[i];
        const float2 slabNear = (box.xy - box.zw - from.xz) * inverseDirection;
        const float2 slabFar = (box.xy + box.zw - from.xz) * inverseDirection;
        const float enter = max(min(slabNear.x, slabFar.x), min(slabNear.y, slabFar.y));
        const float exit = min(max(slabNear.x, slabFar.x), max(slabNear.y, slabFar.y));
        if (enter <= exit && exit > DDGI_SURFACE_OFFSET && enter < segmentLength - DDGI_SURFACE_OFFSET)
        {
            return true;
        }
    }
    return false;
}

bool IsWallBetween(float3 from, float3 to)
{
    return IsWallBetween(from, to, DDGI_ALL_WALLS);
}

// 放射照度と距離の画像（前のフレームの物。光線の当たった面の跳ね返りと、画面への適用で読む）と、線形補間のサンプラー
Texture2D<float4> DDGIIrradianceAtlas : register(t2);
Texture2D<float2> DDGIDistanceAtlas : register(t3);
SamplerState DDGILinearClamp : register(s0);

// 0で割らないよう、0を+1として扱う符号
float2 SignNotZero(float2 value)
{
    return float2(value.x >= 0.0f ? 1.0f : -1.0f, value.y >= 0.0f ? 1.0f : -1.0f);
}

// 向き（長さ1）を八面体マップの座標（-1〜1）へ開いている
float2 OctahedralEncode(float3 direction)
{
    const float3 n = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    return n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * SignNotZero(n.xy);
}

// 八面体マップの座標（-1〜1）から向きへ戻している
float3 OctahedralDecode(float2 coordinate)
{
    float3 n = float3(coordinate, 1.0f - abs(coordinate.x) - abs(coordinate.y));
    if (n.z < 0.0f)
    {
        n.xy = (1.0f - abs(n.yx)) * SignNotZero(n.xy);
    }
    return normalize(n);
}

// 球の上にほぼ均等に並べた向き（フィボナッチ格子）の index 番目を、毎フレームの回転で回している
float3 GetProbeRayDirection(uint index)
{
    const float i = float(index);
    const float count = float(RaysPerProbe);
    const float phi = 2.0f * DDGI_PI * frac(i * (DDGI_GOLDEN_RATIO - 1.0f));
    const float cosTheta = 1.0f - (2.0f * i + 1.0f) / count;
    const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    const float3 direction = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(mul(float4(direction, 0.0f), RayRotation).xyz);
}

uint GetProbeCount()
{
    return ProbeCounts.x * ProbeCounts.y * ProbeCounts.z;
}

// プローブの格子の位置（x・y・z）と通し番号の行き来
uint GetUpdatingProbeIndex(uint groupIndex)
{
    return groupIndex * ProbeUpdateStride + ProbeUpdatePhase;
}

uint3 GetProbeGrid(uint index)
{
    const uint layer = ProbeCounts.x * ProbeCounts.y;
    return uint3(index % ProbeCounts.x, (index % layer) / ProbeCounts.x, index / layer);
}

float3 GetProbePosition(uint3 grid)
{
    return ProbeOrigin + float3(grid) * ProbeSpacing;
}

// 画像の中のプローブの区画の左上（横に x と y の段を並べ、縦に z を並べている）
uint2 GetProbeTileOrigin(uint3 grid, uint interiorTexels)
{
    const uint tileSize = interiorTexels + DDGI_BORDER * 2;
    return uint2((grid.x + grid.y * ProbeCounts.x) * tileSize, grid.z * tileSize);
}

// プローブの区画の中で、向きに当たる所の画像の座標（0〜1）
float2 GetProbeAtlasUV(uint3 grid, float3 direction, uint interiorTexels, float2 atlasSize)
{
    const float2 coordinate = OctahedralEncode(direction) * 0.5f + 0.5f;
    const float2 texel = float2(GetProbeTileOrigin(grid, interiorTexels)) + DDGI_BORDER + coordinate * interiorTexels;
    return texel / atlasSize;
}

// 点（worldPosition）の、面の向き（normal）から届く間接光を、まわりの8つのプローブから求めている。
// viewDirection は点からカメラへの向き（光線の当たった面で使うときは0）。
// testWalls が true なら、点との間に壁があるプローブを使わない（画面に当てるとき。光線の当たった面の跳ね返りでは、
// 数が多く重いのと、弱くぼやけた光で漏れが目立たないので調べない）
float3 GetDDGIIrradiance(float3 worldPosition, float3 normal, float3 viewDirection, bool testWalls)
{
    float2 irradianceSize;
    float2 distanceSize;
    DDGIIrradianceAtlas.GetDimensions(irradianceSize.x, irradianceSize.y);
    DDGIDistanceAtlas.GetDimensions(distanceSize.x, distanceSize.y);

    // 面の裏のプローブを避けるため、読む位置を面の向きと視線の向きへ少しずらしている
    const float3 biasedPosition = worldPosition + normal * NormalBias + viewDirection * ViewBias;
    const int3 maximumBase = int3(ProbeCounts) - 2;
    const int3 baseGrid = clamp(int3(floor((biasedPosition - ProbeOrigin) / ProbeSpacing)), int3(0, 0, 0), maximumBase);
    const float3 alpha = saturate((biasedPosition - GetProbePosition(uint3(baseGrid))) / ProbeSpacing);
    // 8つのプローブと点を囲む範囲に重なる壁だけを、間の壁の判定で調べる（全部の壁を8回調べると重いため）
    uint nearbyWalls = 0;
    if (testWalls)
    {
        const float3 cellMinimum = GetProbePosition(uint3(baseGrid));
        const float3 cellMaximum = cellMinimum + ProbeSpacing;
        nearbyWalls = GetWallsOverlapping(
            min(cellMinimum.xz, biasedPosition.xz), max(cellMaximum.xz, biasedPosition.xz));
    }

    float3 irradianceSum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner)
    {
        const uint3 offset = uint3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        const uint3 grid = uint3(baseGrid) + offset;
        const float3 probePosition = GetProbePosition(grid);

        // 壁の向こうのプローブは使わない。このゲームは壁の形を式で持っているので、点とプローブの間の壁を直接調べている
        // （距離の画像による判定だけでは、薄い壁や角で、向こうの部屋の光が漏れた）
        // HLSL の && は右側も計算するので、印がないときに判定の関数を呼ばないよう、if を分けている
        [branch]
        if (nearbyWalls != 0)
        {
            if (IsWallBetween(biasedPosition, probePosition, nearbyWalls))
            {
                continue;
            }
        }

        // 位置の線形補間の重み
        const float3 trilinear = lerp(1.0f - alpha, alpha, float3(offset));
        float weight = 1.0f;

        // 裏から見たプローブ（面の後ろにあるプローブ）を弱めている
        const float3 toProbe = normalize(probePosition - worldPosition);
        const float wrapped = (dot(toProbe, normal) + 1.0f) * 0.5f;
        weight *= wrapped * wrapped + DDGI_BACKFACE_MIN_WEIGHT;

        // 見えるかどうか：プローブから見たこの向きの面までの距離の平均と分散で、壁の向こうのプローブを弱めている（チェビシェフの不等式）
        const float3 probeToPoint = biasedPosition - probePosition;
        const float distanceToPoint = length(probeToPoint);
        const float2 moments = DDGIDistanceAtlas.SampleLevel(DDGILinearClamp,
            GetProbeAtlasUV(grid, probeToPoint / max(distanceToPoint, DDGI_EPSILON), DDGI_DISTANCE_TEXELS, distanceSize), 0.0f);
        if (distanceToPoint > moments.x)
        {
            const float variance = abs(moments.y - moments.x * moments.x);
            const float difference = distanceToPoint - moments.x;
            float chebyshev = variance / max(variance + difference * difference, DDGI_EPSILON);
            chebyshev = max(chebyshev * chebyshev * chebyshev, 0.0f);
            weight *= max(chebyshev, DDGI_CHEBYSHEV_MIN_WEIGHT);
        }

        // とても小さい重みは、さらに小さくしている（光漏れを減らす。Majercik 2019）
        weight = max(weight, DDGI_EPSILON);
        if (weight < DDGI_WEIGHT_CRUSH_THRESHOLD)
        {
            weight *= weight * weight / (DDGI_WEIGHT_CRUSH_THRESHOLD * DDGI_WEIGHT_CRUSH_THRESHOLD);
        }
        weight *= trilinear.x * trilinear.y * trilinear.z;

        const float3 irradiance = DDGIIrradianceAtlas.SampleLevel(DDGILinearClamp,
            GetProbeAtlasUV(grid, normal, DDGI_IRRADIANCE_TEXELS, irradianceSize), 0.0f).rgb;
        irradianceSum += irradiance * weight;
        weightSum += weight;
    }
    // 重みが全部0（8つとも壁の向こう）なら、間接光なし
    return weightSum > DDGI_EPSILON ? irradianceSum / max(weightSum, DDGI_EPSILON) : 0.0f;
}

#endif
