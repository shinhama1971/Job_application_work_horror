#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "Interactable.h"

class Door : public Object, public Interactable
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    bool m_IsOpen = false;
    bool m_IsOpening = false;

    DirectX::SimpleMath::Vector3 m_StartPosition;
    DirectX::SimpleMath::Vector3 m_OpenPosition;

    float m_OpenSpeed = 1.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    void Uninit() override;

    bool IsInteractionEnabled() const override { return !m_IsOpen && !m_IsOpening; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override
    {
        return DirectX::SimpleMath::Vector3(
            m_Position.x, m_Position.y, m_Position.z);
    }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;
    void ResolveCollision(
        DirectX::SimpleMath::Vector3& position,
        float radius) const;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);

        m_StartPosition = m_Position;

        // 横に80移動して開く
        m_OpenPosition =
            m_Position + DirectX::SimpleMath::Vector3(80.0f, 0.0f, 0.0f);
    }

    bool IsOpen() const
    {
        return m_IsOpen;
    }
};
