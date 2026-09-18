// ============================================================================
// ファイルの役割: 水面用の平面反射レンダーターゲットと反射カメラを管理します。
// 主な技術: 低解像度Render To Texture、反転カメラ、SRV/RTV競合回避、描画状態の復元
// Begin/Endの間だけカメラ行列とRasterizerを差し替え、終了時に元へ戻します。
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "RenderTexture.h"

class Camera;

namespace Effect
{
    class PlanarReflection
    {
    private:
        struct ReflectionBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
        };

        Graphics::RenderTexture m_Texture;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ReflectionBuffer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;

    public:
        void Init();
        void Uninit();
        void Begin(Camera& camera, float reflectionHeight);
        void End(Camera& camera);
        void Bind();
    };
}
