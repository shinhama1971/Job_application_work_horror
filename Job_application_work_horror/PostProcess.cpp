// ============================================================================
// ファイルの役割: 露出・ブルーム・ブラウン管風の効果・光の筋など、画面全体に掛けるシェーダーの演出をまとめて管理している。
// 主な技術: Render To Texture、Compute Shader、横と縦に分けたぼかし（2枚のテクスチャを交互に使う）、明るさと色の調整
// ============================================================================

#include "PostProcess.h"
#include "GpuTimer.h"
#include "Renderer.h"
#include "Application.h"

#include <algorithm>
#include <cmath>

namespace Effect
{
    // 作業用のRenderTextureと各シェーダーを一度だけ作っている。
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
        m_LensDirtStrength = 0.10f;
        m_SignalInterference = 0.0f;
        m_TargetSignalInterference = 0.0f;

        m_RenderTexture.Init(
            Application::GetWidth(),
            Application::GetHeight()
        );

        // ブルームは縦横1/4の解像度で処理している。元の画面は拡大しないため、
        // 物の輪郭そのものははっきりしたまま、光っている部分だけが外側へにじむ。
        const int bloomWidth = (Application::GetWidth() + 3) / 4;
        const int bloomHeight = (Application::GetHeight() + 3) / 4;
        // 明るさが1を超える値も残せるよう、16ビットの浮動小数点のテクスチャにしている（コンピュートシェーダーから書けるようUAV付き）
        constexpr DXGI_FORMAT bloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        m_BloomExtractTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);
        m_BloomHorizontalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);
        m_BloomVerticalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true);

        m_BloomExtractShader.Create("shader/bloomExtractCS.hlsl");
        m_BloomHorizontalShader.Create("shader/bloomBlurHorizontalCS.hlsl");
        m_BloomVerticalShader.Create("shader/bloomBlurVerticalCS.hlsl");

        m_FullScreenQuad.Init();
    }

    // 作った物を解放している
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

    // 目標値へゆっくり近づけ、場面が変わったときの露出やノイズの急な変化を防いでいる。
    void PostProcess::Update()
    {
        const float deltaTime = Application::GetDeltaTime();
        // 60fpsで1フレームあたりの近づく割合を、フレームレートが違っても同じ速さになるよう補正している
        const auto response = [deltaTime](float responseAt60Fps)
        {
            return 1.0f - std::pow(
                1.0f - responseAt60Fps, deltaTime * 60.0f);
        };
        m_Time += deltaTime;

        // 走ったときや電池の警告の強さをなめらかにし、画面効果が一瞬で切り替わらないようにしている。
        m_NoiseAmount +=
            (m_TargetNoiseAmount - m_NoiseAmount) * response(0.075f);
        m_VignetteStrength +=
            (m_TargetVignetteStrength - m_VignetteStrength) * response(0.075f);

        // 暗い所へ入った後はゆっくり目を慣らし、懐中電灯や照明が戻ったときは
        // 素早く普段の露出へ戻すことで、人の目の慣れ方に近づけている。
        const float exposureResponse = response(
            m_TargetExposure > m_Exposure ? 0.012f : 0.065f);
        m_Exposure +=
            (m_TargetExposure - m_Exposure) * exposureResponse;
        m_CorridorTension +=
            (m_TargetCorridorTension - m_CorridorTension) * response(0.035f);
        m_VolumetricIntensity +=
            (m_TargetVolumetricIntensity - m_VolumetricIntensity) * response(0.055f);
        m_SignalInterference +=
            (m_TargetSignalInterference - m_SignalInterference) * response(0.085f);

        // ブルームの一時的な強まり：残り時間の2乗で弱めている（始めは急に、終わりはゆっくり戻る）
        if (m_BloomPulseTimer > 0.0f && m_BloomPulseDuration > 0.0f)
        {
            m_BloomPulseTimer =
                (std::max)(0.0f, m_BloomPulseTimer - deltaTime);
            const float remaining =
                m_BloomPulseTimer / m_BloomPulseDuration;
            m_BloomIntensity = m_BloomBaseIntensity +
                m_BloomPulseStrength * remaining * remaining;
        }
        else
        {
            m_BloomIntensity +=
                (m_BloomBaseIntensity - m_BloomIntensity) * response(0.12f);
        }

        // 驚かせる演出の強さ：同じく残り時間の2乗で弱めている
        if (m_HorrorPulseTimer > 0.0f && m_HorrorPulseDuration > 0.0f)
        {
            m_HorrorPulseTimer =
                (std::max)(0.0f, m_HorrorPulseTimer - deltaTime);
            const float remaining =
                m_HorrorPulseTimer / m_HorrorPulseDuration;
            m_HorrorPulseStrength =
                m_HorrorPulsePeak * remaining * remaining;
        }
        else
        {
            m_HorrorPulseStrength +=
                (0.0f - m_HorrorPulseStrength) * response(0.18f);
        }

        // レンズの曇り：なめらかな曲線（smoothstep）で消している
        if (m_LensMoistureTimer > 0.0f && m_LensMoistureDuration > 0.0f)
        {
            m_LensMoistureTimer = (std::max)(
                0.0f, m_LensMoistureTimer - deltaTime);
            const float remaining =
                m_LensMoistureTimer / m_LensMoistureDuration;
            const float easedRemaining = remaining * remaining *
                (3.0f - 2.0f * remaining);
            m_LensMoisture = m_LensMoisturePeak * easedRemaining;
        }
        else
        {
            m_LensMoisture += (0.0f - m_LensMoisture) * response(0.035f);
        }
    }

    // ブルームを一時的に強めている（peakIntensityは普段の強さを含めた最大値）
    void PostProcess::TriggerBloomPulse(float peakIntensity, float duration)
    {
        m_BloomPulseDuration = (std::max)(duration, 0.01f);
        m_BloomPulseTimer = m_BloomPulseDuration;
        m_BloomPulseStrength =
            (std::max)(0.0f, peakIntensity - m_BloomBaseIntensity);
        m_BloomIntensity = m_BloomBaseIntensity + m_BloomPulseStrength;
    }

    // 驚かせる演出を始めている
    void PostProcess::TriggerHorrorPulse(float strength, float duration)
    {
        m_HorrorPulseDuration = (std::max)(duration, 0.01f);
        m_HorrorPulseTimer = m_HorrorPulseDuration;
        m_HorrorPulsePeak = (std::max)(strength, 0.0f);
        m_HorrorPulseStrength = m_HorrorPulsePeak;
    }

    // レンズの曇りを始めている（すでに曇っていれば、強い方を残している）
    void PostProcess::TriggerLensMoisture(float strength, float duration)
    {
        const float clampedStrength = (std::clamp)(strength, 0.0f, 1.0f);
        m_LensMoistureDuration = (std::max)(duration, 0.05f);
        m_LensMoistureTimer = m_LensMoistureDuration;
        m_LensMoisturePeak = (std::max)(m_LensMoisture, clampedStrength);
        m_LensMoisture = m_LensMoisturePeak;
    }

    // 今の作りでは3DのシーンをBackBufferへ描いているため、画面効果の入力として
    // 同じ大きさ・形式の作業用テクスチャへ写している。
    void PostProcess::CaptureBackBuffer()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 描画先に設定している資源は安全に写せないため、描画先から外している。
        context->OMSetRenderTargets(0, nullptr, nullptr);

        Microsoft::WRL::ComPtr<ID3D11Resource> backBufferResource;
        Renderer::GetBackBufferRTV()->GetResource(backBufferResource.GetAddressOf());
        context->CopyResource(m_RenderTexture.GetTexture(), backBufferResource.Get());

        Renderer::SetBackBufferRenderTarget();
    }

    // UAVとSRVを同時に設定しないようにしながら、明るい部分の抽出と、2方向のぼかしを順番に実行している。
    void PostProcess::RunBloom()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11RenderTargetView* nullRenderTarget = nullptr;
        context->OMSetRenderTargets(1, &nullRenderTarget, nullptr);

        const UINT bloomWidth = static_cast<UINT>(m_BloomExtractTexture.GetWidth());
        const UINT bloomHeight = static_cast<UINT>(m_BloomExtractTexture.GetHeight());

        // 1つの段階を実行している：入力をSRV、出力をUAVに設定してDispatchし、終わったら両方を外している
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

        // 明るい部分の抽出（8x8の画素を1グループとして処理している）
        dispatchPass(
            m_BloomExtractShader,
            m_RenderTexture.GetSRV(),
            m_BloomExtractTexture.GetUAV(),
            (bloomWidth + 7) / 8,
            (bloomHeight + 7) / 8);

        // 横のぼかし（1行を128画素ずつのグループで処理している）
        dispatchPass(
            m_BloomHorizontalShader,
            m_BloomExtractTexture.GetSRV(),
            m_BloomHorizontalTexture.GetUAV(),
            (bloomWidth + 127) / 128,
            bloomHeight);

        // 縦のぼかし（1列を128画素ずつのグループで処理している）
        dispatchPass(
            m_BloomVerticalShader,
            m_BloomHorizontalTexture.GetSRV(),
            m_BloomVerticalTexture.GetUAV(),
            bloomWidth,
            (bloomHeight + 127) / 128);

        context->CSSetShader(nullptr, nullptr, 0);
        Renderer::SetBackBufferRenderTarget();
    }

    // ブルームの結果と元の画像を合成し、最後にブラウン管風の効果・色調・光の筋・レンズの汚れを重ねている。
    void PostProcess::Draw(GpuTimer* gpuTimer)
    {
        if (m_EnableBloom)
        {
            if (gpuTimer != nullptr)
            {
                gpuTimer->BeginPass(
                    GpuPass::Bloom, Renderer::GetDeviceContext());
            }
            RunBloom();
            if (gpuTimer != nullptr)
            {
                gpuTimer->EndPass(
                    GpuPass::Bloom, Renderer::GetDeviceContext());
            }
        }
        else if (gpuTimer != nullptr)
        {
            gpuTimer->SkipPass(GpuPass::Bloom);
        }
        // ポーズメニューの「演出の強さ」を、各効果の強さに掛けている
        const float effectScale = m_UserEffectScale;
        const float adjustedBloom = (std::clamp)(
            m_BloomIntensity * (0.72f + effectScale * 0.28f),
            0.0f, 1.25f);
        const float adjustedVignette = (std::clamp)(
            0.45f + (m_VignetteStrength - 0.45f) * effectScale,
            0.0f, 1.0f);
        // 軽い設定では、全画面の光の筋の描画を止めている。ブルームと懐中電灯そのものは
        // 残るため見やすさは変わらず、古いGPUで最も重い追加の描画だけを省ける。
        const float volumeQuality = (std::clamp)(
            (effectScale - 0.75f) / 0.25f, 0.0f, 1.0f);
        m_FullScreenQuad.Draw(
            m_RenderTexture.GetSRV(),
            m_EnableBloom ? m_BloomVerticalTexture.GetSRV() : nullptr,
            m_Time,
            m_EnableBloom ? adjustedBloom : 0.0f,
            m_EnableNoise ? m_NoiseAmount * effectScale : 0.0f,
            adjustedVignette,
            m_EnableVolumetricLight
                ? m_VolumetricIntensity * volumeQuality
                : 0.0f,
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
