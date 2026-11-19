// ============================================================================
// シェーダーの役割: 外の森の草の葉先を、風とプレイヤーに押される力に向かうばねで動かしている（葉1本 = 1スレッド。ForestGrass.cpp）。
// 主な技術: 流れるノイズの突風と細かいそよぎ、ばね（目標の葉先への引き戻し）と減衰、葉の長さを保つ補正
//
// ・何もないときの葉先は、葉ごとの傾き（Lean）の向き。そこへ風の向きのずれと、プレイヤーから離れる向きのずれを足した所を目標にしている。
// ・葉先は目標へばねで引かれ、速さに比例して止まる。プレイヤーが通り過ぎると、少し行き過ぎて揺れながら戻る。
// ・最後に葉先を葉の長さの球の上へ戻し、地面にめり込まないよう高さの下限を付けている（葉が伸び縮みして見えないように）。
// ============================================================================

#include "renderingConfig.hlsli"
#include "fastNoise.hlsli"
#include "computeDispatchConfig.hlsli"

// ForestGrass.h の Blade と同じ並び（48バイト）
struct GrassBlade
{
    float3 Root;
    float Height;
    float2 Facing;
    float Width;
    float Lean;
    float3 Color;
    float Stiffness;
};

// ForestGrass.h の BladeState と同じ並び（32バイト）
struct GrassState
{
    float4 Tip;
    float4 Velocity;
};

// ForestGrass.cpp の SimulationParams と同じ並び（b0）
cbuffer GrassSimulationBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_GRASS_SIM_PARAMS_SLOT))
{
    float Time;
    float DeltaTime;
    uint BladeCount;
    float WindStrength;
    float2 WindDirection;
    float2 PlayerPosition;
    float PlayerRadius;
    float PlayerStrength;
    float2 SimulationPadding;
    float2 UnseenPosition;  // 草むらを通る「見えない何か」の足元（ForestEvents）
    float UnseenRadius;
    float UnseenStrength;   // 0 なら通っていない
}

StructuredBuffer<GrassBlade> g_Blades : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_GRASS_SIM_INPUT_SLOT));
RWStructuredBuffer<GrassState> g_States : register(SIGNAL_LOST_UAV_REGISTER(SIGNAL_LOST_GRASS_SIM_OUTPUT_SLOT));

// 場所ごとに位相をずらし、草むらを渡る突風と葉ごとの細かい動きを重ねる。
static const float GRASS_GUST_SPATIAL_SCALE = 0.011f;
static const float GRASS_GUST_FLOW_SPEED = 0.22f;
static const float GRASS_GUST_COARSE_WEIGHT = 0.7f;
static const float GRASS_GUST_FINE_WEIGHT = 0.3f;
static const float GRASS_GUST_FINE_SCALE = 2.7f;
static const float GRASS_GUST_FINE_SEED = 9.0f;
static const float GRASS_FLUTTER_BASE_RATE = 2.3f;
static const float GRASS_FLUTTER_STIFFNESS_RATE = 0.05f;
static const float2 GRASS_FLUTTER_PHASE_WEIGHTS = float2(0.71f, 0.53f);
static const float GRASS_WIND_BASE_GAIN = 0.15f;
static const float GRASS_WIND_GUST_GAIN = 1.1f;
static const float GRASS_FLUTTER_AMPLITUDE = 0.05f;

// プレイヤーと見えない影は、草を外へ押す強さと葉の低さが異なる。
static const float GRASS_PLAYER_PUSH_GAIN = 2.2f;
static const float GRASS_PLAYER_HEIGHT_REDUCTION = 0.7f;
static const float GRASS_UNSEEN_FLATTEN_GAIN = 3.4f;
static const float GRASS_UNSEEN_HEIGHT_REDUCTION = 0.92f;

// 倒れた目標の葉先を地面にめり込ませない。grassVS の視点補正でも同じ下限を使う。
static const float GRASS_MIN_TARGET_UP_COMPONENT = 0.12f;

// 影が通り過ぎたあとも倒れ具合を残し、毎秒一定の割合で元に戻す。
static const float GRASS_FLATTEN_RECOVERY_PER_SECOND = 0.2f;
static const float GRASS_UNSEEN_FOOTPRINT_CENTER_WEIGHT = 1.3f;

// 葉の硬さに応じて減衰を増やし、物理計算後にも地面へのめり込みを抑える。
static const float GRASS_DAMPING_BASE = 3.2f;
static const float GRASS_DAMPING_STIFFNESS_GAIN = 0.12f;
static const float GRASS_MIN_UPDATED_UP_COMPONENT = 0.10f;

// 何もないときの葉先と、風とプレイヤーの力を足した、この瞬間の目標の葉先（根元からのずれ）を返している
// 0で割らないための下限、atan2 の 0/0 と、葉の向きの長さ0を避けるわずかなずれ、1フレームを分ける歩数とその1歩の割合
#define GRASS_MIN_DIVISOR 0.001f
#define GRASS_DIRECTION_NUDGE 0.0001f
#define GRASS_SUBSTEP_COUNT 2
#define GRASS_SUBSTEP_FRACTION 0.5f

