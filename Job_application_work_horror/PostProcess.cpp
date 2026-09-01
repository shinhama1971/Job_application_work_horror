// ============================================================================
// ファイルの役割: 露出、ブルーム、CRT、霧など画面全体のシェーダー演出を統括します。
// ============================================================================

#include "PostProcess.h"
#include "Renderer.h"
#include "Application.h"

#include <algorithm>

namespace Effect
{
    // 中間RenderTextureと各シェーダーを一度だけ生成します。
    void PostProcess::Init()
    {
        m_EnableNoise = true;
        m_EnableBloom = true;
        m_Time = 0.0f;
        m_NoiseAmount = 0.18f;
        m_TargetNoiseAmount = 0.18f;
        m_VignetteStrength = 0.55f;
        m_TargetVignetteStrength = 0.55f;
        m_HorrorPulseStrength = 0.0f;
        m_HorrorPulseTimer = 0.0f;
        m_Exposure = 1.0f;
        m_TargetExposure = 1.0f;
        m_LensMoisture = 0.0f;
        m_LensMoisturePeak = 0.0f;
        m_LensMoistureTimer = 0.0f;
        m_LensMoistureDuration = 0.0f;
        m_CorridorTension = 0.0f;
        m_TargetCorridorTension = 0.0f;
        m_VolumetricIntensity = 0.58f;
        m_TargetVolumetricIntensity = 0.58f;
        m_FilmGradeStrength = 0.55f;
        m_LensDirtStrength = 0.16f;
        m_SignalInterference = 0.0f;
        m_TargetSignalInterference = 0.0f;

        m_RenderTexture.Init(
            Application::GetWidth(),
            Application::GetHeight()
        );

        // Bloom is intentionally soft, so quarter-resolution processing keeps
        // its appearance while reducing the three compute passes to one
        // quarter of their previous pixel count.
        const int bloomWidth = (Application::GetWidth() + 3) / 4;
        const int bloomHeight = (Application::GetHeight() + 3) / 4;
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

    // target値へ緩やかに補間し、場面転換時の露出やノイズの急変を防ぎます。
    void PostProcess::Update()
    {
        m_Time += 1.0f / 60.0f;

        // Smooth changes so sprinting and battery warnings never pop on screen.
        m_NoiseAmount +=
            (m_TargetNoiseAmount - m_NoiseAmount) * 0.075f;
        m_VignetteStrength +=
            (m_TargetVignetteStrength - m_VignetteStrength) * 0.075f;

        // Human vision adjusts slowly after entering darkness, but recovers
        // quickly when the flashlight or ceiling lights return.
        const float exposureResponse =
            m_TargetExposure > m_Exposure ? 0.012f : 0.065f;
        m_Exposure +=
            (m_TargetExposure - m_Exposure) * exposureResponse;
        m_CorridorTension +=
            (m_TargetCorridorTension - m_CorridorTension) * 0.035f;
        m_VolumetricIntensity +=
            (m_TargetVolumetricIntensity - m_VolumetricIntensity) * 0.055f;
        m_SignalInterference +=
            (m_TargetSignalInterference - m_SignalInterference) * 0.085f;

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

        if (m_HorrorPulseTimer > 0.0f && m_HorrorPulseDuration > 0.0f)
        {
            m_HorrorPulseTimer =
                (std::max)(0.0f, m_HorrorPulseTimer - 1.0f / 60.0f);
            const float remaining =
                m_HorrorPulseTimer / m_HorrorPulseDuration;
            m_HorrorPulseStrength =
                m_HorrorPulsePeak * remaining * remaining;
        }
        else
        {
            m_HorrorPulseStrength +=
                (0.0f - m_HorrorPulseStrength) * 0.18f;
        }

        if (m_LensMoistureTimer > 0.0f && m_LensMoistureDuration > 0.0f)
        {
            m_LensMoistureTimer = (std::max)(
                0.0f, m_LensMoistureTimer - 1.0f / 60.0f);
            const float remaining =
                m_LensMoistureTimer / m_LensMoistureDuration;
            const float easedRemaining = remaining * remaining *
                (3.0f - 2.0f * remaining);
            m_LensMoisture = m_LensMoisturePeak * easedRemaining;
        }
        else
        {
            m_LensMoisture += (0.0f - m_LensMoisture) * 0.035f;
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

    void PostProcess::TriggerHorrorPulse(float strength, float duration)
    {
        m_HorrorPulseDuration = (std::max)(duration, 0.01f);
        m_HorrorPulseTimer = m_HorrorPulseDuration;
        m_HorrorPulsePeak = (std::max)(strength, 0.0f);
        m_HorrorPulseStrength = m_HorrorPulsePeak;
    }

    void PostProcess::TriggerLensMoisture(float strength, float duration)
    {
        const float clampedStrength = (std::clamp)(strength, 0.0f, 1.0f);
        m_LensMoistureDuration = (std::max)(duration, 0.05f);
        m_LensMoistureTimer = m_LensMoistureDuration;
        m_LensMoisturePeak = (std::max)(m_LensMoisture, clampedStrength);
        m_LensMoisture = m_LensMoisturePeak;
    }

    // 3Dシーンの描画先を画面ではなく中間テクスチャへ切り替えます。
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

    // UAV/SRVの同時バインドを避けながら、抽出と2方向ぼかしを順番に実行します。
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

    // ブルーム結果と元画像を合成し、最後にCRT・色調・霧・レンズ汚れを適用します。
    void PostProcess::Draw()
    {
        if (m_EnableBloom)
        {
            RunBloom();
        }
        const float effectScale = m_UserEffectScale;
        const float adjustedBloom = (std::clamp)(
            m_BloomIntensity * (0.65f + effectScale * 0.35f),
            0.0f, 1.5f);
        const float adjustedVignette = (std::clamp)(
            0.45f + (m_VignetteStrength - 0.45f) * effectScale,
            0.0f, 1.0f);
        m_FullScreenQuad.Draw(
            m_RenderTexture.GetSRV(),
            m_EnableBloom ? m_BloomVerticalTexture.GetSRV() : nullptr,
            m_Time,
            m_EnableBloom ? adjustedBloom : 0.0f,
            m_EnableNoise ? m_NoiseAmount * effectScale : 0.0f,
            adjustedVignette,
            m_EnableVolumetricLight ? m_VolumetricIntensity : 0.0f,
            m_LensDistortionStrength * effectScale,
            m_HorrorPulseStrength * effectScale,
            (std::clamp)(
                m_Exposure + m_UserBrightnessOffset, 0.75f, 1.34f),
            m_LensMoisture,
            (std::clamp)(m_CorridorTension * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_FilmGradeStrength * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_LensDirtStrength * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_SignalInterference * effectScale, 0.0f, 1.0f));
    }
}
