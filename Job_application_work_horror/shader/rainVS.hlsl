// 雨粒の筋と、地面で跳ねるしぶきを、頂点バッファを使わず番号（SV_VertexID・SV_InstanceID）から作っている。
// ・雨粒：番号から決めた乱数の位置を、落ちる速さ×時間だけずらし、カメラを囲む箱の中で折り返している（frac）。
//   ワールドに対して位置が決まっているので、カメラが動いても雨粒は一緒に動かない（箱の端で反対側へ回り込むだけ）。
//   筋は落ちる向きを軸にした細い四角形で、カメラの方を向けている。遠くの筋が1画素より細くなってちらつかないよう、
//   幅を1画素ぶんまで広げ、その分だけ薄くしている。
// ・しぶき：地面の上に寝かせた四角形。0.45秒ごとに場所を変え、ピクセルシェーダーで広がる輪にしている。
// ・屋根やひさしの下（Occluders）と、地面より下の雨粒は描かない。
// ・雨で輪郭が浮かぶ人影（FigureStart より後ろの番号）：人の形の表面を伝う水の筋と、頭と肩のしぶき。その真下には雨を降らせない。
#include "common.hlsl"
#include "renderingConfig.hlsli"

cbuffer ShadowBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_FLASHLIGHT_SHADOW_BUFFER_SLOT))
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}
// ForestRain.cpp の RainMaxOccluders と RoofHeightsPerVector に対応する。
#define RAIN_MAX_OCCLUDERS 8
#define RAIN_ROOF_HEIGHTS_PER_VECTOR 4u
// ForestRain.cpp の RainDrawParams と同じ並び（b9）
cbuffer RainDrawBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT))
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
    uint FigureStart;        // これより後ろの番号は、雨で輪郭が浮かぶ人影の水（ForestRainFigure）
    float FigureVisibility;  // 人影の見え方（0〜1。0 なら人影の水を描かず、真下の雨も止めない）
    float4 Occluders[RAIN_MAX_OCCLUDERS];     // 屋根を上から見た長方形（xy = 最小のx・z、zw = 最大のx・z）
    float4 OccluderBottoms[RAIN_MAX_OCCLUDERS / RAIN_ROOF_HEIGHTS_PER_VECTOR]; // 屋根の下の面の高さ（8個を4つずつ詰めている）
    float4 FigureFoot;       // 人影の足元（xyz）と、崩れ始めてからの秒数（w。0 なら崩れていない）
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

// PCG の係数（O'Neill 2014 の RXS-M-XS。決まった値）：状態を進める掛け算と足し算、ずらす量を選ぶ上位ビット、最小のずらし、出力の掛け算と最後のずらし
#define PCG_MULTIPLIER 747796405u
#define PCG_INCREMENT 2891336453u
#define PCG_SHIFT_SELECT 28u
#define PCG_SHIFT_BASE 4u
#define PCG_OUTPUT_MULTIPLIER 277803737u
#define PCG_OUTPUT_SHIFT 22u

