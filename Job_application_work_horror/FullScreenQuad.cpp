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

        m_OverlayShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/crtOverlayPS.hlsl"
        );

        m_Material = std::make_unique<Material>();

        MATERIAL mtrl{};
        mtrl.Diffuse = Color(1, 1, 1, 1);
        mtrl.TextureEnable = true;

        m_Material->Create(mtrl);

        Renderer::CreateConstantBuffer(
            sizeof(TimeBuffer),
            &m_TimeBuffer
        );
    }

    void FullScreenQuad::Uninit()
    {
        SAFE_RELEASE(m_TimeBuffer);
    }

    void FullScreenQuad::Draw(
        ID3D11ShaderResourceView* bloomSRV,
        float time,
        float bloomIntensity,
        float noiseAmount)
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
        tb.padding = 0.0f;

        context->UpdateSubresource(
            m_TimeBuffer,
            0,
            nullptr,
            &tb,
            0,
            0
        );

        m_VertexBuffer.SetGPU();
        m_IndexBuffer.SetGPU();
        m_Material->SetGPU();

        context->PSSetConstantBuffers(0, 1, &m_TimeBuffer);

        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
        );

        // The original scene is already on the back buffer. Add only bloom.
        m_BloomShader.SetGPU();
        Renderer::SetBlendState(BS_ADDITIVE);
        context->PSSetShaderResources(0, 1, &bloomSRV);
        context->DrawIndexed(4, 0, 0);

        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(0, 1, &nullResource);

        // CRT is a transparent overlay and can never replace the scene with black.
        if (noiseAmount > 0.0f)
        {
            m_OverlayShader.SetGPU();
            Renderer::SetBlendState(BS_ALPHABLEND);
            context->DrawIndexed(4, 0, 0);
        }

        Renderer::SetBlendState(BS_NONE);
        Renderer::SetDepthEnable(true);
    }
}
