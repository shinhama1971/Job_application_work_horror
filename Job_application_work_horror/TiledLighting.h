// ============================================================================
// ファイルの役割: 画面をタイルに分け、タイルごとに影響する点光源だけを
//                 Compute Shaderで絞り込む「タイルベースライティング」を管理している。
// 主な技術: Compute Shader、StructuredBuffer(SRV/UAV)、グループ共有メモリ、
//           InterlockedAdd、タイルの視錐台と球の交差判定
// 照明の計算そのものは今あるピクセルシェーダーが行い、ここではライトの一覧と
// タイルごとのリストをGPUへ用意している（Forward+ / Tiled Forward と呼ばれる構成）。
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
    // 点光源の一覧をGPUへ送り、本描画ではタイルごとに光が届くライトだけを数えさせている。
    class TiledLighting final
    {
    public:
        // シェーダー側（common.hlsl / tiledLightCullingCS.hlsl）と同じ値にしている。
        // タイルの大きさ（16x16画素）、1タイルに入れるライトの上限、ライトの数の上限
        static constexpr uint32_t TileSize = 16;
        static constexpr uint32_t MaxLightsPerTile = 64;
        static constexpr uint32_t MaxLights = 256;

        // バッファとシェーダーを作る／解放する
        void Init();
        void Uninit();

        // このフレームの点光源の一覧をGPUへ送っている。MaxLightsを超えた分は使わない。
        void SetLights(const std::vector<ENVIRONMENT_POINT_LIGHT>& lights);
        // 今の点光源の一覧を返している
        const std::vector<ENVIRONMENT_POINT_LIGHT>& GetLights() const { return m_Lights; }

        // 反射や監視映像など、本描画以外の視点では、全部のライトを順に計算させている。
        void BindAllLights();

        // 本描画の直前に呼び、プレイヤー視点のタイルごとのライトリストを作って使わせている。
        // hasDepthPrepassがtrueなら、本描画の深度バッファに描いた深度プリパスの深度から、タイルごとに
        // 一番奥の深度を求め、それより奥の光源も外している。終わると描画先は本描画のものに戻っている。
        void BuildTiles(const Camera& camera, bool hasDepthPrepass);

        // 送ったライトの数と、タイルの数を返している（デバッグ画面用）
        uint32_t GetLightCount() const { return m_UploadedLightCount; }
        uint32_t GetTileCount() const { return m_TilesX * m_TilesY; }

    private:
        // Compute Shaderへ渡す値（b0）：画面の座標からワールド座標へ戻す行列、カメラの位置と向き、
        // ライトの数、タイルの数、深度の範囲で絞るか、描く範囲
        struct CullingParams
        {
            DirectX::SimpleMath::Matrix InverseViewProjection;
            DirectX::SimpleMath::Vector4 CameraPosition;
            DirectX::SimpleMath::Vector4 CameraForward;
            uint32_t LightCount;
            uint32_t TilesX;
            uint32_t TilesY;
            uint32_t UseDepthBounds;    // 0: 深度を使わない、1: タイルの一番奥の深度より奥の光源を外す
            DirectX::SimpleMath::Vector2 ViewportSize;
            DirectX::SimpleMath::Vector2 ViewportOffset;
        };
        // HLSLの定数バッファと同じ大きさかを、コンパイル時に確かめている
        static_assert(sizeof(CullingParams) == 128,
            "CullingParams must match tiledLightCullingCS.hlsl");

        // ピクセルシェーダーのb6。common.hlslのTiledLightBufferと同じ並びにしている。
        struct ShadingParams
        {
            uint32_t LightCount;
            uint32_t TiledMode;     // 0: 全ライト、1: タイルごとのリスト
            uint32_t TilesX;
            uint32_t TilesY;
            DirectX::SimpleMath::Vector2 ViewportOffset;
            DirectX::SimpleMath::Vector2 Padding;
        };
        static_assert(sizeof(ShadingParams) == 32,
            "ShadingParams must match common.hlsl");

        // ピクセルシェーダーへ渡す値を更新している／ライトの一覧とタイルのリストをピクセルシェーダーへ設定している
        void UpdateShadingParams(bool tiled);
        void BindForShading();

        // ライトを振り分けるコンピュートシェーダー
        ComputeShader m_CullingShader;
        // ライトの一覧（CPUから毎フレーム書き換える）とその読み取り口
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_LightBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_LightSRV;
        // タイルごとのライトの番号の並び（Compute Shaderが書き、ピクセルシェーダーが読む）
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TileIndexBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TileIndexSRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TileIndexUAV;
        // タイルごとのライトの数
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TileCountBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TileCountSRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TileCountUAV;
        // Compute Shader用・ピクセルシェーダー用の定数バッファ
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_CullingParamsBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ShadingParamsBuffer;

        // ライトの一覧、送った数、確保したタイルの数の上限、今のタイルの数（横・縦）、描く範囲の左上
        std::vector<ENVIRONMENT_POINT_LIGHT> m_Lights;
        uint32_t m_UploadedLightCount = 0;
        uint32_t m_MaxTiles = 0;
        uint32_t m_TilesX = 1;
        uint32_t m_TilesY = 1;
        DirectX::SimpleMath::Vector2 m_ViewportOffset;
    };
}
