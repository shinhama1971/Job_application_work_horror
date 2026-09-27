// ============================================================================
// シェーダーの役割: GPUパーティクル（埃）の更新と削除（Update）。
// 入力の一覧を読み、生き残った粒子だけを別のAppendバッファへ追加します。
// 寿命が尽きた粒子は追加せずにreturnするだけで一覧から消え、配列に穴が空きません。
// 入力と出力は同じバッファにできないため、2本のバッファを毎フレーム入れ替えます。
// ============================================================================

#include "dustParticleCommon.hlsli"

StructuredBuffer<DustParticle> InputParticles : register(t0);
// Emit後に CopyStructureCount で写した入力側の個数
ByteAddressBuffer InputCount : register(t1);

AppendStructuredBuffer<DustParticle> OutputParticles : register(u0);

// 廊下の空気がゆっくり回るような流れ。位置と時間で向きが変わります。
float3 GetAirCurrent(float3 position, float phase)
{
    const float time = SimulationTime;
    return float3(
        sin(position.z * 0.021f + time * 0.17f + phase * 6.2831853f),
        sin(time * 0.23f + phase * 10.7f) * 0.35f - 0.22f,     // わずかに沈んでいく
        cos(position.x * 0.019f - time * 0.13f + phase * 4.1f)) * 1.25f;
}

[numthreads(DUST_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    // DispatchIndirectで起動したグループ数は64単位に切り上げなので、端数は捨てます。
    if (index >= InputCount.Load(0))
    {
        return;
    }

    DustParticle particle = InputParticles[index];

    particle.Age += DeltaTime;
    if (particle.Age >= particle.Lifetime)
    {
        return; // 寿命切れ: 出力へ追加しない = 削除
    }

    // プレイヤーから離れて箱の外へ出た粒子も削除し、新しい位置で作り直します。
    const float3 local = particle.Position - (CameraPosition + VolumeCenterOffset);
    if (any(abs(local) > VolumeExtent * 1.05f))
    {
        return;
    }

    // 近くを歩くと、その速さで周りの空気と埃が引きずられます。
    const float3 fromCamera = particle.Position - CameraPosition;
    const float wakeFalloff = saturate(1.0f - length(fromCamera) / 34.0f);
    const float3 targetVelocity =
        GetAirCurrent(particle.Position, particle.Phase) +
        CameraVelocity * (wakeFalloff * wakeFalloff * 0.45f);

    // 目標速度へ滑らかに近づけ、さらに小さな不規則な揺れ（ブラウン運動）を足します。
    const float response = 1.0f - exp(-DeltaTime * 1.6f);
    uint seed = particle.Seed;
    particle.Velocity = lerp(particle.Velocity, targetVelocity, response) +
        RandomFloat3Signed(seed) * (2.4f * DeltaTime);
    particle.Seed = seed;
    particle.Position += particle.Velocity * DeltaTime;

    OutputParticles.Append(particle);
}
