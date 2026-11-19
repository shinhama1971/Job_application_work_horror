// 雨粒の筋と、地面で跳ねるしぶきを、頂点バッファを使わず番号（SV_VertexID・SV_InstanceID）から作っている。
// ・雨粒：番号から決めた乱数の位置を、落ちる速さ×時間だけずらし、カメラを囲む箱の中で折り返している（frac）。
//   ワールドに対して位置が決まっているので、カメラが動いても雨粒は一緒に動かない（箱の端で反対側へ回り込むだけ）。
//   筋は落ちる向きを軸にした細い四角形で、カメラの方を向けている。遠くの筋が1画素より細くなってちらつかないよう、
//   幅を1画素ぶんまで広げ、その分だけ薄くしている。
// ・しぶき：地面の上に寝かせた四角形。0.45秒ごとに場所を変え、ピクセルシェーダーで広がる輪にしている。
// ・屋根やひさしの下（Occluders）と、地面より下の雨粒は描かない。
#include "common.hlsl"

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}
// ForestRain.cpp の RainDrawParams と同じ並び（b9）
cbuffer RainDrawBuffer : register(b9)
{
    float3 CameraPosition;
    float Time;
    float3 BoxSize;          // カメラを囲む、雨粒を置く箱の大きさ
    float GroundY;           // 地面の高さ（これより下の雨粒は描かない）
    float2 Wind;             // 横に流される速さ（x・z）
    float FallSpeed;         // 落ちる速さ
    float StreakLength;      // 筋の長さ
    float StreakWidth;       // 筋の幅
    uint DropCount;          // 雨粒の数（これより後ろの番号はしぶき）
    float PixelScale;        // 距離1あたりの画素の数（= 射影行列の [1][1] × 画面の高さ ÷ 2）
    float Intensity;         // 雨の強さ（0〜1）
    float SplashRadius;      // しぶきを置く、カメラのまわりの範囲（半分の一辺）
    uint OccluderCount;      // 屋根の数
    float2 RainPadding;
    float4 Occluders[8];     // 屋根を上から見た長方形（xy = 最小のx・z、zw = 最大のx・z）
    float4 OccluderBottoms[2]; // 屋根の下の面の高さ（8個を4つずつ詰めている）
}

struct RAIN_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float4 shadowPos : TEXCOORD2;
    float2 uv : TEXCOORD3;       // 雨粒：x = 筋の横（-1〜1）、y = 筋の長さの向き（0 = 尾、1 = 先）。しぶき：輪の中の位置（-1〜1）
    float alpha : TEXCOORD4;     // 薄さ（遠い・近すぎる・細すぎる筋ほど小さい）
    float kind : TEXCOORD5;      // 0 = 雨粒、1 = しぶき
    float life : TEXCOORD6;      // しぶきの経過（0〜1）
};

