#include "CorridorFloodWave.h"
#include "Renderer.h"
#include "Utility.h"
#include "shader/corridorFloodWaveConfig.hlsli"

#include <cstddef>
#include <utility>

namespace
{
    struct alignas(16) WaveConstants
    {
        DirectX::SimpleMath::Vector4 Shape;
        DirectX::SimpleMath::Vector4 Refraction;
    };
    static_assert(sizeof(WaveConstants) == 32);
    static_assert(offsetof(WaveConstants, Refraction) == 16);
    constexpr UINT BackgroundSlot = SIGNAL_LOST_FLOOD_WAVE_BACKGROUND_SLOT;
    constexpr UINT BufferSlot = SIGNAL_LOST_FLOOD_WAVE_BUFFER_SLOT;
    constexpr UINT VertexCount = SIGNAL_LOST_FLOOD_WAVE_COLUMNS *
        SIGNAL_LOST_FLOOD_WAVE_ROWS * SIGNAL_LOST_FLOOD_WAVE_VERTICES_PER_CELL;
    constexpr UINT SprayVertexCount = SIGNAL_LOST_FLOOD_SPRAY_COUNT * SIGNAL_LOST_FLOOD_SPRAY_VERTICES_PER_PARTICLE;
}

void CorridorFloodWave::SetFront(float distance, float crestStrength)
{
    m_Distance = distance;
    m_CrestStrength = crestStrength;
}

DirectX::SimpleMath::Vector4 CorridorFloodWave::CaptureBackground()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Resource> resource;
    Renderer::GetBackBufferRTV()->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(resource.As(&source)))
    {
        return {};
    }
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    if (description.SampleDesc.Count != 1)
    {
        return {};
    }

    if (!m_BackgroundView || description.Width != m_BackgroundDescription.Width ||
        description.Height != m_BackgroundDescription.Height || description.Format != m_BackgroundDescription.Format)
    {
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> background;
        ComPtr<ID3D11ShaderResourceView> view;
        auto* device = Renderer::GetDevice();
        if (FAILED(device->CreateTexture2D(&description, nullptr, background.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(background.Get(), nullptr, view.GetAddressOf())))
        {
            return {};
        }
        m_Background = std::move(background);
        m_BackgroundView = std::move(view);
        m_BackgroundDescription = description;
    }

    // 波を描く前のHDR画面を別のテクスチャに写し、元の描画先と深度を戻す。
    auto* context = Renderer::GetDeviceContext();
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->CopyResource(m_Background.Get(), source.Get());
    Renderer::SetBackBufferRenderTarget();
    return { 1.0f / static_cast<float>(description.Width), 1.0f / static_cast<float>(description.Height), 1.0f, 0.0f };
}

void CorridorFloodWave::Draw(float floorY, float time)
{
    if (m_CrestStrength <= 0.0f || m_Distance <= 0.0f)
    {
        return;
    }
    if (!m_Constants)
    {
        m_Shader.CreateRequired("shader/corridorFloodWaveVS.hlsl", "shader/corridorFloodWavePS.hlsl", true);
        Renderer::CreateRequiredConstantBuffer(sizeof(WaveConstants),
            m_Constants.GetAddressOf(), "1面の濁流の先端");
        m_SprayShader.CreateRequired("shader/corridorFloodSprayVS.hlsl", "shader/corridorFloodSprayPS.hlsl", true);
        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        if (FAILED(Renderer::GetDevice()->CreateRasterizerState(&rasterizerDesc, m_TwoSidedRasterizer.ReleaseAndGetAddressOf())))
        {
            Utility::ReportFatalError("1面の濁流のしぶきのラスタライザーの設定の作成に失敗しました。");
        }
    }

    const auto refraction = CaptureBackground();
    const WaveConstants data{ { floorY, time, m_CrestStrength, m_Distance }, refraction };
    auto* context = Renderer::GetDeviceContext();
    context->UpdateSubresource(m_Constants.Get(), 0, nullptr, &data, 0, 0);
    context->VSSetConstantBuffers(BufferSlot, 1, m_Constants.GetAddressOf());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    context->PSSetConstantBuffers(BufferSlot, 1, m_Constants.GetAddressOf());
    auto* background = refraction.z > 0.0f ? m_BackgroundView.Get() : nullptr;
    context->PSSetShaderResources(BackgroundSlot, 1, &background);
    // 三角形の位置は頂点番号だけで決まるので、毎フレーム頂点データを送る必要がない。
    context->Draw(VertexCount, 0);
    background = nullptr;
    context->PSSetShaderResources(BackgroundSlot, 1, &background);
    DrawSpray();
}

void CorridorFloodWave::DrawSpray()
{
    // 波と同じ定数（波の形・時刻・強さ）を使う。定数バッファは Draw で設定済み
    auto* context = Renderer::GetDeviceContext();
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
    context->RSGetState(previousRasterizer.GetAddressOf());
    context->RSSetState(m_TwoSidedRasterizer.Get());
    m_SprayShader.SetGPU();
    // 半透明の合成で、深度の判定はするが書き込まない（壁の奥の粒は隠れ、粒どうしは重なる）
    Renderer::SetBlendState(BS_ALPHABLEND);
    Renderer::SetDepthEnable(false);
    context->Draw(SprayVertexCount, 0);
    Renderer::SetDepthEnable(true);
    Renderer::SetBlendState(BS_NONE);
    context->RSSetState(previousRasterizer.Get());
}

void CorridorFloodWave::Reset()
{
    m_CrestStrength = 0.0f;
    m_BackgroundView.Reset();
    m_Background.Reset();
    m_Constants.Reset();
    m_TwoSidedRasterizer.Reset();
    m_Shader = Shader{};
    m_SprayShader = Shader{};
}
