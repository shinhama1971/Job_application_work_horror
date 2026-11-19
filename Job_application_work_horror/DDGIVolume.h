// ============================================================================
// ファイルの役割: DDGI（Dynamic Diffuse Global Illumination）。部屋に並べたプローブから光線を飛ばして、
// 壁や床で跳ね返った光（間接光）を毎フレーム少しずつ求め、壁と床のシェーダーに渡している。
// 主な技術: Compute Shader によるソフトウェアのレイトレーシング（部屋の形との交差）、八面体マップ、ヒステリシス、
//           チェビシェフの不等式による光漏れの防止（Majercik ほか 2019。NVIDIA RTXGI と同じ方式）
//
// ・NVIDIA の RTXGI は DirectX 12 と DXR（GPU のレイトレーシング）が前提で、DirectX 11 のこのゲームには入れられない。
//   そのため論文の方式を自分で作り、光線は水たまりの反射で使っている部屋の形（壁の長方形・床・天井）に当てている。
// ・1フレームの流れ：光線を飛ばす（ddgiTraceCS）→ 放射照度と距離の八面体マップを更新（ddgiIrradianceUpdateCS・
//   ddgiDistanceUpdateCS）→ 画面の各画素の間接光を縦横半分の解像度で求める（ddgiApplyCS）→ 壁と床のシェーダーが t18 で読む。
// ・プローブの格子は、部屋の形（Renderer::GetRoomOcclusion）が変わったときに、壁の範囲に合わせて作り直している。
//   部屋の形がない場面（森）では使わない。
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <random>

#include <SimpleMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "ComputeShader.h"

struct LIGHT;

namespace Effect
{
    class DDGIVolume
    {
    public:
        // 1つのプローブから飛ばす光線の数（ddgiCommon.hlsli の DDGI_MAX_RAYS と同じ）と、八面体マップの大きさ（中の画素の数）
        static constexpr uint32_t RaysPerProbe = 64;
        static constexpr uint32_t IrradianceTexels = 8;
        static constexpr uint32_t DistanceTexels = 16;

        // width・height は描く範囲の一番大きい大きさ（間接光の画像は、その縦横半分で作る）
        void Init(int width, int height);
        void Uninit();

        // 部屋の形が変わっていれば格子を作り直し、光線を飛ばして八面体マップを更新している。使えない場面では false。
        // pointLights・lightCount は点光源の一覧（TiledLighting）、light は懐中電灯（ビュー空間の向き）、view はプレイヤーの視点
        bool Update(ID3D11ShaderResourceView* pointLights, uint32_t lightCount,
            const LIGHT& light, const DirectX::SimpleMath::Vector3& cameraPosition,
            const DirectX::SimpleMath::Matrix& view, uint32_t updateFrames);
        // 深度プリパスの深度から、画面の各画素の間接光を求め、ピクセルシェーダーの t18 に設定している（Update が true のフレームだけ）
        void Apply(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& projection,
            const D3D11_VIEWPORT& viewport);
        // t18 から外している（本描画以外の視点の描画の前と、使わない場面）
        void Unbind();

        uint32_t GetProbeCount() const { return m_ProbeCounts[0] * m_ProbeCounts[1] * m_ProbeCounts[2]; }

    private:
        // プローブ格子はXYZの3軸。HLSLのuint3 ProbeCountsと同じ要素数にする。
        static constexpr std::size_t ProbeAxisCount = 3;
        // シェーダーへ渡す値（ddgiCommon.hlsli の DDGIBuffer と同じ並び）
        struct DDGIParameters
        {
            DirectX::SimpleMath::Vector3 ProbeOrigin;
            uint32_t RaysPerProbe;
            DirectX::SimpleMath::Vector3 ProbeSpacing;
            float Hysteresis;
            uint32_t ProbeCounts[ProbeAxisCount];
            uint32_t LightCount;
            DirectX::SimpleMath::Matrix RayRotation;
            DirectX::SimpleMath::Vector3 FlashlightPosition;
            float FlashlightRange;
            DirectX::SimpleMath::Vector3 FlashlightDirection;
            float FlashlightIntensity;
            DirectX::SimpleMath::Vector3 FlashlightColor;
            float FlashlightEnabled;
            DirectX::SimpleMath::Vector4 FlashlightCone;
            float NormalBias;
            float ViewBias;
            float BounceStrength;
            float ResetHistory;
            DirectX::SimpleMath::Matrix InverseProjection;
            DirectX::SimpleMath::Matrix InverseView;
            DirectX::SimpleMath::Vector2 ViewportSize;
            DirectX::SimpleMath::Vector2 OutputSize;
            DirectX::SimpleMath::Vector2 ViewportOffset;
            float MaxRayDistance;
            uint32_t ProbeUpdateStride;
            uint32_t ProbeUpdatePhase;
            uint32_t Padding[3];
        };
        static_assert(sizeof(DDGIParameters) == 368);
        static_assert(offsetof(DDGIParameters, RayRotation) == 48);
        static_assert(offsetof(DDGIParameters, InverseProjection) == 192);
        static_assert(offsetof(DDGIParameters, ProbeUpdateStride) == 348);
        static_assert(offsetof(DDGIParameters, ProbeUpdatePhase) == 352);

        // Compute Shader が書けて、ほかのシェーダーが読める画像1枚
        struct GpuTexture
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> UAV;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
            bool Create(uint32_t width, uint32_t height, DXGI_FORMAT format);
            void Reset();
        };

        // 部屋の形から、プローブの格子（位置・間隔・数）を決め、画像を作り直している
        bool RebuildGrid();
        void UploadParameters();

        // 間接光の画像（縦横半分の解像度）の大きさ
        int m_Width = 0;
        int m_Height = 0;
        // 格子を作った部屋の形の番号（Renderer::GetRoomOcclusionVersion。変わったら作り直す）
        unsigned int m_RoomVersion = 0;
        bool m_HasGrid = false;
        bool m_Active = false;
        bool m_ResetHistory = true;

        DirectX::SimpleMath::Vector3 m_ProbeOrigin;
        DirectX::SimpleMath::Vector3 m_ProbeSpacing;
        std::array<uint32_t, ProbeAxisCount> m_ProbeCounts{};
        DDGIParameters m_Parameters{};

        // 光線の結果（光線の番号 x プローブの番号）、放射照度と距離の八面体マップ（前と今を交互に使う2枚ずつ）、画面の間接光
        GpuTexture m_RayResults;
        std::array<GpuTexture, 2> m_IrradianceAtlas;
        std::array<GpuTexture, 2> m_DistanceAtlas;
        GpuTexture m_IndirectLight;
        // 今の（読む）八面体マップの番号（0か1）
        int m_Current = 0;
        uint32_t m_UpdatePhase = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ParameterBuffer;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_LinearClamp;
        ComputeShader m_TraceShader;
        ComputeShader m_IrradianceUpdateShader;
        ComputeShader m_DistanceUpdateShader;
        ComputeShader m_ApplyShader;
        // 光線の向きを毎フレーム回す乱数（自動撮影と計測では決まった種）
        std::mt19937 m_Random;
    };
}
