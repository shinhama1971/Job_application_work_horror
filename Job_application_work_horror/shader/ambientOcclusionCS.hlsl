// ============================================================================
// シェーダーの役割: 画面空間の環境遮蔽（SSAO）。深度プリパスの深度から、画素のまわりの半球に物がどれだけ詰まっているかを調べ、
// 物と物の隙間・壁の根元・棚の下など、周りの光が届きにくい所ほど暗くする値（1 = 遮る物なし、0 = 完全に囲まれている）を書いている。
// 同じ位置と法線を使って、天井の照明の接触影（下の ComputeContactShadow）も求め、2つ目のチャンネルに書いている。
// 主な技術: 深度からの位置の復元、深度の差から求める法線、法線の向きの半球に散らした点の判定、4x4の模様で回す標本の向き
//           （回した模様は ambientOcclusionBlurCS で深度を見ながらぼかして消している）
// ============================================================================

#include "renderingConfig.hlsli"
#include "computeDispatchConfig.hlsli"

// AmbientOcclusion.cpp の Parameters と同じ並び（b0）
cbuffer AmbientOcclusionBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_AO_PARAMS_SLOT))
{
    float4x4 Projection;        // 視点の空間 → 画面
    float4x4 InverseProjection; // 画面 → 視点の空間
    float2 ViewportSize;
    float2 ViewportOffset;
    float Radius;               // 調べる半球の半径（ワールドの長さ）
    float Intensity;            // 暗くする強さ
    float Bias;                 // 自分の面で暗くならないための、深さのずれ
    float MaxDistance;          // これより遠い所は計算しない（遠くは霧に沈むため）
    float2 OcclusionSize;       // 環境遮蔽の画像の大きさ（描く範囲の半分。軽くするため、縦横半分の解像度で計算している）
    float2 OcclusionPadding;
    float4x4 View;              // ワールド → 視点の空間（点光源の位置を視点の空間へ移すため）
    uint LightCount;            // 点光源の数（t1）
    uint3 ContactPadding;
};

// 点光源（TiledLighting の一覧と同じ物。common.hlsl の ENVIRONMENT_POINT_LIGHT と同じ並び）
struct POINT_LIGHT
{
    float4 PositionRange;   // xyz = 位置、w = 影響の半径
    float4 ColorIntensity;  // rgb = 色、a = 強さ
};

Texture2D<float> SceneDepth : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_AO_DEPTH_SLOT));
StructuredBuffer<POINT_LIGHT> PointLights : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_AO_SECOND_INPUT_SLOT));
// x = 環境遮蔽、y = 接触影（どちらも1 = 遮る物なし）
RWTexture2D<float2> OcclusionOutput : register(SIGNAL_LOST_UAV_REGISTER(SIGNAL_LOST_AO_OUTPUT_SLOT));

