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
    };
}
