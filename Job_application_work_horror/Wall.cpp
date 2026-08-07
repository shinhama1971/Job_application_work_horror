#include "Wall.h"
#include "Camera.h"
#include "Game.h"
#include "Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void Wall::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Vertices.reserve(24);
    m_Indices.reserve(72);

    const Color vertexColor(1.0f, 1.0f, 1.0f, 1.0f);

    const auto addFace = [this, &vertexColor](
        const std::array<Vector3, 4>& positions,
        const Vector3& normal)
    {
        const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
        const std::array<Vector2, 4> uvs =
        {
            Vector2(0.0f, 1.0f),
            Vector2(0.0f, 0.0f),
            Vector2(1.0f, 1.0f),
            Vector2(1.0f, 0.0f)
        };

        for (size_t i = 0; i < positions.size(); ++i)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[i];
            vertex.normal = normal;
            vertex.color = vertexColor;
            vertex.uv = uvs[i];
            m_Vertices.push_back(vertex);
        }

        // Both windings keep the wall visible from either side. This is
        // useful while the room layout is still being iterated.
        const unsigned int faceIndices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(
            m_Indices.end(),
            faceIndices,
            faceIndices + 12
        );
    };

    addFace(
        { Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f) },
        Vector3(0.0f, 0.0f, 1.0f)
    );
    addFace(
        { Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 0.0f, -1.0f)
    );
    addFace(
        { Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(1.0f, 0.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f) },
        Vector3(-1.0f, 0.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, 0.5f, 0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(0.5f, 0.5f, 0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 1.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, -0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, -0.5f, 0.5f) },
        Vector3(0.0f, -1.0f, 0.0f)
    );

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(0.34f, 0.36f, 0.33f, 1.0f);
    material.Ambient = Color(0.03f, 0.035f, 0.03f, 1.0f);
    material.Specular = Color(0.04f, 0.04f, 0.04f, 1.0f);
    material.Shininess = 4.0f;
    material.TextureEnable = FALSE;
    m_Material->Create(material);
}

void Wall::Update()
{
}

void Wall::Draw(Camera* cam)
{
    cam->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );
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

void Wall::DrawShadow()
{
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z);
    Matrix world = Matrix::CreateScale(m_Scale) * rotation *
        Matrix::CreateTranslation(m_Position);
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Core::Game::GetInstance()->GetShadowMap()->SetShader();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

void Wall::ResolveCollision(Vector3& position, float radius) const
{
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
    const float radiusSquared = radius * radius;

    if (distanceSquared >= radiusSquared)
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

    // The player's center is inside the wall rectangle. Push it through the
    // nearest face so even a spawn or a large frame step can recover safely.
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

void Wall::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
