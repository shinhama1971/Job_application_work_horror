// ============================================================================
// ファイルの役割: 揺れる裸電球（2面）の影に使う、電球から6方向を撮った「電球からの距離」のキューブマップを作っている。
// 主な技術: 点光源の影（全方向のシャドウマップ）、キューブマップの各面への描画、距離を色として書く方式、右手系と左手系の向きの変換
//
// ・懐中電灯の影（ShadowMap）は1方向だけを撮るが、電球は周り全部を照らすので、6方向を撮っている。
// ・深度ではなく「電球からの距離（光の届く範囲で割った0〜1）」を書いている。6面で投影が違っても、同じ物差しで比べられる。
// ・電球は毎フレーム揺れるので、6面とも毎フレーム描き直している（影は描く物が少ない深度だけの描画なので軽い）。
// ・撮った画像は t2 に設定し、壁（litTexturePS）と床（wetFloorPS）が pointShadow.hlsli で読んでいる。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <array>
#include <d3d11.h>
#include <wrl/client.h>

#include "Shader.h"

namespace Effect
{
    class PointShadowMap
    {
    public:
        // 1面の大きさ（画素）
        static constexpr int FaceSize = 256;

        void Init();
        void Uninit();
        // face番目（0〜5）の面を描き始めている：描画先をその面にし、電球の位置から面の向きを見る行列に差し替えている
        void BeginFace(int face, const DirectX::SimpleMath::Vector3& lightPosition);
        // 6面を描き終えている：描画先とビューポートを戻し、キューブマップを t2 に設定している
        void End();
        // 影を使わない場面で、t2 から外している
        void Unbind();
        // 電球からの距離を書くシェーダー（ShadowMap::SetShaderOverride に渡す）
        Shader& GetShader() { return m_Shader; }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 6> m_FaceViews;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CubeView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        Shader m_Shader;
        // 描き始める前のビューポート（描き終えたら戻す）
        D3D11_VIEWPORT m_PreviousViewport{};
        UINT m_PreviousViewportCount = 1;
        bool m_HasPreviousViewport = false;
    };
}
