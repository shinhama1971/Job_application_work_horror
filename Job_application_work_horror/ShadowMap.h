// ============================================================================
// ファイルの役割: 懐中電灯（ライト）から見た深度を描き、影の判定に使うテクスチャを作っている。
// 主な技術: 深度テクスチャ、比較サンプラー、ライトの行列、深度だけを書く描画
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "Renderer.h"
#include "Shader.h"

class Camera;

namespace Effect
{
    // 懐中電灯はカメラと同じ位置・向きから出ているので、カメラの位置から見た深度を描いて影に使っている。
    class ShadowMap
    {
    private:
        // シェーダーへ渡す値（b8）：ライトのビュー×射影の行列と、x=1テクセルの大きさ、y=深度の比較のずれ（バイアス）、z=near、w=far
        struct ShadowBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
            DirectX::SimpleMath::Vector4 Parameters;
        };

        // 影のテクスチャの大きさ（1024x1024）
        static constexpr UINT ShadowResolution = 1024;

        // 深度テクスチャ、深度を書く口、シェーダーから読む口、深度を比べて読むサンプラー
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_ComparisonSampler;
        // 影を描くときのラスタライザー（深度を少しずらす設定）、元のラスタライザー、定数バッファ
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_ShadowRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ShadowBuffer;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;// （今は使っていない）

        // 影を描く前のビューポート（終わったら戻す）
        D3D11_VIEWPORT m_PreviousViewport{};
        UINT m_PreviousViewportCount = 1;
        // 深度だけを書くシェーダー
        Shader m_DepthShader;

    public:
        // テクスチャ・サンプラー・ラスタライザー・定数バッファ・シェーダーを作る／解放する
        void Init();
        void Uninit();
        // 影を描き始めている：描画先を深度テクスチャにし、カメラの位置から見た行列を設定している
        void Begin(const Camera& camera);
        // 影を描き終えている：描画先と状態を戻し、影のテクスチャをt5、比較サンプラーをs1に設定している
        void End();
        // 描き直さないフレームに、前に描いた影のテクスチャを設定している
        void Bind();
        // 深度だけを書くシェーダーを設定している
        void SetShader();
    };
}
