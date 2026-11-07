// ============================================================================
// シェーダーの役割: 外の森の草の葉先を、風とプレイヤーに押される力に向かうばねで動かしている（葉1本 = 1スレッド。ForestGrass.cpp）。
// 主な技術: 流れるノイズの突風と細かいそよぎ、ばね（目標の葉先への引き戻し）と減衰、葉の長さを保つ補正
//
// ・何もないときの葉先は、葉ごとの傾き（Lean）の向き。そこへ風の向きのずれと、プレイヤーから離れる向きのずれを足した所を目標にしている。
// ・葉先は目標へばねで引かれ、速さに比例して止まる。プレイヤーが通り過ぎると、少し行き過ぎて揺れながら戻る。
// ・最後に葉先を葉の長さの球の上へ戻し、地面にめり込まないよう高さの下限を付けている（葉が伸び縮みして見えないように）。
// ============================================================================

#include "fastNoise.hlsli"

#define GRASS_GROUP_SIZE 256

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
cbuffer GrassSimulationBuffer : register(b0)
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

StructuredBuffer<GrassBlade> g_Blades : register(t0);
RWStructuredBuffer<GrassState> g_States : register(u0);

// 何もないときの葉先と、風とプレイヤーの力を足した、この瞬間の目標の葉先（根元からのずれ）を返している
float3 GetTargetTip(GrassBlade blade, float flatten, float flattenAngle)
{
    const float3 leanDirection = float3(-blade.Facing.y, 0.0f, blade.Facing.x);
    float3 target = normalize(float3(0.0f, 1.0f, 0.0f) + leanDirection * blade.Lean);

    // 風：大きなノイズを風の向きへ流して、草むらを渡っていく突風の波にしている。葉ごとの細かいそよぎも重ねている
    const float2 windSample = blade.Root.xz * 0.011f - WindDirection * Time * 0.22f;
    const float gust = FastValueNoise(windSample) * 0.7f + FastValueNoise(windSample * 2.7f + 9.0f) * 0.3f;
    const float flutter = sin(Time * (2.3f + blade.Stiffness * 0.05f) + dot(blade.Root.xz, float2(0.71f, 0.53f)));
    const float windAmount = WindStrength * (0.15f + gust * gust * 1.1f) + flutter * 0.05f;
    target += float3(WindDirection.x, 0.0f, WindDirection.y) * windAmount;

    // プレイヤー：足元のまわりの葉を、外へ押し倒している（近いほど強く、真ん中の葉はほぼ寝る）
    const float2 away = blade.Root.xz - PlayerPosition;
    const float distanceToPlayer = length(away);
    const float push = PlayerStrength * saturate(1.0f - distanceToPlayer / max(PlayerRadius, 0.001f));
    if (push > 0.0f)
    {
        const float2 pushDirection = away / max(distanceToPlayer, 0.001f);
        target += float3(pushDirection.x, 0.0f, pushDirection.y) * push * 2.2f;
        target.y *= 1.0f - push * 0.7f;
    }

    // 「見えない何か」に踏まれた葉：踏まれた向きへ倒れたまま、ゆっくり起き上がる（倒れ具合 flatten は葉ごとに覚えている）
    if (flatten > 0.0f)
    {
        target += float3(cos(flattenAngle), 0.0f, sin(flattenAngle)) * flatten * 3.4f;
        target.y *= 1.0f - flatten * 0.92f;
    }

    // 長さをそろえ、寝ても地面より少し上に葉先が残るようにしている
    target = normalize(target);
    target.y = max(target.y, 0.12f);
    return normalize(target) * blade.Height;
}

[numthreads(GRASS_GROUP_SIZE, 1, 1)]
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
    float flatten = max(state.Tip.w - DeltaTime * 0.2f, 0.0f);
    float flattenAngle = state.Velocity.w;
    if (UnseenStrength > 0.0f)
    {
        const float2 fromWalker = blade.Root.xz - UnseenPosition;
        const float walkerDistance = length(fromWalker);
        const float stepOn = UnseenStrength * saturate(1.3f - walkerDistance / max(UnseenRadius, 0.001f));
        if (stepOn > flatten)
        {
            flatten = saturate(stepOn);
            flattenAngle = atan2(fromWalker.y, fromWalker.x + 0.0001f);
        }
    }
    const float3 target = GetTargetTip(blade, flatten, flattenAngle);

    // 1フレームの時間を2歩に分け、硬い葉でも計算が暴れないようにしている（速さを先に更新する、半陰的オイラー法）
    const float stepSeconds = DeltaTime * 0.5f;
    float3 tip = state.Tip.xyz;
    float3 velocity = state.Velocity.xyz;
    const float damping = 3.2f + blade.Stiffness * 0.12f;
    [unroll]
    for (int step = 0; step < 2; ++step)
    {
        const float3 acceleration = (target - tip) * blade.Stiffness - velocity * damping;
        velocity += acceleration * stepSeconds;
        tip += velocity * stepSeconds;
    }

    // 葉の長さを保ち、地面にめり込ませない
    tip = normalize(tip + float3(0.0f, 0.0001f, 0.0f));
    tip.y = max(tip.y, 0.10f);
    tip = normalize(tip) * blade.Height;

    state.Tip = float4(tip, flatten);
    state.Velocity = float4(velocity, flattenAngle);
    g_States[index] = state;
}
