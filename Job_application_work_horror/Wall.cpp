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
    m_SurfaceMaterial.Diffuse = Color(0.34f, 0.36f, 0.33f, 1.0f);
    m_SurfaceMaterial.Ambient = Color(0.03f, 0.035f, 0.03f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.04f, 0.04f, 0.04f, 1.0f);
    m_SurfaceMaterial.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
    m_SurfaceMaterial.Shininess = 4.0f;
    m_SurfaceMaterial.TextureEnable = FALSE;
    m_Material->Create(m_SurfaceMaterial);
}

void Wall::Update()
{
}

void Wall::Draw(Camera* cam)
{
    if (!m_Visible)
    {
        return;
    }

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
    if (!m_Visible || !m_CastsShadow)
    {
        return;
    }

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
    if (!m_Visible || !m_CollisionEnabled)
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

bool Wall::IntersectsInteractionSegment(
    const Vector3& start,
    const Vector3& end,
    float& hitDistance) const
{
    if (!m_Visible || !m_CollisionEnabled)
    {
        return false;
    }

    const Vector3 halfExtent(
        std::abs(m_Scale.x) * 0.5f,
        std::abs(m_Scale.y) * 0.5f,
        std::abs(m_Scale.z) * 0.5f);
    const Vector3 boxMin = m_Position - halfExtent;
    const Vector3 boxMax = m_Position + halfExtent;
    const Vector3 direction = end - start;

    float minimumTime = 0.0f;
    float maximumTime = 1.0f;
    const auto clipAxis = [&minimumTime, &maximumTime](
        float origin,
        float delta,
        float minimum,
        float maximum)
    {
        constexpr float epsilon = 0.000001f;
        if (std::abs(delta) <= epsilon)
        {
            return origin >= minimum && origin <= maximum;
        }

        float enterTime = (minimum - origin) / delta;
        float exitTime = (maximum - origin) / delta;
        if (enterTime > exitTime)
        {
            std::swap(enterTime, exitTime);
        }
        minimumTime = (std::max)(minimumTime, enterTime);
        maximumTime = (std::min)(maximumTime, exitTime);
        return minimumTime <= maximumTime;
    };

    if (!clipAxis(start.x, direction.x, boxMin.x, boxMax.x) ||
        !clipAxis(start.y, direction.y, boxMin.y, boxMax.y) ||
        !clipAxis(start.z, direction.z, boxMin.z, boxMax.z))
    {
        return false;
    }

    const float segmentLength = direction.Length();
    hitDistance = segmentLength * (std::clamp)(minimumTime, 0.0f, 1.0f);
    return true;
}

void Wall::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}

void Wall::SetAppearance(
    const Color& diffuse,
    const Color& emission,
    float shininess)
{
    m_SurfaceMaterial.Diffuse = diffuse;
    m_SurfaceMaterial.Ambient = Color(0.025f, 0.025f, 0.025f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.10f, 0.11f, 0.10f, 1.0f);
    m_SurfaceMaterial.Emission = emission;
    m_SurfaceMaterial.Shininess = (std::max)(shininess, 1.0f);
    m_SurfaceMaterial.TextureEnable = FALSE;

    if (m_Material != nullptr)
    {
        m_Material->SetMaterial(m_SurfaceMaterial);
    }
}