// ---- 接触影（スクリーンスペース・コンタクトシャドウ） ----
// 天井の照明（点光源）には、揺れる裸電球の1つを除いて影の画像（シャドウマップ）がない。そのため棚・机・ロッカーの足元に影がなく、
// 物が床から浮いて見えていた（審査会の指摘「影が当たっていない部分がおかしく見える」）。
// 画素ごとに一番強く照らしている点光源を選び、その光源の方へ短い光線を深度に沿ってたどって、途中で物の裏に入ったら影にしている。
// 壁と床のシェーダーで点光源ごとに求めると、本描画が1〜2ms重くなったので、SSAOと一緒に縦横半分の解像度で求め、同じぼかしで粒を消している。
// 光線をたどる長さ（1単位は約5cm。約80cm）と、歩く回数
static const float CONTACT_SHADOW_LENGTH = 16.0f;
static const int CONTACT_SHADOW_STEPS = 8;
// 光源のまわりで調べない距離（卓上ライトの傘や天井の照明の箱を、さえぎる物と見ないため）
static const float CONTACT_SHADOW_LIGHT_CLEARANCE = 8.0f;
// 自分の面に当たらないよう、面の向きへ浮かせる距離と、奥に入ったとみなす最小の差（遠いほど深度が粗いので、奥行き1あたりの分を足す）
static const float CONTACT_SHADOW_NORMAL_OFFSET = 1.0f;
static const float CONTACT_SHADOW_BIAS = 0.4f;
static const float CONTACT_SHADOW_BIAS_PER_DEPTH = 0.012f;
// 奥に入った差がこれより大きいときは、ずっと奥の物の手前を通っただけとして影にしない（物の厚みの想定）
static const float CONTACT_SHADOW_THICKNESS = 6.0f;
// 影の濃さ（1で真っ暗）と、光線の先の方で当たるほど薄くする割合（物から離れるほど影が薄くなる）
static const float CONTACT_SHADOW_STRENGTH = 0.85f;
static const float CONTACT_SHADOW_FAR_HIT_FADE = 0.6f;
// 光が面をかすめる所（面と光の向きの内積がこの間より小さい所）では弱める（自分の影の点々が出やすいため）
static const float CONTACT_SHADOW_GRAZING_START = 0.10f;
static const float CONTACT_SHADOW_GRAZING_FULL = 0.35f;
// 光源の届き方（距離による弱まり）がこの間より弱い所では、影を薄くする（弱い光の影は見えない）
static const float CONTACT_SHADOW_MIN_ATTENUATION = 0.05f;
static const float CONTACT_SHADOW_FULL_ATTENUATION = 0.20f;
// カメラから遠い所では、画面の上の1歩が小さくなり影がちらつくので、この距離の間で消す
static const float CONTACT_SHADOW_FADE_START = 160.0f;
static const float CONTACT_SHADOW_FADE_END = 240.0f;
// 輝度の重み（一番強く照らしている光源を選ぶとき、色の明るさに使う）
static const float3 CONTACT_LUMINANCE_WEIGHTS = float3(0.2126f, 0.7152f, 0.0722f);
// インターリーブド・グラディエント・ノイズの係数（Jimenez 2014）。画素ごとに歩く位置をずらし、段の縞を粒にしてぼかしで消す
static const float3 INTERLEAVED_GRADIENT_NOISE = float3(0.06711056f, 0.00583715f, 52.9829189f);

// 法線の向きの半球に散らした点の数と、その点（長さ0.15〜1。内側ほど多く置いている）
#define OCCLUSION_KERNEL_SIZE 12
static const float3 g_Kernel[OCCLUSION_KERNEL_SIZE] =
{
    float3(0.538f, 0.183f, 0.259f), float3(-0.403f, 0.452f, 0.150f), float3(0.123f, -0.552f, 0.328f),
    float3(-0.287f, -0.210f, 0.121f), float3(0.704f, -0.257f, 0.422f), float3(-0.639f, 0.112f, 0.563f),
    float3(0.231f, 0.701f, 0.497f), float3(-0.092f, -0.833f, 0.252f), float3(0.312f, 0.034f, 0.881f),
    float3(-0.751f, -0.432f, 0.398f), float3(0.081f, 0.142f, 0.173f), float3(0.611f, 0.598f, 0.122f)
};

// 画素の位置（描く範囲の左上が0）から、視点の空間の位置を復元している
float3 ViewPositionAt(int2 pixel)
{
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float2 uv = (float2(pixel) + 0.5f) / ViewportSize;
    const float4 position = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), InverseProjection);
    return position.xyz / position.w;
}

// 0〜1の乱数（4x4の模様にして、隣り合う画素で標本の向きを変えている）
// 4×4画素で標本の回転を変え、縞が目立たないようにする。
static const int OCCLUSION_ROTATION_TILE_MASK = 3;
static const int OCCLUSION_ROTATION_TILE_WIDTH = 4;
static const float OCCLUSION_ROTATION_COUNT = 16.0f;
static const float OCCLUSION_ROTATION_FULL_TURN = 6.2831853f;

// 除算の下限と、遠方で効果を薄め始める距離の割合。
static const float OCCLUSION_RANGE_MIN_DEPTH_DELTA = 0.001f;
// カメラの後ろとみなす w の下限と、光源の範囲・距離で0で割らないための下限
#define OCCLUSION_MIN_CLIP_W 0.001f
#define OCCLUSION_MIN_DIVISOR 0.001f
static const float OCCLUSION_DISTANCE_FADE_START_FRACTION = 0.6f;

