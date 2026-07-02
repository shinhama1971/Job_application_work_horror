#pragma once

#include "RenderTexture.h"
#include "FullScreenQuad.h"

namespace Effect
{
    class PostProcess
    {
    private:
        bool m_EnableNoise = true;

        float m_Time = 0.0f;

        Graphics::RenderTexture m_RenderTexture;
        Graphics::FullScreenQuad m_FullScreenQuad;

    public:
        void Init();
        void Uninit();
        void Update();

        void Begin();
		void SetRenderTarget();
        void End();
        void Draw();

        void SetNoise(bool enable)
        {
            m_EnableNoise = enable;
        }

        bool IsNoiseEnable() const
        {
            return m_EnableNoise;
        }
    };
}