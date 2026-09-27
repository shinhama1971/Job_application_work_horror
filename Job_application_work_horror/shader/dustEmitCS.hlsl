// ============================================================================
// シェーダーの役割: GPUパーティクル（埃）の生成（Emit）。
// カメラを囲む箱の中へ新しい粒子を作り、AppendStructuredBufferへ追加します。
// Append()は配列の末尾へ追加するだけなので、空き領域を探す必要がありません。
// ============================================================================

#include "dustParticleCommon.hlsli"

// 前フレームの更新後に CopyStructureCount で写した「生きている粒子数」
ByteAddressBuffer AliveCount : register(t0);

// スレッドごとの乱数状態。使うたびに書き戻し、毎フレーム違う値を出します。
RWStructuredBuffer<uint> RandomSeeds : register(u0);
// 生きている粒子の一覧。内部カウンターの位置へ追加されます。
AppendStructuredBuffer<DustParticle> Particles : register(u1);

[numthreads(DUST_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;

    // 生成数を超えたスレッドと、バッファに入りきらない分は早期に抜けます。
    // （Appendで容量を超えると未定義動作になるため、GPU上の個数で上限を守ります）
    const uint aliveCount = min(AliveCount.Load(0), MaxParticles);
    if (index >= EmitCount || index >= MaxParticles - aliveCount)
    {
        return;
    }

    uint seed = RandomSeeds[index];

    DustParticle particle;
    particle.Position = CameraPosition + VolumeCenterOffset +
        RandomFloat3Signed(seed) * VolumeExtent;
    // 生まれた瞬間はほぼ静止させ、空気の流れに少しずつ乗せます。
    particle.Velocity = RandomFloat3Signed(seed) * 0.6f;
    particle.Age = 0.0f;
    particle.Lifetime = lerp(MinLifetime, MaxLifetime, RandomFloat01(seed));
    // 小さな粒を多く、大きな粒を少なくします（二乗で小さい側へ寄せます）。
    const float sizeRandom = RandomFloat01(seed);
    particle.Size = lerp(MinSize, MaxSize, sizeRandom * sizeRandom);
    particle.Phase = RandomFloat01(seed);
    particle.Seed = XorShift(seed) | 1u;    // 0になるとXorshiftが止まるため奇数にします
    particle.Padding = 0.0f;

    Particles.Append(particle);
    RandomSeeds[index] = seed;
}
