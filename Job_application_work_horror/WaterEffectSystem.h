// ============================================================================
// ファイルの役割: 水滴、着水・足音波紋と、それらの描画資源を管理します。
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
};
