#include "PostProcess.h"
#include "Renderer.h"
#include "Application.h"

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

        m_FullScreenQuad.Init();
    }

    void PostProcess::Uninit()
    {
        m_FullScreenQuad.Uninit();
        m_RenderTexture.Uninit();
    }

    void PostProcess::Update()
    {
        m_Time += 1.0f / 60.0f;
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

    void PostProcess::Draw()
    {
        if (m_EnableNoise)
        {
            m_FullScreenQuad.Draw(
                m_RenderTexture.GetSRV(),
                m_Time
            );
        }
    }
}