float InterleavedAngle(int2 pixel)
{
    const int2 cell = pixel & OCCLUSION_ROTATION_TILE_MASK;
    return (float(cell.x * OCCLUSION_ROTATION_TILE_WIDTH + cell.y) + 0.5f) / OCCLUSION_ROTATION_COUNT * OCCLUSION_ROTATION_FULL_TURN;
}

// 視点の空間の点を、描く範囲の画素の位置へ投影している（カメラの後ろなら false）
bool ProjectToPixel(float3 viewPosition, out int2 pixel)
{
    const float4 clip = mul(float4(viewPosition, 1.0f), Projection);
    pixel = int2(0, 0);
    if (clip.w <= OCCLUSION_MIN_CLIP_W)
    {
        return false;
    }
    const float2 ndc = clip.xy / clip.w;
    pixel = int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ViewportSize);
    return all(pixel >= 0) && all(pixel < int2(ViewportSize));
}

// 画素（center は視点の空間の位置、normal は面の向き）を一番強く照らしている点光源へ向けた接触影（1 = 影なし）
float ComputeContactShadow(float3 center, float3 normal, int2 outputPixel)
{
    const float cameraFade = 1.0f - smoothstep(CONTACT_SHADOW_FADE_START, CONTACT_SHADOW_FADE_END, center.z);
    if (cameraFade <= 0.0f)
    {
        return 1.0f;
    }

    // 一番強く照らしている点光源（距離による弱まり × 強さ × 色の明るさ）を選んでいる
    float bestWeight = 0.0f;
    float bestAttenuation = 0.0f;
    float3 bestLight = 0.0f;
    [loop]
    for (uint index = 0; index < LightCount; ++index)
    {
        const POINT_LIGHT light = PointLights[index];
        const float3 lightView = mul(float4(light.PositionRange.xyz, 1.0f), View).xyz;
        const float range = max(light.PositionRange.w, OCCLUSION_MIN_DIVISOR);
        const float lightDistance = length(lightView - center);
        if (lightDistance >= range)
        {
            continue;
        }
        float attenuation = 1.0f - lightDistance / range;
        attenuation *= attenuation;
        const float weight = attenuation * light.ColorIntensity.a * dot(light.ColorIntensity.rgb, CONTACT_LUMINANCE_WEIGHTS);
        if (weight > bestWeight)
        {
            bestWeight = weight;
            bestAttenuation = attenuation;
            bestLight = lightView;
        }
    }
    const float lightFade = smoothstep(CONTACT_SHADOW_MIN_ATTENUATION, CONTACT_SHADOW_FULL_ATTENUATION, bestAttenuation);
    if (lightFade <= 0.0f)
    {
        return 1.0f;
    }

    const float3 toLight = bestLight - center;
    const float distanceToLight = length(toLight);
    const float rayLength = min(CONTACT_SHADOW_LENGTH, distanceToLight - CONTACT_SHADOW_LIGHT_CLEARANCE);
    const float3 direction = toLight / max(distanceToLight, OCCLUSION_MIN_DIVISOR);
    const float grazingFade = smoothstep(CONTACT_SHADOW_GRAZING_START, CONTACT_SHADOW_GRAZING_FULL, dot(normal, direction));
    if (rayLength <= 0.0f || grazingFade <= 0.0f)
    {
        return 1.0f;
    }

    const float3 start = center + normal * CONTACT_SHADOW_NORMAL_OFFSET;
    const float jitter = frac(INTERLEAVED_GRADIENT_NOISE.z *
        frac(dot(float2(outputPixel), INTERLEAVED_GRADIENT_NOISE.xy)));
    [loop]
    for (int step = 1; step <= CONTACT_SHADOW_STEPS; ++step)
    {
        const float travel = (float(step) - jitter) / float(CONTACT_SHADOW_STEPS);
        const float3 rayPosition = start + direction * (travel * rayLength);
        int2 samplePixel;
        if (!ProjectToPixel(rayPosition, samplePixel))
        {
            break;
        }
        // 光線が画面の面より奥（物の裏側）に入り、その差が物の厚みとみなせる範囲なら、さえぎられている
        const float behind = rayPosition.z - ViewPositionAt(samplePixel).z;
        if (behind > CONTACT_SHADOW_BIAS + rayPosition.z * CONTACT_SHADOW_BIAS_PER_DEPTH && behind < CONTACT_SHADOW_THICKNESS)
        {
            const float strength = CONTACT_SHADOW_STRENGTH * (1.0f - travel * CONTACT_SHADOW_FAR_HIT_FADE);
            return 1.0f - strength * cameraFade * lightFade * grazingFade;
        }
    }
    return 1.0f;
}

