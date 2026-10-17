// ============================================================================
// ファイルの役割: 水面の反射を比べるための「キューブマップ」。カメラの位置から6方向を撮った画像を、反射の向きで読んでいる。
// 主な技術: キューブマップへの描画、1フレームに1面ずつ描き直す負荷の分散、右手系と左手系の向きの変換
// （説明はReflectionProbe.hを参照）
// ============================================================================

#include "ReflectionProbe.h"

#include "Camera.h"
#include "Renderer.h"
#include "utility.h"

using namespace DirectX::SimpleMath;

namespace
{
    // キューブマップの6面（+X、-X、+Y、-Y、+Z、-Z）を撮るカメラの向きと上の向き。
    // Direct3Dのキューブマップの面は左手系で決まっているが、このゲームの行列は右手系（SimpleMath）なので、
    // z を反転した向きで撮り、読むときも反射の向きの z を反転している（wetFloorPS）。こうすると左右が裏返らない。
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
    // キューブマップを読むテクスチャの番号
    constexpr UINT ProbeSlot = 7;
}

namespace Effect
{
    void ReflectionProbe::Init()
    {
        ID3D11Device* device = Renderer::GetDevice();

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = FaceSize;
        textureDesc.Height = FaceSize;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 6;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        bool created = SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, m_Texture.ReleaseAndGetAddressOf()));

        // 6面それぞれを描画先にする口と、6面まとめてキューブとして読む口
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

        // 1面ずつ描くので、深度バッファは1面分を使い回している
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
            utility::ReportFatalError("反射のキューブマップの作成に失敗しました。");
        }
        m_NextFace = 0;
        m_FacesDrawn = 0;
    }

    void ReflectionProbe::Uninit()
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

    void ReflectionProbe::BeginFace(Camera& camera, const Vector3& position)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        // 描画先にする間は読めないため、先に t7 から外している
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(ProbeSlot, 1, &nullResource);

        ID3D11RenderTargetView* faceView = m_FaceViews[static_cast<size_t>(m_NextFace)].Get();
        context->OMSetRenderTargets(1, &faceView, m_DepthView.Get());
        const float clearColor[4] = { 0.1f, 0.1f, 0.13f, 1.0f };
        context->ClearRenderTargetView(faceView, clearColor);
        context->ClearDepthStencilView(m_DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(FaceSize);
        viewport.Height = static_cast<float>(FaceSize);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        // 撮る位置から面の向きを、画角90度・縦横同じで見ている（6面で全方向をすき間なく覆う）
        const FaceDirection& face = FaceDirections[m_NextFace];
        const Matrix view = Matrix::CreateLookAt(position, position + face.Forward, face.Up);
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            DirectX::XM_PIDIV2, 1.0f, 0.5f, 600.0f);
        camera.SetOverrideMatrices(view, projection);
    }

    void ReflectionProbe::EndFace(Camera& camera)
    {
        camera.ClearOverrideMatrices();
        Renderer::SetBackBufferRenderTarget();
        m_NextFace = (m_NextFace + 1) % 6;
        if (m_FacesDrawn < 6)
        {
            ++m_FacesDrawn;
        }
        Bind();
    }

    void ReflectionProbe::Bind()
    {
        ID3D11ShaderResourceView* cubeView = m_CubeView.Get();
        Renderer::GetDeviceContext()->PSSetShaderResources(ProbeSlot, 1, &cubeView);
    }
}
