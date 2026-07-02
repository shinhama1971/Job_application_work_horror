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

    public:
        void Init(int width, int height);
        void Uninit();

        void SetRenderTarget();
        void Clear(float r, float g, float b, float a);

        ID3D11ShaderResourceView* GetSRV()
        {
            return m_SRV;
        }
    };
}