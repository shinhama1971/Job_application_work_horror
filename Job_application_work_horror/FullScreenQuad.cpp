// ============================================================================
// ファイルの役割: ポストプロセス用の画面全体ポリゴンとレンダーターゲットを管理します。
// ============================================================================

#include "FullScreenQuad.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

namespace Graphics
{
    void FullScreenQuad::Init()
    {
        m_Vertices.resize(4);

        float w = (float)Application::GetWidth();
        float h = (float)Application::GetHeight();

        m_Vertices[0].position = Vector3(0.0f, 0.0f, 0.0f);
        m_Vertices[1].position = Vector3(w, 0.0f, 0.0f);
        m_Vertices[2].position = Vector3(0.0f, h, 0.0f);
        m_Vertices[3].position = Vector3(w, h, 0.0f);

        m_Vertices[0].color = Color(1, 1, 1, 1);
        m_Vertices[1].color = Color(1, 1, 1, 1);
        m_Vertices[2].color = Color(1, 1, 1, 1);
        m_Vertices[3].color = Color(1, 1, 1, 1);

        m_Vertices[0].uv = Vector2(0, 0);
        m_Vertices[1].uv = Vector2(1, 0);
        m_Vertices[2].uv = Vector2(0, 1);
        m_Vertices[3].uv = Vector2(1, 1);

        m_VertexBuffer.Create(m_Vertices);

        m_Indices.clear();
        m_Indices.push_back(0);
        m_Indices.push_back(1);
        m_Indices.push_back(2);
        m_Indices.push_back(3);

        m_IndexBuffer.Create(m_Indices);

        m_BloomShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/bloomCompositePS.hlsl"
        );

        m_VolumeShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/volumetricFlashlightPS.hlsl"
        );

        m_OverlayShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/crtOverlayPS.hlsl"
        );

        m_ExposureShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/exposurePS.hlsl"
        );

        m_Material = std::make_unique<Material>();

        MATERIAL mtrl{};
        mtrl.Diffuse = Color(1, 1, 1, 1);
        mtrl.TextureEnable = true;

        m_Material->Create(mtrl);

        Renderer::CreateConstantBuffer(
            sizeof(TimeBuffer),
            m_TimeBuffer.ReleaseAndGetAddressOf()
        );
    }

    void FullScreenQuad::Uninit()
    {
        m_TimeBuffer.Reset();
    }

    void FullScreenQuad::Draw(
        ID3D11ShaderResourceView* sceneSRV,
        ID3D11ShaderResourceView* bloomSRV,
        float time,
        float bloomIntensity,
        float noiseAmount,
        float vignetteStrength,
        float volumeIntensity,
        float lensDistortionStrength,
        float horrorPulseStrength,
        float exposure,
        float lensMoisture,
        float corridorTension,
        float filmGradeStrength,
        float lensDirtStrength,
        float signalInterference)
    {
        ID3D11DeviceContext* context =
            Renderer::GetDeviceContext();

        Renderer::SetWorldViewProjection2D();
        Renderer::SetDepthEnable(false);
        Renderer::SetUV(0.0f, 0.0f, 1.0f, 1.0f);

        TimeBuffer tb{};
        tb.time = time;
        tb.bloomIntensity = bloomIntensity;
        tb.noiseAmount = noiseAmount;
        tb.vignetteStrength = vignetteStrength;
        tb.screenAspect = static_cast<float>(Application::GetWidth()) /
            static_cast<float>(Application::GetHeight());
        tb.volumeIntensity = volumeIntensity;
        tb.lensDistortionStrength = lensDistortionStrength;
        tb.horrorPulseStrength = horrorPulseStrength;
        tb.exposure = exposure;
        tb.lensMoisture = lensMoisture;
        tb.corridorTension = corridorTension;
        tb.filmGradeStrength = filmGradeStrength;
        tb.lensDirtStrength = lensDirtStrength;
        tb.signalInterference = signalInterference;

        context->UpdateSubresource(
            m_TimeBuffer.Get(),
            0,
            nullptr,
            &tb,
            0,
            0
        );

        m_VertexBuffer.SetGPU();
        m_IndexBuffer.SetGPU();
        m_Material->SetGPU();

        ID3D11Buffer* timeBuffer = m_TimeBuffer.Get();
        context->PSSetConstantBuffers(0, 1, &timeBuffer);

        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
        );

        ID3D11ShaderResourceView* nullResource = nullptr;

        // Add only the eye-adaptation brightness difference. The original
        // back buffer remains visible even if the captured SRV is unavailable.
        m_ExposureShader.SetGPU();
        Renderer::SetBlendState(BS_ADDITIVE);
        context->PSSetShaderResources(0, 1, &sceneSRV);
        context->DrawIndexed(4, 0, 0);
        context->PSSetShaderResources(0, 1, &nullResource);

        // Add bloom after exposure so bright fixtures retain their glow.
        if (bloomSRV != nullptr && bloomIntensity > 0.001f)
        {
            m_BloomShader.SetGPU();
            Renderer::SetBlendState(BS_ADDITIVE);
            context->PSSetShaderResources(0, 1, &bloomSRV);
            context->DrawIndexed(4, 0, 0);
        }

        context->PSSetShaderResources(0, 1, &nullResource);

        // Integrate a short section of atmospheric scattering along the
        // flashlight ray. The shadow depth map stops the beam at walls.
        if (volumeIntensity > 0.0f)
        {
            m_VolumeShader.SetGPU();
            Renderer::SetBlendState(BS_ADDITIVE);
            context->DrawIndexed(4, 0, 0);
        }

        // CRT is a transparent overlay and can never replace the scene with black.
        if (noiseAmount > 0.0f || horrorPulseStrength > 0.001f ||
            lensMoisture > 0.001f || filmGradeStrength > 0.001f ||
            signalInterference > 0.001f)
        {
            m_OverlayShader.SetGPU();
            Renderer::SetBlendState(BS_ALPHABLEND);
            context->PSSetShaderResources(0, 1, &sceneSRV);
            context->DrawIndexed(4, 0, 0);
            context->PSSetShaderResources(0, 1, &nullResource);
        }

        Renderer::SetBlendState(BS_NONE);
        Renderer::SetDepthEnable(true);
    }
}
