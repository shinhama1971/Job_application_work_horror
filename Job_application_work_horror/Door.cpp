#include "Door.h"

#include "CeilingLight.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void Door::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Vertices.reserve(96);
    m_Indices.reserve(288);

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

        for (size_t i = 0; i < positions.size(); ++i)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[i];
            vertex.normal = normal;
            vertex.color = color;
            vertex.uv = uvs[i];
            m_Vertices.push_back(vertex);
        }

        const unsigned int faceIndices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(m_Indices.end(), faceIndices, faceIndices + 12);
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

    addBox(Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f),
           Color(0.30f, 0.13f, 0.07f, 1.0f));
    addBox(Vector3(0.0f, 0.22f, -0.54f), Vector3(0.35f, 0.17f, 0.035f),
           Color(0.16f, 0.055f, 0.025f, 1.0f));
    addBox(Vector3(0.0f, -0.22f, -0.54f), Vector3(0.35f, 0.17f, 0.035f),
           Color(0.16f, 0.055f, 0.025f, 1.0f));
    addBox(Vector3(-0.28f, 0.0f, -0.64f), Vector3(0.055f, 0.075f, 0.11f),
           Color(0.72f, 0.48f, 0.12f, 1.0f));

    m_DoorIndexCount = m_Indices.size();
    // A separate, non-shadow-casting strip represents light escaping from
    // the room beyond the threshold.
    addBox(Vector3(0.0f, -0.505f, -0.61f),
           Vector3(0.48f, 0.018f, 0.045f),
           Color(1.0f, 0.72f, 0.42f, 1.0f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.08f, 0.08f, 0.06f, 1.0f);
    material.Shininess = 8.0f;
    material.TextureEnable = FALSE;
    m_Material->Create(material);

    m_LeakMaterial = std::make_unique<Material>();
    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = Color(0.05f, 0.035f, 0.02f, 1.0f);
    leakMaterial.Emission = Color(0.08f, 0.035f, 0.015f, 1.0f);
    leakMaterial.TextureEnable = FALSE;
    m_LeakMaterial->Create(leakMaterial);

    m_Scale = Vector3(30.0f, 50.0f, 4.0f);
}
void Door::Update()
{
    if (!m_IsOpening)
    {
        return;
    }

    constexpr float deltaTime = 1.0f / 60.0f;
    if (m_OpenDelayTimer > 0.0f)
    {
        m_OpenDelayTimer = (std::max)(
            0.0f,
            m_OpenDelayTimer - deltaTime);
        const float elapsed = m_OpenDelayDuration - m_OpenDelayTimer;
        const float remaining = m_OpenDelayTimer /
            (std::max)(m_OpenDelayDuration, 0.001f);
        const float rattleStrength =
            0.006f + static_cast<float>(m_LoopPhase) * 0.0045f;
        m_OpenAngle = std::sin(
            elapsed * (30.0f + static_cast<float>(m_LoopPhase) * 7.0f)) *
            rattleStrength * remaining;

        if (m_OpenDelayTimer <= 0.0f)
        {
            m_OpenAngle = 0.0f;
        }
        return;
    }

    constexpr float targetAngle = 1.50f;
    m_OpenAngle = (std::min)(
        m_OpenAngle + m_OpenSpeed,
        targetAngle);

    if (m_OpenAngle >= targetAngle)
    {
        m_IsOpen = true;
        m_IsOpening = false;
    }
}

const char* Door::GetInteractionPrompt() const
{
    return "Open corridor door";
}

void Door::Interact(Player& player)
{
    (void)player;

    // This door leads to the repeating corridor and must be usable before
    // power restoration. The final exit remains separately power-locked.
    if (!m_IsOpen && !m_IsOpening)
    {
        m_IsOpening = true;
        m_OpenDelayTimer = m_OpenDelayDuration;

        const float pulseStrength =
            0.08f + static_cast<float>(m_LoopPhase) * 0.055f;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            pulseStrength,
            0.18f + static_cast<float>(m_LoopPhase) * 0.06f);
        Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
            0.82f + static_cast<float>(m_LoopPhase) * 0.10f,
            0.20f + static_cast<float>(m_LoopPhase) * 0.10f);

        CeilingLight* doorLight =
            Core::Game::GetInstance()->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(
                0.24f + static_cast<float>(m_LoopPhase) * 0.14f,
                0.46f + static_cast<float>(m_LoopPhase) * 0.16f);
        }

        Input::SetVibration(
            3 + m_LoopPhase * 2,
            0.07f + static_cast<float>(m_LoopPhase) * 0.025f);
    }
}

void Door::ResetClosed(int loopPhase)
{
    m_Position = m_StartPosition;
    m_OpenAngle = 0.0f;
    m_IsOpen = false;
    m_IsOpening = false;
    m_OpenDelayTimer = 0.0f;
    m_LoopPhase = (std::clamp)(loopPhase, 0, 3);

    // Each return changes the familiar door slightly: the second loop drags,
    // while the final loop hesitates and then opens with unnatural speed.
    if (m_LoopPhase == 0)
    {
        m_OpenSpeed = 0.032f;
        m_OpenDelayDuration = 0.06f;
    }
    else if (m_LoopPhase == 1)
    {
        m_OpenSpeed = 0.029f;
        m_OpenDelayDuration = 0.20f;
    }
    else if (m_LoopPhase == 2)
    {
        m_OpenSpeed = 0.023f;
        m_OpenDelayDuration = 0.38f;
    }
    else
    {
        m_OpenSpeed = 0.046f;
        m_OpenDelayDuration = 0.58f;
    }
}

