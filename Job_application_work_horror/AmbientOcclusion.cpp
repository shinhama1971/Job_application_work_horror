// ============================================================================
// ファイルの役割: 画面空間の環境遮蔽（SSAO）。深度プリパスの深度から、周りの光が届きにくい所を求めている。
// 主な技術: Compute Shaderでの画面全体の計算、深度を見ながらのぼかし
// （説明はAmbientOcclusion.hを参照）
// ============================================================================

#include "AmbientOcclusion.h"

#include "Renderer.h"
#include "utility.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 環境遮蔽をピクセルシェーダーへ渡すテクスチャの番号
    constexpr UINT OcclusionSlot = 15;
    // 調べる半球の半径（ワールドの長さ。約30cm）、暗くする強さ、自分の面で暗くならないための深さのずれ、計算する一番遠い距離
    constexpr float OcclusionRadius = 6.0f;
    constexpr float OcclusionIntensity = 1.35f;
    constexpr float OcclusionBias = 0.35f;
    constexpr float OcclusionMaxDistance = 260.0f;

    // 1チャンネルの、Compute Shaderが書けてピクセルシェーダーが読めるテクスチャを作っている
    bool CreateOcclusionTexture(int width, int height,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
    {
        ID3D11Device* device = Renderer::GetDevice();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf()));
    }
}

namespace Effect
{
    void AmbientOcclusion::Init(int width, int height)
    {
        m_Width = (std::max)(width, 1);
        m_Height = (std::max)(height, 1);
        const bool created =
            CreateOcclusionTexture((m_Width + 1) / 2, (m_Height + 1) / 2, m_RawTexture, m_RawUAV, m_RawSRV) &&
            CreateOcclusionTexture((m_Width + 1) / 2, (m_Height + 1) / 2, m_BlurredTexture, m_BlurredUAV, m_BlurredSRV) &&
            Renderer::CreateConstantBuffer(sizeof(Parameters), m_ParameterBuffer.ReleaseAndGetAddressOf()) &&
            m_OcclusionShader.Create("shader/ambientOcclusionCS.hlsl") &&
            m_BlurShader.Create("shader/ambientOcclusionBlurCS.hlsl");
        if (!created)
        {
            utility::ReportFatalError("画面空間の環境遮蔽（SSAO）の準備に失敗しました。");
        }
    }

    void AmbientOcclusion::Uninit()
    {
        m_OcclusionShader.Uninit();
        m_BlurShader.Uninit();
        m_ParameterBuffer.Reset();
        m_RawSRV.Reset();
        m_RawUAV.Reset();
        m_RawTexture.Reset();
        m_BlurredSRV.Reset();
        m_BlurredUAV.Reset();
        m_BlurredTexture.Reset();
    }

    void AmbientOcclusion::Compute(const Matrix& projection, const D3D11_VIEWPORT& viewport)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        Parameters parameters{};
        parameters.Projection = projection.Transpose();
        parameters.InverseProjection = projection.Invert().Transpose();
        // 描く範囲が作ったテクスチャより大きいときは、テクスチャの大きさまでにしている
        parameters.ViewportSize = Vector2(
            (std::min)(viewport.Width, static_cast<float>(m_Width)),
            (std::min)(viewport.Height, static_cast<float>(m_Height)));
        parameters.ViewportOffset = Vector2(viewport.TopLeftX, viewport.TopLeftY);
        parameters.Radius = OcclusionRadius;
        parameters.Intensity = OcclusionIntensity;
        parameters.Bias = OcclusionBias;
        parameters.MaxDistance = OcclusionMaxDistance;
        // 縦横半分の解像度で計算している（画面全体の1/4の画素数。ぼかすので、半分でも粗さは目立たない）
        parameters.OcclusionSize = Vector2(
            std::ceil(parameters.ViewportSize.x * 0.5f), std::ceil(parameters.ViewportSize.y * 0.5f));
        context->UpdateSubresource(m_ParameterBuffer.Get(), 0, nullptr, &parameters, 0, 0);

        // 書き込む画像を、ピクセルシェーダーの読み取りから外している。深度バッファも、描画先に設定したままでは読めないため外している
        Unbind();
        ID3D11RenderTargetView* backBuffer = Renderer::GetBackBufferRTV();
        context->OMSetRenderTargets(1, &backBuffer, nullptr);

        ID3D11Buffer* parameterBuffer = m_ParameterBuffer.Get();
        context->CSSetConstantBuffers(0, 1, &parameterBuffer);
        const UINT groupsX = (static_cast<UINT>(parameters.OcclusionSize.x) + 7) / 8;
        const UINT groupsY = (static_cast<UINT>(parameters.OcclusionSize.y) + 7) / 8;

        // 1回目：環境遮蔽を求めている（t0 = 深度、u0 = ぼかす前）
        m_OcclusionShader.SetGPU();
        ID3D11ShaderResourceView* depth = Renderer::GetDepthShaderResourceView();
        context->CSSetShaderResources(0, 1, &depth);
        ID3D11UnorderedAccessView* rawUAV = m_RawUAV.Get();
        context->CSSetUnorderedAccessViews(0, 1, &rawUAV, nullptr);
        context->Dispatch(groupsX, groupsY, 1);

        // 2回目：深さを見ながらぼかしている（t0 = 深度、t1 = ぼかす前、u0 = ぼかした後）
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        m_BlurShader.SetGPU();
        ID3D11ShaderResourceView* blurInputs[2] = { depth, m_RawSRV.Get() };
        context->CSSetShaderResources(0, 2, blurInputs);
        ID3D11UnorderedAccessView* blurredUAV = m_BlurredUAV.Get();
        context->CSSetUnorderedAccessViews(0, 1, &blurredUAV, nullptr);
        context->Dispatch(groupsX, groupsY, 1);

        // Compute Shaderから外し、本描画の描画先（色と深度）に戻して、結果を t15 に設定している
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        context->CSSetShaderResources(0, 2, nullSRVs);
        context->CSSetShader(nullptr, nullptr, 0);
        context->OMSetRenderTargets(1, &backBuffer, Renderer::GetDepthStencilView());
        ID3D11ShaderResourceView* blurred = m_BlurredSRV.Get();
        context->PSSetShaderResources(OcclusionSlot, 1, &blurred);
    }

    void AmbientOcclusion::Unbind()
    {
        ID3D11ShaderResourceView* nullResource = nullptr;
        Renderer::GetDeviceContext()->PSSetShaderResources(OcclusionSlot, 1, &nullResource);
    }
}
