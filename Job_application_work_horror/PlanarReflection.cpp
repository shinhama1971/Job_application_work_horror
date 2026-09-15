// ============================================================================
// ファイルの役割: 水面用の平面反射レンダーターゲットと反射カメラを管理します。
// 主な技術: Render To Texture、反射行列、クリップ平面、SRV/RTV競合回避
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "PlanarReflection.h"

#include "Application.h"
#include "Camera.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace Effect
{
    // 処理内容: 必要な状態とGPU・音声リソースを初期化します。
    void PlanarReflection::Init()
    {
		// 水面には波の歪みとフレネル反射が掛かる。
		// 各辺を1/6にし、反射パスの画素数を約1/36に抑えます。
		constexpr uint32_t reflectionDivisor = 6u;
		const uint32_t reflectionWidth =
			(Application::GetWidth() + reflectionDivisor - 1u) / reflectionDivisor;
		const uint32_t reflectionHeight =
			(Application::GetHeight() + reflectionDivisor - 1u) / reflectionDivisor;

        m_Texture.Init(
			reflectionWidth,
			reflectionHeight,
            DXGI_FORMAT_R8G8B8A8_UNORM);

        Renderer::CreateConstantBuffer(
            sizeof(ReflectionBuffer),
            m_ReflectionBuffer.ReleaseAndGetAddressOf());

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        // 各立体は表裏の面を持つ
        // 以前のCULL_NONEは同じ面の逆巻き三角形まで処理。
        rasterizerDesc.CullMode = D3D11_CULL_BACK;
        rasterizerDesc.DepthClipEnable = TRUE;
        Renderer::GetDevice()->CreateRasterizerState(
            &rasterizerDesc,
            m_TwoSidedRasterizer.ReleaseAndGetAddressOf());
    }

    // 処理内容: 所有するリソースを依存関係の逆順で解放します。
    void PlanarReflection::Uninit()
    {
        m_PreviousRasterizer.Reset();
        m_TwoSidedRasterizer.Reset();
        m_ReflectionBuffer.Reset();
        m_Texture.Uninit();
    }

    // 処理内容: 処理区間を開始し、必要な状態を設定します。
    void PlanarReflection::Begin(
        Camera& camera,
        float reflectionHeight)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 同じテクスチャを描画先として使用中にサンプリングできないため、事前にSRVを解除します。
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(6, 1, &nullResource);

        m_Texture.SetRenderTarget();
        m_Texture.Clear(0.1f, 0.1f, 0.13f, 1.0f);

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

    // 処理内容: 処理区間を終了し、変更した状態を戻します。
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

    // 処理内容: PlanarReflectionの「Bind」処理を担当します。
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
