#include "ShadowMap.h"

#include "Camera.h"

using namespace DirectX::SimpleMath;

namespace Effect
{
    void ShadowMap::Init()
    {
        ID3D11Device* device = Renderer::GetDevice();

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = ShadowResolution;
        textureDesc.Height = ShadowResolution;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags =
            D3D11_BIND_DEPTH_STENCIL |
            D3D11_BIND_SHADER_RESOURCE;
        device->CreateTexture2D(
            &textureDesc,
            nullptr,
            m_Texture.ReleaseAndGetAddressOf());

        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            m_Texture.Get(),
            &depthViewDesc,
            m_DepthView.ReleaseAndGetAddressOf());

        D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDesc{};
        resourceViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
        resourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceViewDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            m_Texture.Get(),
            &resourceViewDesc,
            m_ShaderResourceView.ReleaseAndGetAddressOf());

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        samplerDesc.BorderColor[0] = 1.0f;
        samplerDesc.BorderColor[1] = 1.0f;
        samplerDesc.BorderColor[2] = 1.0f;
        samplerDesc.BorderColor[3] = 1.0f;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(
            &samplerDesc,
            m_ComparisonSampler.ReleaseAndGetAddressOf());

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_BACK;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.DepthBias = 1100;
        rasterizerDesc.SlopeScaledDepthBias = 1.8f;
        rasterizerDesc.DepthBiasClamp = 0.01f;
        device->CreateRasterizerState(
            &rasterizerDesc,
            m_ShadowRasterizer.ReleaseAndGetAddressOf());

        Renderer::CreateConstantBuffer(
            sizeof(ShadowBuffer),
            m_ShadowBuffer.ReleaseAndGetAddressOf());

        m_DepthShader.Create(
            "shader/shadowDepthVS.hlsl",
            "shader/shadowDepthPS.hlsl");
    }

    void ShadowMap::Uninit()
    {
        m_PreviousRasterizer.Reset();
        m_ShadowBuffer.Reset();
        m_ShadowRasterizer.Reset();
        m_ComparisonSampler.Reset();
        m_ShaderResourceView.Reset();
        m_DepthView.Reset();
        m_Texture.Reset();
    }

    void ShadowMap::Begin(const Camera& camera)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(5, 1, &nullResource);

        context->RSGetViewports(
            &m_PreviousViewportCount,
            &m_PreviousViewport);
        context->RSGetState(m_PreviousRasterizer.ReleaseAndGetAddressOf());

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(ShadowResolution);
        viewport.Height = static_cast<float>(ShadowResolution);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(m_ShadowRasterizer.Get());

        context->OMSetRenderTargets(0, nullptr, m_DepthView.Get());
        context->ClearDepthStencilView(
            m_DepthView.Get(),
            D3D11_CLEAR_DEPTH,
            1.0f,
            0);

        const Vector3 lightPosition = camera.GetPosition();
        const Vector3 lightForward = camera.GetForward();
        const Matrix lightView = Matrix::CreateLookAt(
            lightPosition,
            lightPosition + lightForward,
            Vector3::Up);
        const Matrix lightProjection = Matrix::CreatePerspectiveFieldOfView(
            DirectX::XMConvertToRadians(66.0f),
            1.0f,
            2.0f,
            280.0f);

        ShadowBuffer buffer{};
        buffer.ViewProjection = (lightView * lightProjection).Transpose();
        buffer.Parameters = Vector4(
            1.0f / static_cast<float>(ShadowResolution),
            0.0012f,
            2.0f,
            280.0f);
        context->UpdateSubresource(
            m_ShadowBuffer.Get(),
            0,
            nullptr,
            &buffer,
            0,
            0);
        ID3D11Buffer* shadowBuffer = m_ShadowBuffer.Get();
        context->VSSetConstantBuffers(8, 1, &shadowBuffer);
        context->PSSetConstantBuffers(8, 1, &shadowBuffer);
    }

    void ShadowMap::End()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        Renderer::SetBackBufferRenderTarget();
        context->RSSetViewports(
            m_PreviousViewportCount,
            &m_PreviousViewport);
        context->RSSetState(m_PreviousRasterizer.Get());
        m_PreviousRasterizer.Reset();

        ID3D11ShaderResourceView* shadowResource =
            m_ShaderResourceView.Get();
        ID3D11SamplerState* comparisonSampler =
            m_ComparisonSampler.Get();
        context->PSSetShaderResources(5, 1, &shadowResource);
        context->PSSetSamplers(1, 1, &comparisonSampler);
    }

    void ShadowMap::SetShader()
    {
        m_DepthShader.SetGPU();

        // The shadow pass writes depth only. Leaving a regular pixel shader
        // bound causes stage-signature and missing-render-target errors.
        Renderer::GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
    }
}
