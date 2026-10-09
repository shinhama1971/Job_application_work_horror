// ============================================================================
// ファイルの役割: 水滴、着水・足音波紋と、それらの描画資源を管理します。
// 主な技術: コンポジション、イベント連携、時間減衰、描画順管理
// ============================================================================

#pragma once

#include "Material.h"
#include "Shader.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

class Camera;

class WaterEffectSystem final
{
private:
    struct FallingDrop
    {
        DirectX::SimpleMath::Vector3 Position;
        float Speed = 20.0f;
        float WaitTimer = 0.0f;
        float ImpactTimer = 0.0f;
        float Seed = 0.0f;
        bool Active = true;
    };

    struct WaterRipple
    {
        DirectX::SimpleMath::Vector3 Position;
        float Age = 0.0f;
        float Duration = 0.7f;
        float MaxRadius = 3.0f;
    };

    std::vector<FallingDrop> m_FallingDrops;
    std::vector<DirectX::SimpleMath::Vector2> m_PuddleCenters;
    std::vector<VERTEX_3D> m_DropVertices;
    VertexBuffer<VERTEX_3D> m_DropVertexBuffer;
    Shader m_DropShader;
    std::unique_ptr<Material> m_DropMaterial;
    std::vector<VERTEX_3D> m_RippleVertices;
    VertexBuffer<VERTEX_3D> m_RippleVertexBuffer;
    std::unique_ptr<Material> m_RippleMaterial;
    std::vector<WaterRipple> m_FootstepRipples;
    float m_FootstepRippleCooldown = 0.0f;
    float m_LensSplashCooldown = 0.0f;
    // 床一面が水に浸かった範囲（1面の西棟）。水たまりと同じく足音が水音になり、波紋が立ちます。
    bool m_HasFloodRegion = false;
    DirectX::SimpleMath::Vector2 m_FloodMin;
    DirectX::SimpleMath::Vector2 m_FloodMax;

    void UpdateFallingDrops(float deltaTime);
    void DrawFallingDrops(Camera* camera);
    void UpdateFootstepRipples(float deltaTime);
    void DrawWaterRipples(Camera* camera);

public:
    void Init();
    void Update(float deltaTime);
    void Draw(Camera* camera);
    void Uninit();

    bool TriggerFootstepRipple(
        const DirectX::SimpleMath::Vector3& position,
        bool sprinting);
    bool IsInsidePuddle(
        const DirectX::SimpleMath::Vector3& position) const;
    bool IsAnyPuddleVisible(const Camera& camera) const;
    // x・z の範囲（minimum〜maximum）を、床一面が水に浸かった場所にします。
    void SetFloodRegion(
        const DirectX::SimpleMath::Vector2& minimum,
        const DirectX::SimpleMath::Vector2& maximum)
    {
        m_HasFloodRegion = true;
        m_FloodMin = minimum;
        m_FloodMax = maximum;
    }
    bool IsInsideFloodRegion(const DirectX::SimpleMath::Vector3& position) const
    {
        return m_HasFloodRegion &&
            position.x > m_FloodMin.x && position.x < m_FloodMax.x &&
            position.z > m_FloodMin.y && position.z < m_FloodMax.y;
    }
    bool HasFloodRegion() const { return m_HasFloodRegion; }
    DirectX::SimpleMath::Vector2 GetFloodMin() const { return m_FloodMin; }
    DirectX::SimpleMath::Vector2 GetFloodMax() const { return m_FloodMax; }
};
