// ============================================================================
// ファイルの役割: 懐中電灯（ライト）から見た深度を描き、影の判定に使うテクスチャを作っている。
// 主な技術: 深度テクスチャ、比較サンプラー、ライトの行列、深度だけを書く描画
// ============================================================================

#include "ShadowMap.h"

#include "Camera.h"

using namespace DirectX::SimpleMath;

namespace Effect
{
    // 深度テクスチャと、それを読むための仕組みを作っている
    void ShadowMap::Init()
    {
        ID3D11Device* device = Renderer::GetDevice();

        // 深度として書き込み、シェーダーからも読めるよう、形式を決めずに（TYPELESS）作っている
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

        // 書き込むときは32ビットの深度として扱っている
        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            m_Texture.Get(),
            &depthViewDesc,
            m_DepthView.ReleaseAndGetAddressOf());

        // 読むときは32ビットの浮動小数点として扱っている
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDesc{};
        resourceViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
        resourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceViewDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            m_Texture.Get(),
            &resourceViewDesc,
            m_ShaderResourceView.ReleaseAndGetAddressOf());

        // 比較サンプラー：テクスチャの深度と比べた結果（影かどうか）を、周りと混ぜて柔らかく返している。範囲の外は「影ではない」にしている
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

        // 影を描くときは深度を少しずらし（バイアス）、面が自分自身の影で黒くなる「シャドウアクネ」を防いでいる
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

    // 作った物を解放している
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

    // 影を描き始めている
    void ShadowMap::Begin(const Camera& camera)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 影のテクスチャを読み取りに使っている間は描画先にできないため、先にt5から外している
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(5, 1, &nullResource);

        // 今のビューポートとラスタライザーを覚えておき、影用に差し替えている
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

        // 描画先を深度テクスチャだけにし、一番奥（1.0）で消している
        context->OMSetRenderTargets(0, nullptr, m_DepthView.Get());
        context->ClearDepthStencilView(
            m_DepthView.Get(),
            D3D11_CLEAR_DEPTH,
            1.0f,
            0);

        // ライトの位置と向き＝カメラの位置と向き（懐中電灯は目の位置から照らしている）
        const Vector3 lightPosition = camera.GetPosition();
        const Vector3 lightForward = camera.GetForward();
        const Matrix lightView = Matrix::CreateLookAt(
            lightPosition,
            lightPosition + lightForward,
            Vector3::Up);
        // 画角66度（懐中電灯の円錐より少し広い）、2〜280の範囲で影を作っている
        const Matrix lightProjection = Matrix::CreatePerspectiveFieldOfView(
            DirectX::XMConvertToRadians(66.0f),
            1.0f,
            2.0f,
            280.0f);

        // 行列とパラメーターをシェーダーの形にしてb8へ送っている
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

    // 描画先・ビューポート・ラスタライザーを元に戻し、影のテクスチャと比較サンプラーを設定している
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

    // 前に描いた影のテクスチャと比較サンプラーを設定している
    void ShadowMap::Bind()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* shadowResource =
            m_ShaderResourceView.Get();
        ID3D11SamplerState* comparisonSampler =
            m_ComparisonSampler.Get();
        context->PSSetShaderResources(5, 1, &shadowResource);
        context->PSSetSamplers(1, 1, &comparisonSampler);
    }

    // 深度だけを書く頂点シェーダーを設定している
    void ShadowMap::SetShader()
    {
        m_DepthShader.SetGPU();

        // 影の描画は深度だけを書き込んでいる。普通のピクセルシェーダーが残っていると、
        // 入出力の形や描画先の不足でエラーになるため、ピクセルシェーダーを外している。
        Renderer::GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
    }
}
