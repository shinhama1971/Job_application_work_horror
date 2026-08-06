#pragma once
#include "Renderer.h"

namespace Graphics
{
    class RenderTexture
    {
    private:
        ID3D11Texture2D* m_Texture = nullptr;
        ID3D11RenderTargetView* m_RTV = nullptr;
        ID3D11ShaderResourceView* m_SRV = nullptr;
        ID3D11UnorderedAccessView* m_UAV = nullptr;
        int m_Width = 0;
        int m_Height = 0;

    public:
        void Init(
            int width,
            int height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
            bool enableUnorderedAccess = false);
        void Uninit();

        void SetRenderTarget();
        void Clear(float r, float g, float b, float a);

        ID3D11ShaderResourceView* GetSRV()
        {
            return m_SRV;
        }

        ID3D11UnorderedAccessView* GetUAV() { return m_UAV; }
        ID3D11Texture2D* GetTexture() { return m_Texture; }
        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
    };
}
