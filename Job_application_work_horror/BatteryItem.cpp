// ============================================================================
// ファイルの役割: 懐中電灯の電池回復アイテムと、その表示・取得演出を管理
// ============================================================================

#include "BatteryItem.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"

#include <cmath>

using namespace DirectX::SimpleMath;

void BatteryItem::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    const auto addCylinder = [this](
        float centerY,
        float halfHeight,
        float radius,
        const Color& color)
    {
        constexpr unsigned int segments = 20;
        constexpr float circle = 6.28318530718f;
        const unsigned int sideBase = static_cast<unsigned int>(m_Vertices.size());

        for (unsigned int segment = 0; segment <= segments; ++segment)
        {
            const float angle = circle * static_cast<float>(segment) /
                static_cast<float>(segments);
            const float x = std::cos(angle) * radius;
            const float z = std::sin(angle) * radius;
            const Vector3 normal(std::cos(angle), 0.0f, std::sin(angle));
            const float u = static_cast<float>(segment) /
                static_cast<float>(segments);

            m_Vertices.push_back({ Vector3(x, centerY - halfHeight, z), normal,
                color, Vector2(u, 1.0f) });
            m_Vertices.push_back({ Vector3(x, centerY + halfHeight, z), normal,
                color, Vector2(u, 0.0f) });
        }

        for (unsigned int segment = 0; segment < segments; ++segment)
        {
            const unsigned int base = sideBase + segment * 2;
            const unsigned int sideIndices[] =
            {
                base, base + 1, base + 2,
                base + 2, base + 1, base + 3
            };
            m_Indices.insert(m_Indices.end(), sideIndices, sideIndices + 6);
        }

        const unsigned int bottomCenter = static_cast<unsigned int>(m_Vertices.size());
        m_Vertices.push_back({ Vector3(0.0f, centerY - halfHeight, 0.0f),
            Vector3(0.0f, -1.0f, 0.0f), color, Vector2(0.5f, 0.5f) });
        const unsigned int topCenter = static_cast<unsigned int>(m_Vertices.size());
        m_Vertices.push_back({ Vector3(0.0f, centerY + halfHeight, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f), color, Vector2(0.5f, 0.5f) });

        const unsigned int capBase = static_cast<unsigned int>(m_Vertices.size());
        for (unsigned int segment = 0; segment <= segments; ++segment)
        {
            const float angle = circle * static_cast<float>(segment) /
                static_cast<float>(segments);
            const float x = std::cos(angle) * radius;
            const float z = std::sin(angle) * radius;
            const Vector2 uv(x / (radius * 2.0f) + 0.5f,
                z / (radius * 2.0f) + 0.5f);
            m_Vertices.push_back({ Vector3(x, centerY - halfHeight, z),
                Vector3(0.0f, -1.0f, 0.0f), color, uv });
            m_Vertices.push_back({ Vector3(x, centerY + halfHeight, z),
                Vector3(0.0f, 1.0f, 0.0f), color, uv });
        }

        for (unsigned int segment = 0; segment < segments; ++segment)
        {
            const unsigned int rim = capBase + segment * 2;
            const unsigned int capIndices[] =
            {
                bottomCenter, rim + 2, rim,
                topCenter, rim + 1, rim + 3
            };
            m_Indices.insert(m_Indices.end(), capIndices, capIndices + 6);
        }
    };

    const Color white(1.0f, 1.0f, 1.0f, 1.0f);
    addCylinder(0.0f, 0.56f, 0.235f, white);
    m_BodyIndexCount = static_cast<unsigned int>(m_Indices.size());

    m_MetalIndexStart = m_BodyIndexCount;
    addCylinder(-0.60f, 0.045f, 0.245f, white);
    addCylinder(0.60f, 0.045f, 0.245f, white);
    addCylinder(0.685f, 0.040f, 0.115f, white);
    m_MetalIndexCount = static_cast<unsigned int>(m_Indices.size()) -
        m_MetalIndexStart;

    m_ChargeIndexStart = static_cast<unsigned int>(m_Indices.size());
    addCylinder(0.24f, 0.075f, 0.246f, white);
    m_ChargeIndexCount = static_cast<unsigned int>(m_Indices.size()) -
        m_ChargeIndexStart;
}

void BatteryItem::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    const auto createMaterial = [](
        const Color& diffuse,
        const Color& specular,
        const Color& emission,
        float shininess)
    {
        auto material = std::make_unique<Material>();
        MATERIAL data{};
        data.Ambient = Color(
            diffuse.x * 0.32f,
            diffuse.y * 0.32f,
            diffuse.z * 0.32f,
            diffuse.w);
        data.Diffuse = diffuse;
        data.Specular = specular;
        data.Emission = emission;
        data.Shininess = shininess;
        data.TextureEnable = FALSE;
        material->Create(data);
        return material;
    };

    m_BodyMaterial = createMaterial(
        Color(0.055f, 0.070f, 0.060f, 1.0f),
        Color(0.28f, 0.32f, 0.28f, 1.0f),
        Color(0.002f, 0.006f, 0.003f, 1.0f), 42.0f);
    m_MetalMaterial = createMaterial(
        Color(0.52f, 0.54f, 0.48f, 1.0f),
        Color(0.88f, 0.90f, 0.82f, 1.0f),
        Color(0.008f, 0.009f, 0.006f, 1.0f), 76.0f);
    m_ChargeMaterial = createMaterial(
        Color(0.10f, 0.82f, 0.28f, 1.0f),
        Color(0.50f, 1.0f, 0.60f, 1.0f),
        Color(0.035f, 0.28f, 0.075f, 1.0f), 54.0f);

    m_Scale = Vector3(4.2f, 4.2f, 4.2f);
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

void BatteryItem::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    constexpr float deltaTime = 1.0f / 60.0f;
    m_AnimationTime += deltaTime;
    m_Rotation.y += 0.022f;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * 2.4f) * 0.38f;
}

void BatteryItem::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected || player.GetBattery() >= 100.0f)
    {
        return;
    }

    player.AddBattery(m_RecoverValue);
    m_IsCollected = true;
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        0.62f, 0.34f);
    Input::SetVibration(6, 0.13f);
}

void BatteryItem::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

    cam->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    const float pulse = 1.0f +
        (std::sin(m_AnimationTime * 3.0f) * 0.5f + 0.5f) * 0.035f;
    const Matrix scale = Matrix::CreateScale(
        m_Scale.x * pulse, m_Scale.y * pulse, m_Scale.z * pulse);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    m_BodyMaterial->SetGPU();
    context->DrawIndexed(m_BodyIndexCount, 0, 0);
    m_MetalMaterial->SetGPU();
    context->DrawIndexed(m_MetalIndexCount, m_MetalIndexStart, 0);
    m_ChargeMaterial->SetGPU();
    context->DrawIndexed(m_ChargeIndexCount, m_ChargeIndexStart, 0);
}

void BatteryItem::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_BodyMaterial.reset();
    m_MetalMaterial.reset();
    m_ChargeMaterial.reset();
}