// 整数から、3つの 0〜1 の乱数を作っている（PCG の混ぜ方）
uint Pcg(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
// 乱数の下16ビットを0〜1にするときの最大値
#define RAIN_HASH_MASK 0xFFFFu
#define RAIN_HASH_MAX 65535.0f
// 雨粒の筋：速さのばらつき（最低・幅、乱数を混ぜる係数）、見え始める距離（始め・終わり）、箱の端で消す範囲（箱の幅の割合）、
// 1画素より細くしないための広げ方、0で割らないための下限、横の向きを作れないとみなす長さの2乗、筋の長さ（最低・幅）
#define RAIN_SPEED_BASE 0.85f
#define RAIN_SPEED_RANGE 0.3f
#define RAIN_SPEED_SEED_X 7.13f
#define RAIN_SPEED_SEED_Z 3.7f
#define RAIN_NEAR_FADE_START 4.0f
#define RAIN_NEAR_FADE_END 16.0f
#define RAIN_BOX_EDGE_FADE_START 0.38f
#define RAIN_BOX_EDGE_FADE_END 0.5f
#define RAIN_MIN_PIXEL_WIDTH_SCALE 1.3f
#define RAIN_MIN_DIVISOR 0.001f
#define RAIN_MIN_RIGHT_LENGTH_SQ 0.000001f
#define RAIN_STREAK_LENGTH_BASE 0.7f
#define RAIN_STREAK_LENGTH_RANGE 0.6f
// 地面のしぶき：1回の長さ（秒）、乱数の種（素数）、地面から浮かせる高さ、見え始める距離（始め・終わり）、
// 範囲の端で消す割合、屋根を調べる高さ、大きさ（最低・幅）
#define SPLASH_PERIOD 0.45f
#define SPLASH_SEED_PRIME 7919u
#define SPLASH_SEED_OFFSET 17u
#define SPLASH_CYCLE_PRIME 104729u
#define SPLASH_PLACE_OFFSET 3u
#define SPLASH_GROUND_LIFT 0.12f
#define SPLASH_NEAR_FADE_START 3.0f
#define SPLASH_NEAR_FADE_END 8.0f
#define SPLASH_EDGE_FADE_START 0.7f
#define SPLASH_SHELTER_PROBE_HEIGHT 0.5f
#define SPLASH_RADIUS_BASE 0.35f
#define SPLASH_RADIUS_RANGE 0.9f

float3 Hash3(uint value)
{
    const uint a = Pcg(value);
    const uint b = Pcg(a);
    const uint c = Pcg(b);
    return float3(a & RAIN_HASH_MASK, b & RAIN_HASH_MASK, c & RAIN_HASH_MASK) / RAIN_HASH_MAX;
}

// 屋根やひさしの下か（雨が当たらない場所）
bool IsSheltered(float3 position)
{
    [loop]
    for (uint index = 0; index < OccluderCount; ++index)
    {
        const float4 rect = Occluders[index];
        const float bottom = OccluderBottoms[index / 4u][index % 4u];
        if (position.y < bottom && position.x > rect.x && position.x < rect.z &&
            position.z > rect.y && position.z < rect.w)
        {
            return true;
        }
    }
    return false;
}

RAIN_PS_IN Finish(float3 position, float2 uv, float alpha, float kind, float life)
{
    RAIN_PS_IN output;
    output.worldPos = position;
    output.viewPos = mul(float4(position, 1.0f), View).xyz;
    output.pos = mul(float4(output.viewPos, 1.0f), Projection);
    output.shadowPos = mul(float4(position, 1.0f), ShadowViewProjection);
    output.uv = uv;
    output.alpha = alpha;
    output.kind = kind;
    output.life = life;
    return output;
}

RAIN_PS_IN main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const float side = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
    const float along = (vertexId & 2u) != 0u ? 1.0f : 0.0f;
    if (instanceId < DropCount)
    {
        // ---- 雨粒の筋 ----
        const float3 random = Hash3(instanceId);
        const float speedScale = RAIN_SPEED_BASE + RAIN_SPEED_RANGE * frac(random.x * RAIN_SPEED_SEED_X + random.z * RAIN_SPEED_SEED_Z);
        const float3 velocity = float3(Wind.x, -FallSpeed * speedScale, Wind.y);
        // 箱の中の乱数の位置を、速さ×時間だけ動かし、カメラを中心にした箱の中へ折り返している
        const float3 moved = random * BoxSize + velocity * Time;
        const float3 relative = (frac((moved - CameraPosition) / BoxSize + 0.5f) - 0.5f) * BoxSize;
        const float3 head = CameraPosition + relative;
        const float3 axis = normalize(velocity);
        const float3 toCamera = CameraPosition - head;
        const float distanceToCamera = length(toCamera);
        // 地面より下、屋根の下、目のすぐ前（大きな板に見える）は描かない
        float alpha = Intensity * smoothstep(RAIN_NEAR_FADE_START, RAIN_NEAR_FADE_END, distanceToCamera) *
            (1.0f - smoothstep(BoxSize.x * RAIN_BOX_EDGE_FADE_START, BoxSize.x * RAIN_BOX_EDGE_FADE_END, length(relative.xz)));
        if (head.y < GroundY || IsSheltered(head))
        {
            alpha = 0.0f;
        }
        // 1画素より細くならないよう幅を広げ、広げた分だけ薄くしている（遠くの筋のちらつきを防ぐ）
        const float pixelWidth = distanceToCamera / max(PixelScale, 1.0f);
        const float width = max(StreakWidth, pixelWidth * RAIN_MIN_PIXEL_WIDTH_SCALE);
        alpha *= StreakWidth / width;
        // 筋の横の向き：落ちる向きと、カメラへの向きの両方に直角
        float3 right = cross(axis, toCamera / max(distanceToCamera, RAIN_MIN_DIVISOR));
        right = dot(right, right) > RAIN_MIN_RIGHT_LENGTH_SQ ? normalize(right) : float3(1.0f, 0.0f, 0.0f);
        const float streakLength = StreakLength * (RAIN_STREAK_LENGTH_BASE + RAIN_STREAK_LENGTH_RANGE * random.y);
        const float3 position = head - axis * streakLength * (1.0f - along) + right * side * width * 0.5f;
        RAIN_PS_IN output = Finish(position, float2(side, along), alpha, 0.0f, 0.0f);
        if (alpha <= 0.0f)
        {
            // 描かない雨粒は、面積のない三角形にして捨てている
            output.pos = float4(0.0f, 0.0f, -1.0f, 1.0f);
        }
        return output;
    }

    // ---- 地面のしぶき ----
    const uint splashIndex = instanceId - DropCount;
    const float period = SPLASH_PERIOD;
    const float3 seed = Hash3(splashIndex * SPLASH_SEED_PRIME + SPLASH_SEED_OFFSET);
    // しぶきごとに時間をずらし、1回ごとに違う場所に出している
    const float cycleTime = Time / period + seed.z;
    const uint cycle = (uint)floor(cycleTime);
    const float life = frac(cycleTime);
    const float3 place = Hash3(splashIndex * SPLASH_SEED_PRIME + cycle * SPLASH_CYCLE_PRIME + SPLASH_PLACE_OFFSET);
    const float2 area = float2(SplashRadius, SplashRadius) * 2.0f;
    const float2 relative = (frac((place.xy * area - CameraPosition.xz) / area + 0.5f) - 0.5f) * area;
    float3 center = float3(CameraPosition.x + relative.x, GroundY + SPLASH_GROUND_LIFT, CameraPosition.z + relative.y);
    const float distanceToCamera = length(CameraPosition - center);
    float alpha = Intensity * smoothstep(SPLASH_NEAR_FADE_START, SPLASH_NEAR_FADE_END, distanceToCamera) *
        (1.0f - smoothstep(SplashRadius * SPLASH_EDGE_FADE_START, SplashRadius, length(relative)));
    if (IsSheltered(center + float3(0.0f, SPLASH_SHELTER_PROBE_HEIGHT, 0.0f)))
    {
        alpha = 0.0f;
    }
    const float radius = SPLASH_RADIUS_BASE + SPLASH_RADIUS_RANGE * place.z;
    const float3 position = center + float3(side * radius, 0.0f, (along * 2.0f - 1.0f) * radius);
    RAIN_PS_IN output = Finish(position, float2(side, along * 2.0f - 1.0f), alpha, 1.0f, life);
    if (alpha <= 0.0f)
    {
        output.pos = float4(0.0f, 0.0f, -1.0f, 1.0f);
    }
    return output;
}
