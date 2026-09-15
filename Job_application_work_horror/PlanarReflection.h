// ============================================================================
// ファイルの役割: 水面用の平面反射レンダーターゲットと反射カメラを管理します。
// 主な技術: Render To Texture、反射行列、クリップ平面、SRV/RTV競合回避
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
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
