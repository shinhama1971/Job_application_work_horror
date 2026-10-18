// ============================================================================
// ファイルの役割: 平面反射のレンダーターゲットと、反射を描くカメラを管理している（床の水面と、2面の壁の鏡）。
// 主な技術: 低い解像度へのRender To Texture、面で反転したカメラ、SRVとRTVの同時使用の回避、描画状態の復元
// Begin/Endの間だけカメラの行列とラスタライザーを差し替え、終わったら元に戻している。
//
// ・反射は「面の向こう側の、面で反転した位置に立つカメラ」から、本物の景色を描いて作っている。
//   面の上の点をこのカメラで画面へ投影した位置の色が、その点に映る色になる（床と鏡のシェーダーが読む）。
// ・床の水面（t6・頂点シェーダーのb9）と壁の鏡（t13・ピクセルシェーダーのb9）で、テクスチャと行列の置き場所を分けている。
//   定数バッファのスロットは b0〜b13 がすべて使われているため、鏡の行列は、床が使っていないピクセルシェーダー側のb9に置いている。
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "RenderTexture.h"

class Camera;

namespace Effect
{
    // 面（床や壁）を鏡にして、面で反転した視点から世界を描き、床や鏡のシェーダーへ渡している。
    class PlanarReflection
    {
    private:
        // 反射カメラのビュー×射影の行列（反射のUVを求めるのに使っている）
        struct ReflectionBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
        };

        // 反射を描くテクスチャ、行列の定数バッファ、反射用のラスタライザー、元のラスタライザー
        Graphics::RenderTexture m_Texture;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ReflectionBuffer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;
        // 反射のテクスチャを置くスロット（t）、行列を置くスロット（b）、行列をピクセルシェーダー側に置くか
        unsigned int m_TextureSlot = 6;
        unsigned int m_BufferSlot = 9;
        bool m_BufferInPixelShader = false;

        // 行列とテクスチャを、決めたスロットに設定している
        void BindResources();

    public:
        // テクスチャ・定数バッファ・ラスタライザーを作る／解放する。
        // 床の水面は既定のまま（t6・頂点シェーダーのb9）、壁の鏡は（13, 9, true）で作っている。
        void Init(unsigned int textureSlot = 6, unsigned int bufferSlot = 9, bool bufferInPixelShader = false);
        void Uninit();
        // 床の水面：高さreflectionHeightの面で上下を反転したカメラで描き始めている
        void Begin(Camera& camera, float reflectionHeight);
        // 任意の面（planePointを通り、planeNormalの向きを表とする面）で反転したカメラで描き始めている
        void BeginPlane(
            Camera& camera,
            const DirectX::SimpleMath::Vector3& planePoint,
            const DirectX::SimpleMath::Vector3& planeNormal);
        // 反射を描き終えている：カメラと描画先を戻し、反射のテクスチャと行列を設定している
        void End(Camera& camera);
        // 描き直さないフレームに、前に描いた反射のテクスチャと行列を設定している
        void Bind();
    };
}
