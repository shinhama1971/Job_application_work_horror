// ============================================================================
// シェーダーの役割: 粒子数から、間接実行（Indirect）の引数を作ります。
// Appendバッファの内部カウンターはCopyStructureCountで普通のバッファへ写してから使い、
// ここで「粒子数」を「スレッドグループ数」と「描画インデックス数」へ単位変換します。
// CopyStructureCountの出力先を引数バッファにすると、Y/Zなどが不定値のままになるためです。
// ============================================================================

#include "dustParticleCommon.hlsli"

ByteAddressBuffer ParticleCount : register(t0);

// 0〜11バイト  : DispatchIndirect の引数（ThreadGroupCountX, Y, Z）
// 12〜31バイト : DrawIndexedInstancedIndirect の引数
//               （IndexCountPerInstance, InstanceCount, StartIndexLocation,
//                 BaseVertexLocation, StartInstanceLocation）
RWByteAddressBuffer IndirectArgs : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    const uint count = min(ParticleCount.Load(0), MaxParticles);

    const uint groups =
        (count + DUST_THREAD_GROUP_SIZE - 1) / DUST_THREAD_GROUP_SIZE;
    IndirectArgs.Store3(0, uint3(groups, 1, 1));

    // 1粒子 = 4頂点・6インデックス。インスタンス描画は使わず InstanceCount = 1 にします。
    IndirectArgs.Store4(12, uint4(count * 6, 1, 0, 0));
    IndirectArgs.Store(28, 0);
}
