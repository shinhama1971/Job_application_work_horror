// ============================================================================
// ファイルの役割: 驚かせる瞬間などに、画面の上へ漂う埃の粒と細かい走査線を短い時間だけ重ねている（Object名は "CRTNoise"）。
// 主な技術: 画面全体に重ねる表現、シェーダーで作る粒の模様、アルファブレンド、時間で動く表現
// ============================================================================

#include "ScreenDustOverlay.h"
#include "Renderer.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

// 画面と同じ大きさの四角形と、埃を描くシェーダー（PS_HorrorDust）、定数バッファを作っている
void ScreenDustOverlay::Init()
{
    float w = (float)Application::GetWidth();
    float h = (float)Application::GetHeight();

    m_Vertices.resize(4);

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

    m_Indices = { 0, 1, 2, 3 };
    m_IndexBuffer.Create(m_Indices);

    m_Shader.Create(
        "shader/unlitTextureVS.hlsl",
        "shader/PS_HorrorDust.hlsl"
    );

    m_Material = std::make_unique<Material>();

    MATERIAL mtrl{};
    mtrl.Diffuse = Color(1, 1, 1, 1);
    mtrl.TextureEnable = false;

    m_Material->Create(mtrl);

    Renderer::CreateConstantBuffer(
        sizeof(TimeBuffer),
        m_TimeBuffer.ReleaseAndGetAddressOf()
    );
}

// 表示している間だけ時間を進め、残り秒数が尽きたら消している
void ScreenDustOverlay::Update()
{
    if (!m_IsActive) return;

    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;

    if (m_Timer > 0.0f)
    {
        m_Timer -= deltaTime;

        if (m_Timer <= 0.0f)
        {
            m_IsActive = false;
        }
    }
}

// 2D用の行列で、画面全体に半透明で重ねて描いている（深度は使わない）
void ScreenDustOverlay::Draw(Camera* /*camera*/)
{
    if (!m_IsActive) return;

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();

    Renderer::SetWorldViewProjection2D();
    Renderer::SetDepthEnable(false);
    Renderer::SetBlendState(BS_ALPHABLEND);

    TimeBuffer tb{};
    tb.time = m_Time;
    tb.power = m_Power;
    tb.dummy1 = 0.0f;
    tb.dummy2 = 0.0f;

    context->UpdateSubresource(
        m_TimeBuffer.Get(),
        0,
        nullptr,
        &tb,
        0,
        0
    );

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    ID3D11Buffer* timeBuffer = m_TimeBuffer.Get();
    context->PSSetConstantBuffers(0, 1, &timeBuffer);

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
    );

    context->DrawIndexed(4, 0, 0);

    Renderer::SetDepthEnable(true);
}

// 定数バッファを解放している
void ScreenDustOverlay::Uninit()
{
    m_TimeBuffer.Reset();
}
