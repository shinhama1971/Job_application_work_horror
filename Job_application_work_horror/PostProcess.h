#pragma once

#include "RenderTexture.h"
#include "FullScreenQuad.h"
#include "ComputeShader.h"

namespace Effect
{
    class PostProcess
    {
    private:
        bool m_EnableNoise = true;

        float m_Time = 0.0f;
        float m_BloomIntensity = 0.72f;

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

        void SetNoise(bool enable)
        {
            m_EnableNoise = enable;
        }

        bool IsNoiseEnable() const
        {
            return m_EnableNoise;
        }

        void SetBloomIntensity(float intensity)
        {
            m_BloomIntensity = intensity;
        }
    };
}
