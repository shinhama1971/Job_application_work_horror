// ============================================================================
// ファイルの役割: 懐中電灯の電池回復アイテムと、その表示・取得演出を管理します。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

class BatteryItem : public Object, public Interactable
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    std::unique_ptr<Material> m_BodyMaterial;
    std::unique_ptr<Material> m_MetalMaterial;
    std::unique_ptr<Material> m_ChargeMaterial;

    unsigned int m_BodyIndexCount = 0;
    unsigned int m_MetalIndexStart = 0;
    unsigned int m_MetalIndexCount = 0;
    unsigned int m_ChargeIndexStart = 0;
    unsigned int m_ChargeIndexCount = 0;

    bool m_IsCollected = false;
    bool m_IsActive = true;
    float m_RecoverValue = 30.0f;
    float m_BaseY = 0.0f;
    float m_AnimationTime = 0.0f;

    void BuildGeometry();

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
    const char* GetInteractionPrompt() const override { return "電池を拾う"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }
};
