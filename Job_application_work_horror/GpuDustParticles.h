// ============================================================================
// ファイルの役割: 空気中を漂う埃を、生成・更新・削除から描画までGPUだけで行う
//                 「GPUパーティクル」を管理します。
// 主な技術: Append/Consume StructuredBuffer（内部カウンター）、CopyStructureCount、
//           DispatchIndirect、DrawIndexedInstancedIndirect、SV_VertexIDによる
//           頂点バッファなしのビルボード生成
// CPUは「このフレームに何個作りたいか」と経過時間を渡すだけで、
// 生きている粒子の数は最後までGPU上だけで決まります（GPU駆動）。
// ============================================================================

#pragma once

#include "ComputeShader.h"
#include "Renderer.h"

#include <array>
#include <cstdint>
#include <wrl/client.h>

class Camera;

namespace Effect
{
    class GpuDustParticles final
    {
    public:
        // 同時に存在できる粒子の最大数（バッファの容量）です。
        static constexpr uint32_t MaxParticles = 32768;
        // 1フレームに生成できる最大数。乱数の状態をこのスレッド数だけ持ちます。
        static constexpr uint32_t MaxEmitPerFrame = 1024;

        void Init();
        void Uninit();

        // 一覧を空にします。シーンを切り替えたとき、前の場所の埃を残さないために呼びます。
        void Reset();

        // 生成・更新をCompute Shaderで行います。deltaTimeが0なら（ポーズ中など）何もしません。
        // density は 0〜1 で、演出品質の設定に合わせて量を減らすために使います。
        void Simulate(const Camera& camera, float deltaTime, float density);

        // 本描画の最後に、加算合成で埃を描きます。
        void Draw(const Camera& camera);

        // デバッグ表示用。GPUから数フレーム遅れで読み戻した生存数です（描画は待ちません）。
        uint32_t GetAliveCountForDebug() const { return m_DebugAliveCount; }
        uint32_t GetLastEmitCount() const { return m_LastEmitCount; }

    private:
        // dustParticleCommon.hlsli の DustParticle と同じ並びです。
        struct Particle
        {
            DirectX::SimpleMath::Vector3 Position;
            float Age;
            DirectX::SimpleMath::Vector3 Velocity;
            float Lifetime;
            float Size;
            uint32_t Seed;
            float Phase;
            float Padding;
        };
        static_assert(sizeof(Particle) == 48, "Particle must match dustParticleCommon.hlsli");

        // dustParticleCommon.hlsli の DustSimulationBuffer と同じ並びです。
        struct SimulationParams
        {
            DirectX::SimpleMath::Vector3 CameraPosition;
            float DeltaTime;
            DirectX::SimpleMath::Vector3 VolumeExtent;
            float SimulationTime;
            DirectX::SimpleMath::Vector3 CameraVelocity;
            uint32_t EmitCount;
            DirectX::SimpleMath::Vector3 VolumeCenterOffset;
            uint32_t MaxParticles;
            float MinLifetime;
            float MaxLifetime;
            float MinSize;
            float MaxSize;
        };
        static_assert(sizeof(SimulationParams) == 80,
            "SimulationParams must match dustParticleCommon.hlsli");

        // dustVS.hlsl の DustDrawBuffer と同じ並びです。
        struct DrawParams
        {
            DirectX::SimpleMath::Matrix View;
            DirectX::SimpleMath::Matrix Projection;
            DirectX::SimpleMath::Vector3 VolumeCenter;
            float Time;
            DirectX::SimpleMath::Vector3 VolumeExtent;
            float PixelWorldScale;
        };
        static_assert(sizeof(DrawParams) == 160, "DrawParams must match dustVS.hlsl");

        // 生存リストはAppendの入力と出力を同じにできないため、2本を交互に使います。
        struct ParticleList
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> Buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> UAV;  // カウンター付き
        };

        bool CreateParticleList(ParticleList& list);
        void RunArgsShader();
        void ReadBackAliveCount();

        ComputeShader m_EmitShader;
        ComputeShader m_UpdateShader;
        ComputeShader m_ArgsShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;

        std::array<ParticleList, 2> m_Lists;
        uint32_t m_CurrentList = 0;     // 生きている粒子が入っている側

        Microsoft::WRL::ComPtr<ID3D11Buffer> m_SeedBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SeedUAV;
        // CopyStructureCountで内部カウンターを写す、普通の(RAW)バッファです。
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_CountBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CountSRV;
        // DispatchIndirect(先頭12バイト)とDrawIndexedInstancedIndirect(続く20バイト)の引数です。
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_IndirectArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_IndirectArgsUAV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_IndexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_SimulationParamsBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_DrawParamsBuffer;

        // 生存数をデバッグ表示するための読み戻し用。GPUを待たないよう3本を順番に使います。
        static constexpr uint32_t ReadbackLatency = 3;
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, ReadbackLatency> m_ReadbackBuffers;
        uint32_t m_ReadbackFrame = 0;
        uint32_t m_DebugAliveCount = 0;

        bool m_CounterResetPending = true;
        bool m_HasPreviousCameraPosition = false;
        DirectX::SimpleMath::Vector3 m_PreviousCameraPosition;
        float m_SimulationTime = 0.0f;
        float m_EmitRemainder = 0.0f;
        uint32_t m_LastEmitCount = 0;
    };
}
