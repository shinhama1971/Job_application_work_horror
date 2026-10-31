// ============================================================================
// ファイルの役割: 揺れる裸電球（2面）の影に使う、電球から6方向を撮った「電球からの距離」のキューブマップを作っている。
// 主な技術: 点光源の影（全方向のシャドウマップ）、キューブマップの各面への描画、距離を色として書く方式
// （説明はPointShadowMap.hを参照）
// ============================================================================

#include "PointShadowMap.h"

#include "Renderer.h"
#include "utility.h"

using namespace DirectX::SimpleMath;

namespace
{
    // キューブマップの6面（+X、-X、+Y、-Y、+Z、-Z）を撮る向きと上の向き。
    // ReflectionProbe と同じく、z を反転した向きで撮り、読むときも z を反転している（pointShadow.hlsli）。
    struct FaceDirection
    {
        Vector3 Forward;
        Vector3 Up;
    };
    const FaceDirection FaceDirections[6] =
    {
        { Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f) },
        { Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f) },
        { Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) },
        { Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f) },
        { Vector3(0.0f, 0.0f, -1.0f), Vector3(0.0f, 1.0f, 0.0f) },
        { Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 1.0f, 0.0f) },
    };
    // キューブマップを設定するテクスチャの番号
    constexpr UINT ShadowSlot = 2;
}

namespace Effect
{
    void PointShadowMap::Init()
    {
        ID3D11Device* device = Renderer::GetDevice();

        // 距離を1つの値（32ビットの小数）で書く、6面のテクスチャ
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = FaceSize;
        textureDesc.Height = FaceSize;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 6;
        textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        bool created = SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, m_Texture.ReleaseAndGetAddressOf()));

        for (UINT face = 0; face < 6 && created; ++face)
        {
            D3D11_RENDER_TARGET_VIEW_DESC faceDesc{};
            faceDesc.Format = textureDesc.Format;
            faceDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            faceDesc.Texture2DArray.FirstArraySlice = face;
            faceDesc.Texture2DArray.ArraySize = 1;
            created = SUCCEEDED(device->CreateRenderTargetView(
                m_Texture.Get(), &faceDesc, m_FaceViews[face].ReleaseAndGetAddressOf()));
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC cubeDesc{};
        cubeDesc.Format = textureDesc.Format;
        cubeDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        cubeDesc.TextureCube.MipLevels = 1;
        created = created && SUCCEEDED(device->CreateShaderResourceView(
            m_Texture.Get(), &cubeDesc, m_CubeView.ReleaseAndGetAddressOf()));

        // 手前の物だけを残すための深度バッファ（1面ずつ描くので、1面分を使い回している）
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = FaceSize;
        depthDesc.Height = FaceSize;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        created = created &&
            SUCCEEDED(device->CreateTexture2D(&depthDesc, nullptr, m_DepthTexture.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateDepthStencilView(m_DepthTexture.Get(), nullptr, m_DepthView.ReleaseAndGetAddressOf()));
        if (!created)
        {
            utility::ReportFatalError("電球の影のキューブマップの作成に失敗しました。");
        }

        m_Shader.Create("shader/pointShadowVS.hlsl", "shader/pointShadowPS.hlsl");
    }

    void PointShadowMap::Uninit()
    {
        for (auto& view : m_FaceViews)
        {
            view.Reset();
        }
        m_CubeView.Reset();
        m_Texture.Reset();
        m_DepthView.Reset();
        m_DepthTexture.Reset();
    }

    void PointShadowMap::BeginFace(int face, const Vector3& lightPosition)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        if (!m_HasPreviousViewport)
        {
            // 描画先にする間は読めないため、先に t2 から外し、元のビューポートを覚えている
            Unbind();
            m_PreviousViewportCount = 1;
            context->RSGetViewports(&m_PreviousViewportCount, &m_PreviousViewport);
            m_HasPreviousViewport = true;
        }

        ID3D11RenderTargetView* faceView = m_FaceViews[static_cast<size_t>(face)].Get();
        context->OMSetRenderTargets(1, &faceView, m_DepthView.Get());
        // 何も描かれない所は、光の届く範囲の端（1）にしている（＝その向きには影がない）
        const float farthest[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        context->ClearRenderTargetView(faceView, farthest);
        context->ClearDepthStencilView(m_DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(FaceSize);
        viewport.Height = static_cast<float>(FaceSize);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        // 電球の位置から面の向きを、画角90度・縦横同じで見ている（6面で全方向をすき間なく覆う）
        const FaceDirection& direction = FaceDirections[face];
        Matrix view = Matrix::CreateLookAt(lightPosition, lightPosition + direction.Forward, direction.Up);
        Matrix projection = Matrix::CreatePerspectiveFieldOfView(DirectX::XM_PIDIV2, 1.0f, 0.5f, 400.0f);
        Renderer::SetViewMatrix(&view);
        Renderer::SetProjectionMatrix(&projection);
    }

    void PointShadowMap::End()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        Renderer::SetBackBufferRenderTarget();
        if (m_HasPreviousViewport)
        {
            context->RSSetViewports(m_PreviousViewportCount, &m_PreviousViewport);
            m_HasPreviousViewport = false;
        }
        ID3D11ShaderResourceView* cubeView = m_CubeView.Get();
        context->PSSetShaderResources(ShadowSlot, 1, &cubeView);
    }

    void PointShadowMap::Unbind()
    {
        ID3D11ShaderResourceView* nullResource = nullptr;
        Renderer::GetDeviceContext()->PSSetShaderResources(ShadowSlot, 1, &nullResource);
    }
}
