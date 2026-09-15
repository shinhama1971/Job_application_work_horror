// ============================================================================
// ファイルの役割: 画面の埃、レンズ汚れ、湿り表現のオーバーレイを管理します。
// 主な技術: スクリーンスペース表現、粒子、アルファブレンド、時間アニメーション
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "ScreenDustOverlay.h"
#include "Renderer.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
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

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
void ScreenDustOverlay::Update()
{
    if (!m_IsActive) return;

    m_Time += 1.0f / 60.0f;

    if (m_Timer > 0.0f)
    {
        m_Timer -= 1.0f / 60.0f;

        if (m_Timer <= 0.0f)
        {
            m_IsActive = false;
        }
    }
}

// 処理内容: 現在の状態に対応する描画命令を発行します。
void ScreenDustOverlay::Draw(Camera* cam)
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

// 処理内容: 所有するリソースを依存関係の逆順で解放します。
void ScreenDustOverlay::Uninit()
{
    m_TimeBuffer.Reset();
}