float3 GetTargetTip(GrassBlade blade, float flatten, float flattenAngle)
{
    const float3 leanDirection = float3(-blade.Facing.y, 0.0f, blade.Facing.x);
    float3 target = normalize(float3(0.0f, 1.0f, 0.0f) + leanDirection * blade.Lean);

    // 風：大きなノイズを風の向きへ流して、草むらを渡っていく突風の波にしている。葉ごとの細かいそよぎも重ねている
    const float2 windSample = blade.Root.xz * GRASS_GUST_SPATIAL_SCALE - WindDirection * Time * GRASS_GUST_FLOW_SPEED;
    const float gust = FastValueNoise(windSample) * GRASS_GUST_COARSE_WEIGHT + FastValueNoise(windSample * GRASS_GUST_FINE_SCALE + GRASS_GUST_FINE_SEED) * GRASS_GUST_FINE_WEIGHT;
    const float flutter = sin(Time * (GRASS_FLUTTER_BASE_RATE + blade.Stiffness * GRASS_FLUTTER_STIFFNESS_RATE) + dot(blade.Root.xz, GRASS_FLUTTER_PHASE_WEIGHTS));
    const float windAmount = WindStrength * (GRASS_WIND_BASE_GAIN + gust * gust * GRASS_WIND_GUST_GAIN) + flutter * GRASS_FLUTTER_AMPLITUDE;
    target += float3(WindDirection.x, 0.0f, WindDirection.y) * windAmount;

    // プレイヤー：足元のまわりの葉を、外へ押し倒している（近いほど強く、真ん中の葉はほぼ寝る）
    const float2 away = blade.Root.xz - PlayerPosition;
    const float distanceToPlayer = length(away);
    const float push = PlayerStrength * saturate(1.0f - distanceToPlayer / max(PlayerRadius, GRASS_MIN_DIVISOR));
    if (push > 0.0f)
    {
        const float2 pushDirection = away / max(distanceToPlayer, GRASS_MIN_DIVISOR);
        target += float3(pushDirection.x, 0.0f, pushDirection.y) * push * GRASS_PLAYER_PUSH_GAIN;
        target.y *= 1.0f - push * GRASS_PLAYER_HEIGHT_REDUCTION;
    }

    // 「見えない何か」に踏まれた葉：踏まれた向きへ倒れたまま、ゆっくり起き上がる（倒れ具合 flatten は葉ごとに覚えている）
    if (flatten > 0.0f)
    {
        target += float3(cos(flattenAngle), 0.0f, sin(flattenAngle)) * flatten * GRASS_UNSEEN_FLATTEN_GAIN;
        target.y *= 1.0f - flatten * GRASS_UNSEEN_HEIGHT_REDUCTION;
    }

    // 長さをそろえ、寝ても地面より少し上に葉先が残るようにしている
    target = normalize(target);
    target.y = max(target.y, GRASS_MIN_TARGET_UP_COMPONENT);
    return normalize(target) * blade.Height;
}

[numthreads(SIGNAL_LOST_GRASS_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= BladeCount)
    {
        return;
    }
    const GrassBlade blade = g_Blades[index];
    GrassState state = g_States[index];
    // 「見えない何か」に踏まれた倒れ具合（Tip.w。0〜1）と、倒れる向き（Velocity.w。xz の角度）。
    // 踏まれている間は 1 にし、離れたら約5秒で 0 へ戻している（通った跡が草むらに残り、ゆっくり消える）
    float flatten = max(state.Tip.w - DeltaTime * GRASS_FLATTEN_RECOVERY_PER_SECOND, 0.0f);
    float flattenAngle = state.Velocity.w;
    if (UnseenStrength > 0.0f)
    {
        const float2 fromWalker = blade.Root.xz - UnseenPosition;
        const float walkerDistance = length(fromWalker);
        const float stepOn = UnseenStrength * saturate(GRASS_UNSEEN_FOOTPRINT_CENTER_WEIGHT - walkerDistance / max(UnseenRadius, GRASS_MIN_DIVISOR));
        if (stepOn > flatten)
        {
            flatten = saturate(stepOn);
            flattenAngle = atan2(fromWalker.y, fromWalker.x + GRASS_DIRECTION_NUDGE);
        }
    }
    const float3 target = GetTargetTip(blade, flatten, flattenAngle);

    // 1フレームの時間を2歩に分け、硬い葉でも計算が暴れないようにしている（速さを先に更新する、半陰的オイラー法）
    const float stepSeconds = DeltaTime * GRASS_SUBSTEP_FRACTION;
    float3 tip = state.Tip.xyz;
    float3 velocity = state.Velocity.xyz;
    const float damping = GRASS_DAMPING_BASE + blade.Stiffness * GRASS_DAMPING_STIFFNESS_GAIN;
    [unroll]
    for (int step = 0; step < GRASS_SUBSTEP_COUNT; ++step)
    {
        const float3 acceleration = (target - tip) * blade.Stiffness - velocity * damping;
        velocity += acceleration * stepSeconds;
        tip += velocity * stepSeconds;
    }

    // 葉の長さを保ち、地面にめり込ませない
    tip = normalize(tip + float3(0.0f, GRASS_DIRECTION_NUDGE, 0.0f));
    tip.y = max(tip.y, GRASS_MIN_UPDATED_UP_COMPONENT);
    tip = normalize(tip) * blade.Height;

    state.Tip = float4(tip, flatten);
    state.Velocity = float4(velocity, flattenAngle);
    g_States[index] = state;
}
