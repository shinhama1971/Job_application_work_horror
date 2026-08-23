#include "Item.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"

#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void Item::BuildGeometry()
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

    const Color glass(0.34f, 0.46f, 0.38f, 1.0f);
    const Color metal(0.62f, 0.58f, 0.42f, 1.0f);
    const Color filament(1.0f, 0.48f, 0.10f, 1.0f);
    addBox(Vector3::Zero, Vector3(0.22f, 0.46f, 0.22f), glass);
    addBox(Vector3(0.0f, 0.58f, 0.0f),
        Vector3(0.30f, 0.12f, 0.30f), metal);
    addBox(Vector3(0.0f, -0.58f, 0.0f),
        Vector3(0.30f, 0.12f, 0.30f), metal);
    addBox(Vector3(0.0f, 0.0f, -0.25f),
        Vector3(0.045f, 0.34f, 0.035f), filament);
}

void Item::Init()
{
    m_Vertices.reserve(96);
    m_Indices.reserve(288);
    BuildGeometry();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.55f, 0.48f, 0.30f, 1.0f);
    material.Emission = Color(0.065f, 0.035f, 0.008f, 1.0f);
    material.Shininess = 32.0f;
    material.TextureEnable = FALSE;
    m_Material->Create(material);

    m_Scale = Vector3(5.0f, 5.0f, 5.0f);
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

void Item::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    // A slow hover and pulse make the small fuse readable in a dark room.
    constexpr float deltaTime = 1.0f / 60.0f;
    m_AnimationTime += deltaTime;
    m_Rotation.y += 0.025f;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * 2.6f) * 0.75f;
}

void Item::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }

    m_IsCollected = true;
    Core::Game::GetInstance()->AddItemCount();
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        0.72f, 0.30f);
    Input::SetVibration(6, 0.14f);

    if (Core::Game::GetInstance()->GetItemCount() == 1)
    {
        const Vector3 shadowPosition(
            player.GetPosition().x,
            player.GetPosition().y,
            player.GetPosition().z - 80.0f
        );

        Core::Game::GetInstance()->RequestAddObject<ShadowMan>(
            [shadowPosition](ShadowMan& shadow)
            {
                shadow.SetPosition(
                    shadowPosition.x,
                    shadowPosition.y,
                    shadowPosition.z
                );
            }
        );
    }
}

void Item::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

    cam->SetCamera();

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );

    Matrix t = Matrix::CreateTranslation(m_Position);

    const float pulse =
        1.0f + (std::sin(m_AnimationTime * 3.2f) * 0.5f + 0.5f) * 0.08f;
    Matrix s = Matrix::CreateScale(
        m_Scale.x * pulse,
        m_Scale.y * pulse,
        m_Scale.z * pulse
    );

    Matrix worldmtx = s * r * t;

    Renderer::SetWorldMatrix(&worldmtx);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

void Item::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
}
