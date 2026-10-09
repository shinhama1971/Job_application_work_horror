// ============================================================================
// ファイルの役割: 水面用の平面反射のレンダーターゲットと、反射を描くカメラを管理している。
// 主な技術: 低い解像度へのRender To Texture、上下を反転したカメラ、SRVとRTVの同時使用の回避、描画状態の復元
// Begin/Endの間だけカメラの行列とラスタライザーを差し替え、終わったら元に戻している。
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "RenderTexture.h"

class Camera;

namespace Effect
{
    // 床（水面）を鏡にして、上下を反転した視点から世界を描き、濡れた床のシェーダーへ渡している。
    class PlanarReflection
    {
    private:
        // 反射カメラのビュー×射影の行列（頂点シェーダーのb9。濡れた床のシェーダーで反射のUVを求めるのに使っている）
        struct ReflectionBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
        };

        // 反射を描くテクスチャ、行列の定数バッファ、反射用のラスタライザー、元のラスタライザー
        Graphics::RenderTexture m_Texture;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ReflectionBuffer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;

    public:
        // テクスチャ・定数バッファ・ラスタライザーを作る／解放する
        void Init();
        void Uninit();
        // 反射を描き始めている：描画先をテクスチャにし、高さreflectionHeightの面で上下を反転したカメラにしている
        void Begin(Camera& camera, float reflectionHeight);
        // 反射を描き終えている：カメラと描画先を戻し、反射のテクスチャをt6に設定している
        void End(Camera& camera);
        // 描き直さないフレームに、前に描いた反射のテクスチャをt6に設定している
        void Bind();
    };
}
