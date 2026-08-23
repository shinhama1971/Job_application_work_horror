#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

class Item : public Object, public Interactable
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    bool m_IsCollected = false;
    bool m_IsActive = true;
    float m_BaseY = 0.0f;
    float m_AnimationTime = 0.0f;

    void BuildGeometry();

    // プレイヤーに触れた判定用の距離

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "ヒューズを拾う"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    bool IsCollected() const
    {
        return m_IsCollected;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    bool IsActive() const
    {
        return m_IsActive;
    }
};
