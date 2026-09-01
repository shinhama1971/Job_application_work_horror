// ============================================================================
// ファイルの役割: 水面用の平面反射レンダーターゲットと反射カメラを管理します。
// ============================================================================

#include "PlanarReflection.h"

#include "Application.h"
#include "Camera.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace Effect
{
    void PlanarReflection::Init()
    {
		// Half-resolution reflections remove most of this extra scene pass's
		// pixel cost. The slight softness is natural for rippling puddles.
		const uint32_t reflectionWidth =
			(Application::GetWidth() + 1u) / 2u;
		const uint32_t reflectionHeight =
			(Application::GetHeight() + 1u) / 2u;
        m_Texture.Init(
			reflectionWidth,
			reflectionHeight,
            DXGI_FORMAT_R8G8B8A8_UNORM);

        Renderer::CreateConstantBuffer(
            sizeof(ReflectionBuffer),
            m_ReflectionBuffer.ReleaseAndGetAddressOf());

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        Renderer::GetDevice()->CreateRasterizerState(
            &rasterizerDesc,
            m_TwoSidedRasterizer.ReleaseAndGetAddressOf());
    }

    void PlanarReflection::Uninit()
    {
        m_PreviousRasterizer.Reset();
        m_TwoSidedRasterizer.Reset();
        m_ReflectionBuffer.Reset();
        m_Texture.Uninit();
    }

    void PlanarReflection::Begin(
        Camera& camera,
        float reflectionHeight)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // The texture cannot be sampled while it is used as a render target.
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(6, 1, &nullResource);

        m_Texture.SetRenderTarget();
        m_Texture.Clear(0.008f, 0.010f, 0.013f, 1.0f);

        context->RSGetState(m_PreviousRasterizer.ReleaseAndGetAddressOf());
        context->RSSetState(m_TwoSidedRasterizer.Get());

        Vector3 reflectedPosition = camera.GetPosition();
        reflectedPosition.y =
            reflectionHeight * 2.0f - reflectedPosition.y;

        Vector3 reflectedForward = camera.GetForward();
        reflectedForward.y = -reflectedForward.y;
        reflectedForward.Normalize();

        const Matrix reflectionView = Matrix::CreateLookAt(
            reflectedPosition,
            reflectedPosition + reflectedForward,
            Vector3(0.0f, -1.0f, 0.0f));

        const float aspectRatio =
            static_cast<float>(Application::GetWidth()) /
            static_cast<float>(Application::GetHeight());
        const Matrix reflectionProjection =
            Matrix::CreatePerspectiveFieldOfView(
                DirectX::XMConvertToRadians(60.0f),
                aspectRatio,
                1.0f,
                1000.0f);

        ReflectionBuffer buffer{};
        buffer.ViewProjection =
            (reflectionView * reflectionProjection).Transpose();
        context->UpdateSubresource(
            m_ReflectionBuffer.Get(),
            0,
            nullptr,
            &buffer,
            0,
            0);
        ID3D11Buffer* reflectionBuffer = m_ReflectionBuffer.Get();
        context->VSSetConstantBuffers(9, 1, &reflectionBuffer);

        camera.SetOverrideMatrices(
            reflectionView,
            reflectionProjection);
    }

    void PlanarReflection::End(Camera& camera)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        camera.ClearOverrideMatrices();
        Renderer::SetBackBufferRenderTarget();

        context->RSSetState(m_PreviousRasterizer.Get());
        m_PreviousRasterizer.Reset();

        ID3D11ShaderResourceView* reflectionResource =
            m_Texture.GetSRV();
        context->PSSetShaderResources(
            6,
            1,
            &reflectionResource);
    }

    void PlanarReflection::Bind()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* reflectionResource =
            m_Texture.GetSRV();
        context->PSSetShaderResources(
            6,
            1,
            &reflectionResource);
    }
}
