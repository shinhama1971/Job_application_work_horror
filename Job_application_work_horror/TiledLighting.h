// ============================================================================
// ファイルの役割: 画面をタイルに分け、タイルごとに影響する点光源だけを
//                 Compute Shaderで絞り込む「タイルベースライティング」を管理します。
// 主な技術: Compute Shader、StructuredBuffer(SRV/UAV)、Group Shared Memory、
//           InterlockedAdd、タイル視錐台と球の交差判定
// 照明の計算そのものは既存のピクセルシェーダーが行い、ここではライト一覧と
// タイル別リストをGPUへ用意します（Forward+ / Tiled Forward と呼ばれる構成）。
// ============================================================================

#pragma once

#include "ComputeShader.h"
#include "Renderer.h"

#include <cstdint>
#include <vector>
#include <wrl/client.h>

class Camera;

namespace Effect
{
    class TiledLighting final
    {
    public:
        // シェーダー側（common.hlsl / tiledLightCullingCS.hlsl）と同じ値にします。
        static constexpr uint32_t TileSize = 16;
        static constexpr uint32_t MaxLightsPerTile = 64;
        static constexpr uint32_t MaxLights = 256;

        void Init();
        void Uninit();

        // このフレームの点光源一覧をGPUへ転送します。MaxLightsを超えた分は使いません。
        void SetLights(const std::vector<ENVIRONMENT_POINT_LIGHT>& lights);
        const std::vector<ENVIRONMENT_POINT_LIGHT>& GetLights() const { return m_Lights; }

        // 反射や監視映像など、本描画以外の視点では全ライトを順に計算させます。
        void BindAllLights();

        // 本描画の直前に呼び、プレイヤー視点のタイル別ライトリストを作って使わせます。
        void BuildTiles(const Camera& camera);

        uint32_t GetLightCount() const { return m_UploadedLightCount; }
        uint32_t GetTileCount() const { return m_TilesX * m_TilesY; }

    private:
        struct CullingParams
        {
            DirectX::SimpleMath::Matrix InverseViewProjection;
            DirectX::SimpleMath::Vector4 CameraPosition;
            uint32_t LightCount;
            uint32_t TilesX;
            uint32_t TilesY;
            uint32_t Padding;
            DirectX::SimpleMath::Vector2 ViewportSize;
            DirectX::SimpleMath::Vector2 ViewportOffset;
        };
        static_assert(sizeof(CullingParams) == 112,
            "CullingParams must match tiledLightCullingCS.hlsl");

        // ピクセルシェーダーのb6。common.hlslのTiledLightBufferと同じ並びです。
        struct ShadingParams
        {
            uint32_t LightCount;
            uint32_t TiledMode;     // 0: 全ライト、1: タイル別リスト
            uint32_t TilesX;
            uint32_t TilesY;
            DirectX::SimpleMath::Vector2 ViewportOffset;
            DirectX::SimpleMath::Vector2 Padding;
        };
        static_assert(sizeof(ShadingParams) == 32,
            "ShadingParams must match common.hlsl");

        void UpdateShadingParams(bool tiled);
        void BindForShading();

        ComputeShader m_CullingShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_LightBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_LightSRV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TileIndexBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TileIndexSRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TileIndexUAV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TileCountBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TileCountSRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TileCountUAV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_CullingParamsBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ShadingParamsBuffer;

        std::vector<ENVIRONMENT_POINT_LIGHT> m_Lights;
        uint32_t m_UploadedLightCount = 0;
        uint32_t m_MaxTiles = 0;
        uint32_t m_TilesX = 1;
        uint32_t m_TilesY = 1;
        DirectX::SimpleMath::Vector2 m_ViewportOffset;
    };
}
