// ============================================================================
// ファイルの役割: ヒューズ挿入、電力復旧、操作フィードバックを管理します。
// ============================================================================

#include "Game.h"
#include "FuseBox.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"

#include <array>

using namespace DirectX::SimpleMath;

void FuseBox::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    const auto addFace = [this](
        const std::array<Vector3, 4>& positions,
        const Vector3& normal,
        const Color& color)
    {
        const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
        const std::array<Vector2, 4> uvs =
        {
            Vector2(0.0f, 1.0f), Vector2(0.0f, 0.0f),
            Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f)
        };

        for (size_t index = 0; index < positions.size(); ++index)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[index];
            vertex.normal = normal;
            vertex.color = color;
            vertex.uv = uvs[index];
            m_Vertices.push_back(vertex);
        }

        const unsigned int indices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(m_Indices.end(), indices, indices + 12);
    };

    const auto addBox = [&addFace](
        const Vector3& center,
        const Vector3& half,
        const Color& color)
    {
        const float left = center.x - half.x;
        const float right = center.x + half.x;
        const float bottom = center.y - half.y;
        const float top = center.y + half.y;
        const float back = center.z - half.z;
        const float front = center.z + half.z;

        addFace({ Vector3(left, bottom, front), Vector3(left, top, front),
                  Vector3(right, bottom, front), Vector3(right, top, front) },
                Vector3(0.0f, 0.0f, 1.0f), color);
        addFace({ Vector3(right, bottom, back), Vector3(right, top, back),
                  Vector3(left, bottom, back), Vector3(left, top, back) },
                Vector3(0.0f, 0.0f, -1.0f), color);
        addFace({ Vector3(right, bottom, front), Vector3(right, top, front),
                  Vector3(right, bottom, back), Vector3(right, top, back) },
                Vector3(1.0f, 0.0f, 0.0f), color);
        addFace({ Vector3(left, bottom, back), Vector3(left, top, back),
                  Vector3(left, bottom, front), Vector3(left, top, front) },
                Vector3(-1.0f, 0.0f, 0.0f), color);
        addFace({ Vector3(left, top, front), Vector3(left, top, back),
                  Vector3(right, top, front), Vector3(right, top, back) },
                Vector3(0.0f, 1.0f, 0.0f), color);
        addFace({ Vector3(left, bottom, back), Vector3(left, bottom, front),
                  Vector3(right, bottom, back), Vector3(right, bottom, front) },
                Vector3(0.0f, -1.0f, 0.0f), color);
    };

    addBox(Vector3::Zero, Vector3(0.50f, 0.50f, 0.20f),
           Color(0.16f, 0.18f, 0.17f, 1.0f));
    addBox(Vector3(0.0f, 0.0f, -0.23f), Vector3(0.42f, 0.42f, 0.045f),
           Color(0.035f, 0.045f, 0.04f, 1.0f));

    const Color fuseColor = m_IsPowered
        ? Color(0.68f, 0.82f, 0.64f, 1.0f)
        : Color(0.11f, 0.13f, 0.12f, 1.0f);
    addBox(Vector3(-0.25f, -0.06f, -0.30f), Vector3(0.075f, 0.25f, 0.04f), fuseColor);
    addBox(Vector3(0.0f, -0.06f, -0.30f), Vector3(0.075f, 0.25f, 0.04f), fuseColor);
    addBox(Vector3(0.25f, -0.06f, -0.30f), Vector3(0.075f, 0.25f, 0.04f), fuseColor);

    const Color indicatorColor = m_IsPowered
        ? Color(0.12f, 1.0f, 0.24f, 1.0f)
        : Color(0.85f, 0.04f, 0.025f, 1.0f);
    addBox(Vector3(0.0f, 0.33f, -0.31f), Vector3(0.12f, 0.055f, 0.045f),
           indicatorColor);

    addBox(Vector3(0.46f, 0.0f, -0.30f), Vector3(0.025f, 0.16f, 0.04f),
           Color(0.52f, 0.39f, 0.12f, 1.0f));
}

void FuseBox::Init()
{
    m_Vertices.reserve(168);
    m_Indices.reserve(504);
    BuildGeometry();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.14f, 0.14f, 0.12f, 1.0f);
    material.Shininess = 18.0f;
    material.TextureEnable = FALSE;
    m_Material->Create(material);

    m_Scale = Vector3(12.0f, 18.0f, 4.0f);
}

void FuseBox::Update()
{
}

const char* FuseBox::GetInteractionPrompt() const
{
    if (m_IsManualControl)
    {
        return m_ManualPrompt;
    }

    if (m_IsExitControl)
    {
        return Core::Game::GetInstance()->IsPowerRestored()
            ? "出口へ非常電源を送る"
            : "先に主電源を復旧する";
    }

    return Core::Game::GetInstance()->GetItemCount() < 3
        ? "ヒューズが3本必要"
        : "電力を復旧する";
}

void FuseBox::Interact(Player& player)
{
    (void)player;

    Core::Game* game = Core::Game::GetInstance();
    if (m_IsManualControl)
    {
        if (m_IsPowered || !m_ManualInteractionAllowed)
        {
            return;
        }

        m_IsPowered = true;
        game->PlayAudioCue(SOUND_CUE_POWER);
        BuildGeometry();
        m_VertexBuffer.Modify(m_Vertices);
        game->GetPostProcess()->TriggerBloomPulse(0.72f, 0.28f);
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.22f);
        Input::SetVibration(6, 0.13f);
        return;
    }

    if (m_IsExitControl)
    {
        if (m_IsPowered || !game->IsPowerRestored())
        {
            return;
        }

        m_IsPowered = true;
        game->PlayAudioCue(SOUND_CUE_POWER);
        game->GetPostProcess()->TriggerBloomPulse(1.25f, 0.72f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.36f);
        BuildGeometry();
        m_VertexBuffer.Modify(m_Vertices);
        Input::SetVibration(10, 0.22f);

        ScreenDustOverlay* crt = game->GetObj<ScreenDustOverlay>("CRTNoise");
        if (crt != nullptr)
        {
            crt->SetPower(0.48f);
            crt->SetActive(true);
            crt->SetTimer(0.32f);
        }
        return;
    }

    if (m_IsPowered || game->GetItemCount() < 3)
    {
        return;
    }

    m_IsPowered = true;
    game->PlayAudioCue(SOUND_CUE_POWER);
    game->SetPowerRestored(true);
    game->GetPostProcess()->TriggerBloomPulse(1.65f, 1.35f);
    game->GetPostProcess()->TriggerHorrorPulse(0.55f, 0.75f);
    BuildGeometry();
    m_VertexBuffer.Modify(m_Vertices);
    Input::SetVibration(18, 0.28f);

    ScreenDustOverlay* crt =
        game->GetObj<ScreenDustOverlay>("CRTNoise");
    if (crt != nullptr)
    {
        crt->SetPower(0.9f);
        crt->SetActive(true);
        crt->SetTimer(0.7f);
    }
}

void FuseBox::ResetActivation()
{
    m_IsPowered = false;
    BuildGeometry();
    m_VertexBuffer.Modify(m_Vertices);
}

void FuseBox::Draw(Camera* camera)
{
    camera->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

void FuseBox::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
