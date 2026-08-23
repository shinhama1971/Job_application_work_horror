#pragma once

#include <wrl/client.h>
#include "Renderer.h"
#include "Shader.h"

class Camera;

namespace Effect
{
    class ShadowMap
    {
    private:
        struct ShadowBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
            DirectX::SimpleMath::Vector4 Parameters;
        };

        static constexpr UINT ShadowResolution = 1024;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_ComparisonSampler;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_ShadowRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ShadowBuffer;

        D3D11_VIEWPORT m_PreviousViewport{};
        UINT m_PreviousViewportCount = 1;
        Shader m_DepthShader;

    public:
        void Init();
        void Uninit();
        void Begin(const Camera& camera);
        void End();
        void Bind();
        void SetShader();
    };
}