// 整数から、3つの 0〜1 の乱数を作っている（PCG の混ぜ方）
uint Pcg(uint value)
{
    const uint state = value * PCG_MULTIPLIER + PCG_INCREMENT;
    const uint word = ((state >> ((state >> PCG_SHIFT_SELECT) + PCG_SHIFT_BASE)) ^ state) * PCG_OUTPUT_MULTIPLIER;
    return (word >> PCG_OUTPUT_SHIFT) ^ word;
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
        const float bottom = OccluderBottoms[index / RAIN_ROOF_HEIGHTS_PER_VECTOR][index % RAIN_ROOF_HEIGHTS_PER_VECTOR];
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

// ---- 雨で輪郭が浮かぶ人影（ForestRainFigure） ----
// 姿は描かず、人の形の表面を伝って流れ落ちる水の筋と、頭と肩で跳ねるしぶきだけで、そこに立つ人の輪郭を見せている。
// 形は足元からの高さごとの半径（脚 → 腰 → 胸 → 肩 → 首 → 頭の球）で表した回転体の胴に、左右の脚と、体の横に垂らした両腕を足したもの
// （背は人影のモデルと同じ約55）。脚と腕は、目から見て左右に並ぶ向きに置き、どこから見ても人の形に見えるようにしている。
// 脚の半径と上端、腰の半径と上端、胸の始まり、肩の高さと半径、首の高さ・半径・上端、頭の中心の高さと半径、頭の上端
#define FIGURE_LEG_RADIUS 2.6f
#define FIGURE_LEG_TOP 22.0f
#define FIGURE_HIP_RADIUS 5.0f
#define FIGURE_HIP_TOP 27.0f
#define FIGURE_CHEST_START 30.0f
#define FIGURE_SHOULDER_HEIGHT 43.0f
#define FIGURE_SHOULDER_RADIUS 6.0f
#define FIGURE_NECK_HEIGHT 46.0f
#define FIGURE_NECK_RADIUS 2.3f
#define FIGURE_NECK_TOP 48.0f
#define FIGURE_HEAD_CENTER 51.5f
#define FIGURE_HEAD_RADIUS 4.0f
#define FIGURE_TOP 55.5f
// 左右の脚の中心の間隔の半分（腰へ向かって胴の中心へ寄せる高さの幅）、腕の中心の胴の中心からの距離・半径・下端・上端、腕を伝う水の割合
#define FIGURE_LEG_SPACING 3.0f
#define FIGURE_LEG_MERGE 4.0f
#define FIGURE_ARM_OFFSET 7.6f
#define FIGURE_ARM_RADIUS 1.7f
#define FIGURE_ARM_BOTTOM 17.0f
#define FIGURE_ARM_TOP 43.0f
#define FIGURE_ARM_SHARE 0.28f
#define FIGURE_ARM_SEED_SCALE 7.3f
// 粒の乱数の種（素数と2つのずらし）、一周の角度、頭と肩で跳ねるしぶきにする粒の割合
#define FIGURE_SEED_PRIME 6151u
#define FIGURE_SEED_OFFSET 29u
#define FIGURE_SECOND_SEED_OFFSET 4111u
#define FIGURE_TWO_PI 6.2831853f
#define FIGURE_SPLASH_SHARE 0.22f
// 表面を伝う水：流れ落ちる速さ（基準・ばらつきの最低と幅）、表面から浮かせる距離、傾きを調べる高さの差、筋の長さ、幅の倍率、濃さ
#define FIGURE_SLIDE_SPEED 30.0f
#define FIGURE_SLIDE_SPEED_BASE 0.7f
#define FIGURE_SLIDE_SPEED_RANGE 0.6f
#define FIGURE_SURFACE_OFFSET 0.2f
#define FIGURE_SLOPE_STEP 1.0f
#define FIGURE_STREAK_LENGTH 1.8f
#define FIGURE_STREAK_WIDTH_SCALE 1.4f
#define FIGURE_DROP_ALPHA 0.6f
// 頭と肩のしぶき：1回の長さ（秒）、跳ねるたびに場所を回す角度、表面から浮かせる高さ、大きさ（最低＋幅）
#define FIGURE_SPLASH_PERIOD 0.35f
#define FIGURE_SPLASH_ANGLE_STEP 2.39996f
#define FIGURE_SPLASH_LIFT 0.1f
#define FIGURE_SPLASH_RADIUS_BASE 0.3f
#define FIGURE_SPLASH_RADIUS_RANGE 0.4f
// 崩れるとき：水が落ちる重さ（1メートル ≒ 24 なので、重力 9.8m/s² ≒ 240）と、外へ散る速さ（最低＋乱数の幅）
#define FIGURE_COLLAPSE_GRAVITY 240.0f
#define FIGURE_COLLAPSE_SPREAD 12.0f
#define FIGURE_COLLAPSE_SPREAD_BASE 0.5f

// 足元からの高さ height での、人の形の半径（形の外なら0）
float GetFigureRadius(float height)
{
    if (height < 0.0f || height > FIGURE_TOP)
    {
        return 0.0f;
    }
    const float headOffset = height - FIGURE_HEAD_CENTER;
    const float head = sqrt(max(FIGURE_HEAD_RADIUS * FIGURE_HEAD_RADIUS - headOffset * headOffset, 0.0f));
    float body = FIGURE_LEG_RADIUS;
    body = lerp(body, FIGURE_HIP_RADIUS, smoothstep(FIGURE_LEG_TOP, FIGURE_HIP_TOP, height));
    body = lerp(body, FIGURE_SHOULDER_RADIUS, smoothstep(FIGURE_CHEST_START, FIGURE_SHOULDER_HEIGHT, height));
    body = lerp(body, FIGURE_NECK_RADIUS, smoothstep(FIGURE_SHOULDER_HEIGHT, FIGURE_NECK_HEIGHT, height));
    body = height > FIGURE_NECK_TOP ? 0.0f : body;
    return max(head, body);
}

// 足元の軸からの横の距離 radial で、上から落ちる雨が当たる高さ（頭か肩。当たらなければ負）
float GetFigureTop(float radial)
{
    if (radial < FIGURE_HEAD_RADIUS)
    {
        return FIGURE_HEAD_CENTER + sqrt(FIGURE_HEAD_RADIUS * FIGURE_HEAD_RADIUS - radial * radial);
    }
    return radial < FIGURE_SHOULDER_RADIUS ? FIGURE_SHOULDER_HEIGHT : -1.0f;
}

// 頭と肩で跳ねるしぶき1つを作っている。跳ねるたびに、当たる場所を回している（angle = 粒ごとの向き、fall = 崩れるときのずれ）
RAIN_PS_IN MakeFigureSplash(float3 random, float3 random2, float angle, float3 fall, float alpha, float side, float along)
{
    const float radial = sqrt(random.z) * FIGURE_SHOULDER_RADIUS;
    const float cycleTime = Time / FIGURE_SPLASH_PERIOD + random2.x;
    const float life = frac(cycleTime);
    const float splashAngle = angle + floor(cycleTime) * FIGURE_SPLASH_ANGLE_STEP;
    const float3 center = FigureFoot.xyz + float3(cos(splashAngle), 0.0f, sin(splashAngle)) * radial +
        float3(0.0f, GetFigureTop(radial) + FIGURE_SPLASH_LIFT, 0.0f) + fall;
    const float splashAlpha = alpha * smoothstep(SPLASH_NEAR_FADE_START, SPLASH_NEAR_FADE_END, length(CameraPosition - center));
    const float radius = FIGURE_SPLASH_RADIUS_BASE + FIGURE_SPLASH_RADIUS_RANGE * random2.y;
    const float3 position = center + float3(side * radius, 0.0f, (along * 2.0f - 1.0f) * radius);
    RAIN_PS_IN splash = Finish(position, float2(side, along * 2.0f - 1.0f), splashAlpha, 1.0f, life);
    if (splashAlpha <= 0.0f)
    {
        splash.pos = float4(0.0f, 0.0f, -1.0f, 1.0f);
    }
    return splash;
}

// 人の形の表面を伝って流れ落ちる水の筋1本を作っている（頭の上から足元まで、粒ごとに速さと始まりをずらして繰り返す）
RAIN_PS_IN MakeFigureStreak(float3 random, float3 random2, float3 outward, float3 fall, float alpha, float side, float along)
{
    const float speed = FIGURE_SLIDE_SPEED * (FIGURE_SLIDE_SPEED_BASE + FIGURE_SLIDE_SPEED_RANGE * random.z);
    // 目から見て左右に並ぶ向き（脚と腕を置く向き）と、粒が左右のどちらにあるか
    float3 toCameraFlat = float3(CameraPosition.x - FigureFoot.x, 0.0f, CameraPosition.z - FigureFoot.z);
    toCameraFlat = dot(toCameraFlat, toCameraFlat) > RAIN_MIN_RIGHT_LENGTH_SQ ? normalize(toCameraFlat) : float3(0.0f, 0.0f, 1.0f);
    const float3 sideAxis = float3(toCameraFlat.z, 0.0f, -toCameraFlat.x) * (random2.y < 0.5f ? -1.0f : 1.0f);
    const bool arm = frac(random2.y * FIGURE_ARM_SEED_SCALE) < FIGURE_ARM_SHARE;
    float height;
    float radius;
    float lowerRadius;
    float3 center;
    if (arm)
    {
        // 体の横に垂らした腕：肩から手の先までを伝って落ちる
        height = FIGURE_ARM_TOP - (FIGURE_ARM_TOP - FIGURE_ARM_BOTTOM) * frac(random2.x + Time * speed / (FIGURE_ARM_TOP - FIGURE_ARM_BOTTOM));
        radius = FIGURE_ARM_RADIUS + FIGURE_SURFACE_OFFSET;
        lowerRadius = radius;
        center = sideAxis * FIGURE_ARM_OFFSET;
    }
    else
    {
        height = FIGURE_TOP * (1.0f - frac(random2.x + Time * speed / FIGURE_TOP));
        radius = GetFigureRadius(height) + FIGURE_SURFACE_OFFSET;
        lowerRadius = GetFigureRadius(max(height - FIGURE_SLOPE_STEP, 0.0f)) + FIGURE_SURFACE_OFFSET;
        // 脚は左右に分け、腰へ向かって胴の中心へ寄せている
        center = sideAxis * FIGURE_LEG_SPACING * (1.0f - smoothstep(FIGURE_LEG_TOP - FIGURE_LEG_MERGE, FIGURE_HIP_TOP, height));
    }
    const float3 head = FigureFoot.xyz + center + outward * radius + float3(0.0f, height, 0.0f) + fall;
    // 流れる向き：表面に沿って下へ（肩や頭では外へ回り込みながら）
    const float3 axis = normalize(outward * (lowerRadius - radius) - float3(0.0f, FIGURE_SLOPE_STEP, 0.0f));
    const float3 toCamera = CameraPosition - head;
    const float distanceToCamera = length(toCamera);
    alpha *= smoothstep(RAIN_NEAR_FADE_START, RAIN_NEAR_FADE_END, distanceToCamera);
    const float pixelWidth = distanceToCamera / max(PixelScale, 1.0f);
    const float baseWidth = StreakWidth * FIGURE_STREAK_WIDTH_SCALE;
    const float width = max(baseWidth, pixelWidth * RAIN_MIN_PIXEL_WIDTH_SCALE);
    alpha *= baseWidth / width;
    float3 right = cross(axis, toCamera / max(distanceToCamera, RAIN_MIN_DIVISOR));
    right = dot(right, right) > RAIN_MIN_RIGHT_LENGTH_SQ ? normalize(right) : float3(1.0f, 0.0f, 0.0f);
    const float3 position = head - axis * FIGURE_STREAK_LENGTH * (1.0f - along) + right * side * width * 0.5f;
    RAIN_PS_IN output = Finish(position, float2(side, along), alpha, 0.0f, 0.0f);
    if (alpha <= 0.0f)
    {
        output.pos = float4(0.0f, 0.0f, -1.0f, 1.0f);
    }
    return output;
}

// 人の形の表面を伝う水の筋1本、または頭と肩で跳ねるしぶき1つを作っている
RAIN_PS_IN MakeFigureDrop(uint index, float side, float along)
{
    const float3 random = Hash3(index * FIGURE_SEED_PRIME + FIGURE_SEED_OFFSET);
    const float3 random2 = Hash3(index * FIGURE_SEED_PRIME + FIGURE_SECOND_SEED_OFFSET);
    const float angle = random.x * FIGURE_TWO_PI;
    const float3 outward = float3(cos(angle), 0.0f, sin(angle));
    // 崩れるときは、水が表面を離れ、外へ散りながら落ちていく（FigureFoot.w = 崩れ始めてからの秒数）
    const float collapse = FigureFoot.w;
    const float3 fall = outward * collapse * FIGURE_COLLAPSE_SPREAD * (FIGURE_COLLAPSE_SPREAD_BASE + random2.z) -
        float3(0.0f, 0.5f * FIGURE_COLLAPSE_GRAVITY * collapse * collapse, 0.0f);
    const float alpha = FigureVisibility * FIGURE_DROP_ALPHA;

    RAIN_PS_IN output;
    if (random.y < FIGURE_SPLASH_SHARE)
    {
        output = MakeFigureSplash(random, random2, angle, fall, alpha, side, along);
    }
    else
    {
        output = MakeFigureStreak(random, random2, outward, fall, alpha, side, along);
    }
    return output;
}

RAIN_PS_IN main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const float side = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
    const float along = (vertexId & 2u) != 0u ? 1.0f : 0.0f;
    [branch]
    if (instanceId >= FigureStart)
    {
        return MakeFigureDrop(instanceId - FigureStart, side, along);
    }
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
        // 雨で輪郭が浮かぶ人影の真下（頭と肩の下）には、雨が届かない
        if (FigureVisibility > 0.0f && FigureFoot.w <= 0.0f)
        {
            const float3 fromFoot = head - FigureFoot.xyz;
            const float radial = length(fromFoot.xz);
            if (radial < FIGURE_SHOULDER_RADIUS && fromFoot.y > 0.0f && fromFoot.y < GetFigureTop(radial))
            {
                alpha *= 1.0f - FigureVisibility;
            }
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
