#include "RenderTexture.h"

namespace Graphics
{
    void RenderTexture::Init(int width, int height)
    {
        ID3D11Device* device = Renderer::GetDevice();

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;

        device->CreateTexture2D(&texDesc, nullptr, &m_Texture);

        device->CreateRenderTargetView(
            m_Texture,
            nullptr,
            &m_RTV
        );

        device->CreateShaderResourceView(
            m_Texture,
            nullptr,
            &m_SRV
        );
    }

    void RenderTexture::Uninit()
    {
        SAFE_RELEASE(m_SRV);
        SAFE_RELEASE(m_RTV);
        SAFE_RELEASE(m_Texture);
    }

    void RenderTexture::SetRenderTarget()
    {
        ID3D11DeviceContext* context =
            Renderer::GetDeviceContext();

        ID3D11RenderTargetView* rtv = m_RTV;

        // まずは深度なしで確認
        context->OMSetRenderTargets(
            1,
            &rtv,
            nullptr
        );
    }

    void RenderTexture::Clear(float r, float g, float b, float a)
    {
        float clearColor[4] = { r, g, b, a };

        Renderer::GetDeviceContext()->ClearRenderTargetView(
            m_RTV,
            clearColor
        );
    }
}