[numthreads(SIGNAL_LOST_OCCLUSION_GROUP_SIZE, SIGNAL_LOST_OCCLUSION_GROUP_SIZE, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // 半分の解像度の1画素が、描く範囲の2x2画素の左上を受け持っている
    const int2 outputPixel = int2(id.xy);
    if (any(outputPixel >= int2(OcclusionSize)))
    {
        return;
    }
    const int2 pixel = min(outputPixel * SIGNAL_LOST_OCCLUSION_DOWNSAMPLE_FACTOR, int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float3 center = ViewPositionAt(pixel);
    // 何も描いていない所と、遠くて霧に沈む所は、遮る物なしにしている
    if (depth >= 1.0f || center.z > MaxDistance)
    {
        OcclusionOutput[outputPixel] = float2(1.0f, 1.0f);
        return;
    }

    // 法線：左右・上下の隣の画素のうち、深さの差が小さい方を使い、物の縁で法線が崩れないようにしている
    const float3 right = ViewPositionAt(pixel + int2(1, 0)) - center;
    const float3 left = center - ViewPositionAt(pixel - int2(1, 0));
    const float3 down = ViewPositionAt(pixel + int2(0, 1)) - center;
    const float3 up = center - ViewPositionAt(pixel - int2(0, 1));
    const float3 horizontal = abs(right.z) < abs(left.z) ? right : left;
    const float3 vertical = abs(down.z) < abs(up.z) ? down : up;
    float3 normal = normalize(cross(vertical, horizontal));
    // 法線はカメラの方（視点の空間の-z）を向けている
    if (dot(normal, center) > 0.0f)
    {
        normal = -normal;
    }

    // 法線を軸にした座標の向き（4x4の模様で回して、少ない標本でも偏らないようにしている）
    const float angle = InterleavedAngle(outputPixel);
    const float3 randomVector = float3(cos(angle), sin(angle), 0.0f);
    const float3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
    const float3 bitangent = cross(normal, tangent);

    float occlusion = 0.0f;
    [unroll]
    for (int index = 0; index < OCCLUSION_KERNEL_SIZE; ++index)
    {
        const float3 offset = g_Kernel[index];
        const float3 samplePosition = center + (tangent * offset.x + bitangent * offset.y + normal * offset.z) * Radius;
        // 標本の点を画面へ投影し、その画素に描かれている物の深さと比べている
        const float4 clip = mul(float4(samplePosition, 1.0f), Projection);
        const float2 ndc = clip.xy / clip.w;
        const int2 samplePixel = int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ViewportSize);
        if (any(samplePixel < 0) || any(samplePixel >= int2(ViewportSize)))
        {
            continue;
        }
        const float sceneZ = ViewPositionAt(samplePixel).z;
        // 標本の点より手前に物があれば遮られている。ただし、半径より大きく手前の物（離れた前景）は数えない
        const float rangeCheck = smoothstep(0.0f, 1.0f, Radius / max(abs(center.z - sceneZ), OCCLUSION_RANGE_MIN_DEPTH_DELTA));
        occlusion += (sceneZ < samplePosition.z - Bias ? 1.0f : 0.0f) * rangeCheck;
    }

    const float distanceFade = 1.0f - smoothstep(MaxDistance * OCCLUSION_DISTANCE_FADE_START_FRACTION, MaxDistance, center.z);
    OcclusionOutput[outputPixel] = float2(
        saturate(1.0f - occlusion / float(OCCLUSION_KERNEL_SIZE) * Intensity * distanceFade),
        ComputeContactShadow(center, normal, outputPixel));
}
