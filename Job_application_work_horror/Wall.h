#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"

class Camera;

class Wall : public Object
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    IndexBuffer m_IndexBuffer;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;

    std::unique_ptr<Material> m_Material;
    MATERIAL m_SurfaceMaterial{};
    bool m_CollisionEnabled = true;
    bool m_CastsShadow = true;
    bool m_Visible = true;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    void Uninit() override;

    void ResolveCollision(
        DirectX::SimpleMath::Vector3& position,
        float radius) const;

    bool IntersectsInteractionSegment(
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end,
        float& hitDistance) const;

    void SetAppearance(
        const DirectX::SimpleMath::Color& diffuse,
        const DirectX::SimpleMath::Color& emission,
        float shininess);

    void SetCollisionEnabled(bool enabled)
    {
        m_CollisionEnabled = enabled;
    }

    void SetCastsShadow(bool castsShadow)
    {
        m_CastsShadow = castsShadow;
    }

    void SetVisible(bool visible)
    {
        m_Visible = visible;
    }

    void SetScale(float x, float y, float z)
    {
        m_Scale = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};
