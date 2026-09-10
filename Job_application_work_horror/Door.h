// ============================================================================
// ファイルの役割: ドアの描画、開閉アニメーション、施錠条件、当たり判定を管理します。
// ============================================================================

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
    std::unique_ptr<Material> m_LeakMaterial;
    size_t m_DoorIndexCount = 0;

    bool m_IsOpen = false;
    bool m_IsOpening = false;
    bool m_IsLocked = false;
    float m_LockedRattleTimer = 0.0f;

    DirectX::SimpleMath::Vector3 m_StartPosition;
    float m_OpenAngle = 0.0f;
    float m_OpenSpeed = 0.032f;
    float m_OpenDelayTimer = 0.0f;
    float m_OpenDelayDuration = 0.06f;
    int m_LoopPhase = 0;

    DirectX::SimpleMath::Matrix GetDoorWorldMatrix() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return true; }
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
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
    void ResetClosed(int loopPhase = 0);
    void SetLocked(bool locked) { m_IsLocked = locked; }
    bool IsLocked() const { return m_IsLocked; }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_StartPosition = m_Position;
    }

    bool IsOpen() const
    {
        return m_IsOpen;
    }
};
