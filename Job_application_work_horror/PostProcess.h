#pragma once

#include <algorithm>

#include "RenderTexture.h"
#include "FullScreenQuad.h"
#include "ComputeShader.h"

namespace Effect
{
    class PostProcess
    {
    private:
        bool m_EnableNoise = true;
        bool m_EnableBloom = true;
        bool m_EnableVolumetricLight = false;

        float m_Time = 0.0f;
        float m_BloomBaseIntensity = 0.72f;
        float m_BloomIntensity = 0.72f;
        float m_BloomPulseStrength = 0.0f;
        float m_BloomPulseTimer = 0.0f;
        float m_BloomPulseDuration = 0.0f;
        float m_NoiseAmount = 0.18f;
        float m_TargetNoiseAmount = 0.18f;
        float m_VignetteStrength = 0.55f;
        float m_TargetVignetteStrength = 0.55f;
        float m_LensDistortionStrength = 0.32f;
        float m_HorrorPulseStrength = 0.0f;
        float m_HorrorPulsePeak = 0.0f;
        float m_HorrorPulseTimer = 0.0f;
        float m_HorrorPulseDuration = 0.0f;
        float m_Exposure = 1.0f;
        float m_TargetExposure = 1.0f;
        float m_LensMoisture = 0.0f;
        float m_LensMoisturePeak = 0.0f;
        float m_LensMoistureTimer = 0.0f;
        float m_LensMoistureDuration = 0.0f;
        float m_CorridorTension = 0.0f;
        float m_TargetCorridorTension = 0.0f;
        float m_VolumetricIntensity = 0.58f;
        float m_TargetVolumetricIntensity = 0.58f;
        float m_FilmGradeStrength = 0.55f;
        float m_LensDirtStrength = 0.35f;

        Graphics::RenderTexture m_RenderTexture;
        Graphics::RenderTexture m_BloomExtractTexture;
        Graphics::RenderTexture m_BloomHorizontalTexture;
        Graphics::RenderTexture m_BloomVerticalTexture;
        Graphics::FullScreenQuad m_FullScreenQuad;

        ComputeShader m_BloomExtractShader;
        ComputeShader m_BloomHorizontalShader;
        ComputeShader m_BloomVerticalShader;

        void RunBloom();

    public:
        void Init();
        void Uninit();
        void Update();

        void Begin();
        void End();
        void CaptureBackBuffer();
        void Draw();

        void TriggerBloomPulse(float peakIntensity, float duration);
        void TriggerHorrorPulse(float strength, float duration);
        void TriggerLensMoisture(float strength, float duration);

        void SetCorridorTension(float tension)
        {
            m_TargetCorridorTension = (std::clamp)(tension, 0.0f, 1.0f);
        }

        void SetAtmosphere(float noiseAmount, float vignetteStrength)
        {
            m_TargetNoiseAmount = noiseAmount;
            m_TargetVignetteStrength = vignetteStrength;
        }

        void SetVolumetricLight(bool enable)
        {
            m_EnableVolumetricLight = enable;
        }

        void SetVolumetricIntensity(float intensity)
        {
            m_TargetVolumetricIntensity =
                (std::clamp)(intensity, 0.0f, 1.0f);
        }

        void SetBloomEnabled(bool enable)
        {
            m_EnableBloom = enable;
        }

        void SetNoise(bool enable)
        {
            m_EnableNoise = enable;
        }

        void SetExposure(float exposure)
        {
            m_TargetExposure = (std::clamp)(exposure, 0.85f, 1.20f);
        }

        void SetLensDistortionStrength(float strength)
        {
            m_LensDistortionStrength = (std::clamp)(strength, 0.0f, 0.80f);
        }

        void SetFilmGradeStrength(float strength)
        {
            m_FilmGradeStrength = (std::clamp)(strength, 0.0f, 1.0f);
        }

        void SetLensDirtStrength(float strength)
        {
            m_LensDirtStrength = (std::clamp)(strength, 0.0f, 1.0f);
        }

        bool IsNoiseEnable() const
        {
            return m_EnableNoise;
        }

        void SetBloomIntensity(float intensity)
        {
            m_BloomBaseIntensity = intensity;
            m_BloomIntensity = intensity;
        }
    };
}
