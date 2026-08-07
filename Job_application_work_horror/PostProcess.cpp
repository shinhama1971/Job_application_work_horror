#include "PostProcess.h"
#include "Renderer.h"
#include "Application.h"

#include <algorithm>

namespace Effect
{
    void PostProcess::Init()
    {
        m_EnableNoise = true;
        m_Time = 0.0f;

        m_RenderTexture.Init(
            Application::GetWidth(),
            Application::GetHeight()
        );

        const int bloomWidth = (Application::GetWidth() + 1) / 2;
        const int bloomHeight = (Application::GetHeight() + 1) / 2;
        constexpr DXGI_FORMAT bloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        m_BloomExtractTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);
        m_BloomHorizontalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);
        m_BloomVerticalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);

        m_BloomExtractShader.Create("shader/bloomExtractCS.hlsl");
        m_BloomHorizontalShader.Create("shader/bloomBlurHorizontalCS.hlsl");
        m_BloomVerticalShader.Create("shader/bloomBlurVerticalCS.hlsl");

        m_FullScreenQuad.Init();
    }

    void PostProcess::Uninit()
    {
        m_BloomVerticalShader.Uninit();
        m_BloomHorizontalShader.Uninit();
        m_BloomExtractShader.Uninit();
        m_FullScreenQuad.Uninit();
        m_BloomVerticalTexture.Uninit();
        m_BloomHorizontalTexture.Uninit();
        m_BloomExtractTexture.Uninit();
        m_RenderTexture.Uninit();
    }

    void PostProcess::Update()
    {
        m_Time += 1.0f / 60.0f;

        // Smooth changes so sprinting and battery warnings never pop on screen.
        m_NoiseAmount +=
            (m_TargetNoiseAmount - m_NoiseAmount) * 0.075f;
        m_VignetteStrength +=
            (m_TargetVignetteStrength - m_VignetteStrength) * 0.075f;

        if (m_BloomPulseTimer > 0.0f && m_BloomPulseDuration > 0.0f)
        {
            m_BloomPulseTimer =
                (std::max)(0.0f, m_BloomPulseTimer - 1.0f / 60.0f);
            const float remaining =
                m_BloomPulseTimer / m_BloomPulseDuration;
            m_BloomIntensity = m_BloomBaseIntensity +
                m_BloomPulseStrength * remaining * remaining;
        }
        else
        {
            m_BloomIntensity +=
                (m_BloomBaseIntensity - m_BloomIntensity) * 0.12f;
        }
    }

    void PostProcess::TriggerBloomPulse(float peakIntensity, float duration)
    {
        m_BloomPulseDuration = (std::max)(duration, 0.01f);
        m_BloomPulseTimer = m_BloomPulseDuration;
        m_BloomPulseStrength =
            (std::max)(0.0f, peakIntensity - m_BloomBaseIntensity);
        m_BloomIntensity = m_BloomBaseIntensity + m_BloomPulseStrength;
    }

    void PostProcess::Begin()
    {
        m_RenderTexture.SetRenderTarget();
        m_RenderTexture.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void PostProcess::End()
    {
        Renderer::SetBackBufferRenderTarget();
    }

    void PostProcess::CaptureBackBuffer()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // A resource cannot be copied safely while it is still an output target.
        context->OMSetRenderTargets(0, nullptr, nullptr);

        ID3D11Resource* backBufferResource = nullptr;
        Renderer::GetBackBufferRTV()->GetResource(&backBufferResource);
        context->CopyResource(m_RenderTexture.GetTexture(), backBufferResource);
        SAFE_RELEASE(backBufferResource);

        Renderer::SetBackBufferRenderTarget();
    }

    void PostProcess::RunBloom()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11RenderTargetView* nullRenderTarget = nullptr;
        context->OMSetRenderTargets(1, &nullRenderTarget, nullptr);

        const UINT bloomWidth = static_cast<UINT>(m_BloomExtractTexture.GetWidth());
        const UINT bloomHeight = static_cast<UINT>(m_BloomExtractTexture.GetHeight());

        const auto dispatchPass = [context](
            const ComputeShader& shader,
            ID3D11ShaderResourceView* input,
            ID3D11UnorderedAccessView* output,
            UINT groupX,
            UINT groupY)
        {
            shader.SetGPU();
            context->CSSetShaderResources(0, 1, &input);
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->Dispatch(groupX, groupY, 1);

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            context->CSSetShaderResources(0, 1, &nullSRV);
            context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        };

        dispatchPass(
            m_BloomExtractShader,
            m_RenderTexture.GetSRV(),
            m_BloomExtractTexture.GetUAV(),
            (bloomWidth + 7) / 8,
            (bloomHeight + 7) / 8);

        dispatchPass(
            m_BloomHorizontalShader,
            m_BloomExtractTexture.GetSRV(),
            m_BloomHorizontalTexture.GetUAV(),
            (bloomWidth + 127) / 128,
            bloomHeight);

        dispatchPass(
            m_BloomVerticalShader,
            m_BloomHorizontalTexture.GetSRV(),
            m_BloomVerticalTexture.GetUAV(),
            bloomWidth,
            (bloomHeight + 127) / 128);

        context->CSSetShader(nullptr, nullptr, 0);
        Renderer::SetBackBufferRenderTarget();
    }

    void PostProcess::Draw()
    {
        RunBloom();
        m_FullScreenQuad.Draw(
            m_BloomVerticalTexture.GetSRV(),
            m_Time,
            m_BloomIntensity,
            m_EnableNoise ? m_NoiseAmount : 0.0f,
            m_VignetteStrength);
    }
}
