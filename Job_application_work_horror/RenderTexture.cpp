// ============================================================================
// ファイルの役割: 画面以外に描くためのテクスチャと、その描画先（RTV）・読み取り口（SRV）・書き込み口（UAV）・深度を管理している。
// 主な技術: Render Target View、Shader Resource View、Unordered Access View、Depth Stencil
// ============================================================================

#include "RenderTexture.h"

namespace Graphics
{
    void RenderTexture::Init(
        int width,
        int height,
        DXGI_FORMAT format,
        bool enableUnorderedAccess)
    {
        ID3D11Device* device = Renderer::GetDevice();

        m_Width = width;
        m_Height = height;

        // 描画先にもシェーダーの入力にもなるテクスチャを作っている（ミップマップなし、マルチサンプルなし）
        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = format;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;
        if (enableUnorderedAccess)
        {
            texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        m_UAV.Reset();
        m_DepthView.Reset();
        m_DepthTexture.Reset();
        device->CreateTexture2D(
            &texDesc,
            nullptr,
            m_Texture.ReleaseAndGetAddressOf());

        device->CreateRenderTargetView(
            m_Texture.Get(),
            nullptr,
            m_RTV.ReleaseAndGetAddressOf()
        );

        device->CreateShaderResourceView(
            m_Texture.Get(),
            nullptr,
            m_SRV.ReleaseAndGetAddressOf()
        );

        if (enableUnorderedAccess)
        {
            device->CreateUnorderedAccessView(
                m_Texture.Get(),
                nullptr,
                m_UAV.ReleaseAndGetAddressOf()
            );
        }

        // 描画先と深度バッファは同じ大きさで作っている。
        // コンピュートシェーダー専用のブルームの画像には、深度バッファを作っていない。
        if (!enableUnorderedAccess)
        {
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                m_DepthTexture.ReleaseAndGetAddressOf());
            device->CreateDepthStencilView(
                m_DepthTexture.Get(),
                nullptr,
                m_DepthView.ReleaseAndGetAddressOf());
        }

    }

    void RenderTexture::Uninit()
    {
        m_UAV.Reset();
        m_SRV.Reset();
        m_RTV.Reset();
        m_Texture.Reset();
        m_DepthView.Reset();
        m_DepthTexture.Reset();
        m_Width = 0;
        m_Height = 0;
    }

    // このテクスチャを描画先にし、ビューポートを大きさに合わせ、深度を消している
    void RenderTexture::SetRenderTarget()
    {
        ID3D11DeviceContext* context =
            Renderer::GetDeviceContext();

        // このテクスチャ自身をシェーダーから読んだままだと描画先にできないため、t0・t1の読み取りを外している
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullSRVs);

        ID3D11RenderTargetView* rtv = m_RTV.Get();

        // このテクスチャと深度バッファを描画先にしている
        context->OMSetRenderTargets(
            1,
            &rtv,
            m_DepthView.Get()
        );

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(m_Width);
        viewport.Height = static_cast<float>(m_Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        if (m_DepthView)
        {
            context->ClearDepthStencilView(
                m_DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        }
    }

    // 指定した色で塗りつぶしている
    void RenderTexture::Clear(float r, float g, float b, float a)
    {
        float clearColor[4] = { r, g, b, a };

        Renderer::GetDeviceContext()->ClearRenderTargetView(
            m_RTV.Get(),
            clearColor
        );
    }
}
