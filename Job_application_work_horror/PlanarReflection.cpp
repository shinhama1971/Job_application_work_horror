// ============================================================================
// ファイルの役割: 平面反射のレンダーターゲットと、反射を描くカメラを管理している（床の水面と、2面の壁の鏡）。
// 主な技術: 低い解像度へのRender To Texture、面で反転したカメラ、SRVとRTVの同時使用の回避、描画状態の復元
// ============================================================================

#include "PlanarReflection.h"

#include "Application.h"
#include "Camera.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace Effect
{
    void PlanarReflection::Init(unsigned int textureSlot, unsigned int bufferSlot, bool bufferInPixelShader)
    {
        m_TextureSlot = textureSlot;
        m_BufferSlot = bufferSlot;
        m_BufferInPixelShader = bufferInPixelShader;

		// 水面には波の歪みとフレネル反射が掛かるため、反射は細かく描かなくても目立たない。
		// 縦横を1/2にし、反射の描画の画素数を約1/4に抑えている。
		constexpr uint32_t reflectionDivisor = 2u;
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
        // 作る立体は表と裏の両方の面を持つため、背面カリングを使える。
        // 前のCULL_NONEで、裏向きの面まで描いていた分の負荷を避けている。
        rasterizerDesc.CullMode = D3D11_CULL_BACK;
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
        BeginPlane(camera, Vector3(0.0f, reflectionHeight, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    }

    void PlanarReflection::BeginPlane(
        Camera& camera,
        const Vector3& planePoint,
        const Vector3& planeNormal)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 同じテクスチャを描画先にしている間は読み取れないため、先にスロットからSRVを外している。
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(m_TextureSlot, 1, &nullResource);

        // 描画先を反射のテクスチャにし、暗い青灰色で消している
        m_Texture.SetRenderTarget();
        m_Texture.Clear(0.1f, 0.1f, 0.13f, 1.0f);

        // 今のラスタライザーを覚えておき、反射用に差し替えている
        context->RSGetState(m_PreviousRasterizer.ReleaseAndGetAddressOf());
        context->RSSetState(m_TwoSidedRasterizer.Get());

        // カメラの位置・向き・上方向を、面で反転している（床なら上下、壁の鏡なら左右と前後が反転する）。
        // 床の面（上向き）では、前と同じく高さだけ反転し、上方向は下向きになる。
        Vector3 normal = planeNormal;
        normal.Normalize();
        const auto reflectPoint = [&planePoint, &normal](const Vector3& point)
        {
            return point - normal * (2.0f * (point - planePoint).Dot(normal));
        };
        const auto reflectDirection = [&normal](const Vector3& direction)
        {
            return direction - normal * (2.0f * direction.Dot(normal));
        };
        const Vector3 reflectedPosition = reflectPoint(camera.GetPosition());
        Vector3 reflectedForward = reflectDirection(camera.GetForward());
        reflectedForward.Normalize();
        const Vector3 reflectedUp = reflectDirection(Vector3(0.0f, 1.0f, 0.0f));

        const Matrix reflectionView = Matrix::CreateLookAt(
            reflectedPosition,
            reflectedPosition + reflectedForward,
            reflectedUp);

        // 射影はプレイヤーの視点と同じ（画角60度・1〜1000）
        const float aspectRatio =
            static_cast<float>(Application::GetWidth()) /
            static_cast<float>(Application::GetHeight());
        const Matrix reflectionProjection =
            Matrix::CreatePerspectiveFieldOfView(
                DirectX::XMConvertToRadians(60.0f),
                aspectRatio,
                1.0f,
                1000.0f);

        // 反射カメラの行列を、シェーダーの形（転置）にして定数バッファへ送っている
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
        if (m_BufferInPixelShader)
        {
            context->PSSetConstantBuffers(m_BufferSlot, 1, &reflectionBuffer);
        }
        else
        {
            context->VSSetConstantBuffers(m_BufferSlot, 1, &reflectionBuffer);
        }

        // この間に描かれるObjectが反射カメラの行列を使うよう、カメラの行列を差し替えている
        camera.SetOverrideMatrices(
            reflectionView,
            reflectionProjection);
    }

    void PlanarReflection::End(Camera& camera)
    {
        // カメラの行列と描画先をバックバッファに戻し、ラスタライザーも元に戻している
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        camera.ClearOverrideMatrices();
        Renderer::SetBackBufferRenderTarget();

        context->RSSetState(m_PreviousRasterizer.Get());
        m_PreviousRasterizer.Reset();

        // 描いた反射を、床や鏡のシェーダーが読めるよう設定している
        BindResources();
    }

    // 前に描いた反射のテクスチャと行列を、そのまま設定している
    void PlanarReflection::Bind()
    {
        BindResources();
    }

    void PlanarReflection::BindResources()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* reflectionResource =
            m_Texture.GetSRV();
        context->PSSetShaderResources(
            m_TextureSlot,
            1,
            &reflectionResource);
        // 別の面の反射が同じスロットの行列を書き換えていても、読む前に自分の行列へ戻している
        ID3D11Buffer* reflectionBuffer = m_ReflectionBuffer.Get();
        if (m_BufferInPixelShader)
        {
            context->PSSetConstantBuffers(m_BufferSlot, 1, &reflectionBuffer);
        }
        else
        {
            context->VSSetConstantBuffers(m_BufferSlot, 1, &reflectionBuffer);
        }
    }
}