void Door::ResolveCollision(Vector3& position, float radius) const
{
    // Once the handle is used, let the player pass while the leaf swings.
    // This avoids the rotating mesh pushing the player into the wall.
    if (m_IsOpen || m_IsOpening)
    {
        return;
    }

    const float halfX = std::abs(m_Scale.x) * 0.5f;
    const float halfZ = std::abs(m_Scale.z) * 0.5f;
    const float minX = m_Position.x - halfX;
    const float maxX = m_Position.x + halfX;
    const float minZ = m_Position.z - halfZ;
    const float maxZ = m_Position.z + halfZ;

    const float closestX = std::clamp(position.x, minX, maxX);
    const float closestZ = std::clamp(position.z, minZ, maxZ);
    const float deltaX = position.x - closestX;
    const float deltaZ = position.z - closestZ;
    const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;

    if (distanceSquared >= radius * radius)
    {
        return;
    }

    constexpr float epsilon = 0.000001f;
    if (distanceSquared > epsilon)
    {
        const float distance = std::sqrt(distanceSquared);
        const float pushDistance = radius - distance;
        position.x += deltaX / distance * pushDistance;
        position.z += deltaZ / distance * pushDistance;
        return;
    }

    const float distanceToLeft = position.x - minX;
    const float distanceToRight = maxX - position.x;
    const float distanceToNear = position.z - minZ;
    const float distanceToFar = maxZ - position.z;
    const float nearestFace = (std::min)(
        (std::min)(distanceToLeft, distanceToRight),
        (std::min)(distanceToNear, distanceToFar)
    );

    if (nearestFace == distanceToLeft)
    {
        position.x = minX - radius;
    }
    else if (nearestFace == distanceToRight)
    {
        position.x = maxX + radius;
    }
    else if (nearestFace == distanceToNear)
    {
        position.z = minZ - radius;
    }
    else
    {
        position.z = maxZ + radius;
    }
}

Matrix Door::GetDoorWorldMatrix() const
{
    // The generated mesh is centered. Move its left edge to the origin,
    // rotate around that hinge, then return the hinge to world space.
    const float halfWidth = std::abs(m_Scale.x) * 0.5f;
    const Vector3 hingePosition(
        m_StartPosition.x - halfWidth,
        m_StartPosition.y,
        m_StartPosition.z);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix centerFromHinge = Matrix::CreateTranslation(
        halfWidth,
        0.0f,
        0.0f);
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y + m_OpenAngle,
        m_Rotation.x,
        m_Rotation.z);
    return scale * centerFromHinge * rotation *
        Matrix::CreateTranslation(hingePosition);
}

void Door::Draw(Camera* camera)
{
    camera->SetCamera();

    Matrix world = GetDoorWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_DoorIndexCount), 0, 0);

    const float normalizedOpen = (std::clamp)(m_OpenAngle / 1.20f, 0.0f, 1.0f);
    const float openAmount = normalizedOpen * normalizedOpen *
        (3.0f - 2.0f * normalizedOpen);
    float rattleGlow = 0.0f;
    if (m_OpenDelayTimer > 0.0f)
    {
        const float elapsed = m_OpenDelayDuration - m_OpenDelayTimer;
        const float remaining = m_OpenDelayTimer /
            (std::max)(m_OpenDelayDuration, 0.001f);
        rattleGlow =
            (std::sin(elapsed * 45.0f) * 0.5f + 0.5f) * remaining;
    }

    const bool powerRestored =
        Core::Game::GetInstance()->IsPowerRestored();
    const float leakIntensity =
        0.045f + static_cast<float>(m_LoopPhase) * 0.025f +
        openAmount * (powerRestored ? 0.62f : 0.30f) +
        rattleGlow * 0.22f;

    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = Color(0.04f, 0.03f, 0.02f, 1.0f);
    leakMaterial.Emission = powerRestored
        ? Color(
            0.72f * leakIntensity,
            0.82f * leakIntensity,
            1.00f * leakIntensity,
            1.0f)
        : Color(
            1.00f * leakIntensity,
            0.24f * leakIntensity,
            0.10f * leakIntensity,
            1.0f);
    leakMaterial.TextureEnable = FALSE;
    m_LeakMaterial->SetMaterial(leakMaterial);
    m_LeakMaterial->SetGPU();

    // The leak belongs to the doorway, so it remains fixed while the door
    // leaf rotates around its hinge.
    const Matrix baseRotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    Matrix leakWorld = Matrix::CreateScale(m_Scale) * baseRotation *
        Matrix::CreateTranslation(m_StartPosition);
    Renderer::SetWorldMatrix(&leakWorld);
    context->DrawIndexed(
        static_cast<UINT>(m_Indices.size() - m_DoorIndexCount),
        static_cast<UINT>(m_DoorIndexCount),
        0);
}

void Door::DrawShadow()
{
    Matrix world = GetDoorWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Core::Game::GetInstance()->GetShadowMap()->SetShader();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_DoorIndexCount), 0, 0);
}

void Door::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
