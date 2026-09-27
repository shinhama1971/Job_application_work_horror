// ============================================================================
// GPUパーティクル（空気中を漂う埃）で共有する構造体・定数・乱数です。
// CPU側（GpuDustParticles.h）の構造体と並びを必ず一致させてください。
// ============================================================================
#ifndef DUST_PARTICLE_COMMON_INCLUDED
#define DUST_PARTICLE_COMMON_INCLUDED

struct DustParticle
{
    float3 Position;
    float Age;          // 生成からの経過秒
    float3 Velocity;
    float Lifetime;     // 寿命（秒）
    float Size;         // ビルボードの半径（ワールド単位）
    uint Seed;          // 粒子ごとの乱数状態
    float Phase;        // 揺らぎ・瞬きをずらすための値（0〜1）
    float Padding;
};

// Emit / Update / Args の3つのCompute Shaderで共通の定数バッファです。
cbuffer DustSimulationBuffer : register(b0)
{
    float3 CameraPosition;
    float DeltaTime;
    float3 VolumeExtent;        // カメラを囲む箱の半分の大きさ（この外に出た粒子は消します）
    float SimulationTime;
    float3 CameraVelocity;      // プレイヤーが歩くと周りの埃が引きずられます
    uint EmitCount;             // このフレームに生成したい個数
    float3 VolumeCenterOffset;  // 箱の中心をカメラからずらす量（床〜天井を覆うため下へずらします）
    uint MaxParticles;
    float MinLifetime;
    float MaxLifetime;
    float MinSize;
    float MaxSize;
};

static const uint DUST_THREAD_GROUP_SIZE = 64;

// 授業資料と同じXorshift。粒子やスレッドごとに状態を持ち、呼ぶたびに更新します。
uint XorShift(inout uint seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

float RandomFloat01(inout uint seed)
{
    return (XorShift(seed) & 0x00FFFFFF) / 16777215.0f;
}

float3 RandomFloat3Signed(inout uint seed)
{
    return float3(
        RandomFloat01(seed),
        RandomFloat01(seed),
        RandomFloat01(seed)) * 2.0f - 1.0f;
}

#endif
