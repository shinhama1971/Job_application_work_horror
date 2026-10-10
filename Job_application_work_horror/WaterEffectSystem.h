// ============================================================================
// ファイルの役割: 天井から落ちる水滴、着水と足音の波紋と、それらを描くための資源を管理している。
// 主な技術: Groundに持たせる部品（コンポジション）、足音のタイミングとの連携、時間で薄れる表現、描く順番の管理
// ============================================================================

#pragma once

#include "Material.h"
#include "Shader.h"
#include "VertexBuffer.h"

#include <array>
#include <memory>
#include <vector>

class Camera;

// 床の水の効果をまとめたクラス。Groundが1つ持ち、更新と描画を任せている。
class WaterEffectSystem final
{
private:
    // 天井から落ちる水滴1つ分（位置・落ちる速さ・次に落ちるまでの待ち時間・着水してからの残り時間・個体差・落ちている途中か）
    struct FallingDrop
    {
        DirectX::SimpleMath::Vector3 Position;
        float Speed = 20.0f;
        float WaitTimer = 0.0f;
        float ImpactTimer = 0.0f;
        float Seed = 0.0f;
        bool Active = true;
    };

    // 足音の波紋1つ分（位置・できてからの秒数・消えるまでの秒数・最大の半径）
    struct WaterRipple
    {
        DirectX::SimpleMath::Vector3 Position;
        float Age = 0.0f;
        float Duration = 0.7f;
        float MaxRadius = 3.0f;
    };

    // 水滴、水たまりの中心（x・z）
    std::vector<FallingDrop> m_FallingDrops;
    std::vector<DirectX::SimpleMath::Vector2> m_PuddleCenters;
    // 水滴の形・バッファ・シェーダー・マテリアル
    std::vector<VERTEX_3D> m_DropVertices;
    VertexBuffer<VERTEX_3D> m_DropVertexBuffer;
    Shader m_DropShader;
    std::unique_ptr<Material> m_DropMaterial;
    // 波紋の形・バッファ・マテリアル、足音の波紋
    std::vector<VERTEX_3D> m_RippleVertices;
    VertexBuffer<VERTEX_3D> m_RippleVertexBuffer;
    std::unique_ptr<Material> m_RippleMaterial;
    std::vector<WaterRipple> m_FootstepRipples;
    // 次に足音の波紋を作れるまでの秒数、次にレンズの曇りを出せるまでの秒数
    float m_FootstepRippleCooldown = 0.0f;
    float m_LensSplashCooldown = 0.0f;
    // 床一面が水に浸かった範囲（1面の西棟）。水たまりと同じく、足音が水音になり、波紋が立つ。
    bool m_HasFloodRegion = false;
    DirectX::SimpleMath::Vector2 m_FloodMin;
    DirectX::SimpleMath::Vector2 m_FloodMax;

    // 水滴の更新・描画、足音の波紋の更新、波紋としぶきの描画
    void UpdateFallingDrops(float deltaTime);
    void DrawFallingDrops(Camera* camera);
    void UpdateFootstepRipples(float deltaTime);
    void DrawWaterRipples(Camera* camera);

public:
    // 水たまりのマス目（82x82）の表の範囲。床（x・z が -500〜500）を覆う 16x16 マスにしている。
    // wetFloorPS.hlsl の PuddleCellBuffer と同じ値にする。
    static constexpr int PuddleCellMin = -8;
    static constexpr int PuddleCellCount = 16;
    // マス目ごとの水たまりの値（x = 水たまりがあれば1、y・z = 中心のずれと回転に使う乱数）
    using PuddleCellTable = std::array<DirectX::SimpleMath::Vector4, PuddleCellCount * PuddleCellCount>;

private:
    // 水たまりの置き場所はCPUで決め、この表をシェーダーへ渡している。
    // シェーダーで sin を使った乱数を計算すると、GPUの種類で sin の精度が違い、水たまりの場所が変わってしまうためである。
    PuddleCellTable m_PuddleCells{};

public:
    const PuddleCellTable& GetPuddleCells() const { return m_PuddleCells; }

    // 作る・更新する・描く・解放する
    void Init();
    void Update(float deltaTime);
    void Draw(Camera* camera);
    void Uninit();

    // 足元が水の中なら波紋を作っている（水の中ならtrue）
    bool TriggerFootstepRipple(
        const DirectX::SimpleMath::Vector3& position,
        bool sprinting);
    // その位置が水たまりか、水に浸かった範囲の中か
    bool IsInsidePuddle(
        const DirectX::SimpleMath::Vector3& position) const;
    // 水面が画面に映っているか
    bool IsAnyPuddleVisible(const Camera& camera) const;
    // x・z の範囲（minimum〜maximum）を、床一面が水に浸かった場所にしている。
    void SetFloodRegion(
        const DirectX::SimpleMath::Vector2& minimum,
        const DirectX::SimpleMath::Vector2& maximum)
    {
        m_HasFloodRegion = true;
        m_FloodMin = minimum;
        m_FloodMax = maximum;
    }
    // その位置が水に浸かった範囲の中かを返している（高さは見ていない）
    bool IsInsideFloodRegion(const DirectX::SimpleMath::Vector3& position) const
    {
        return m_HasFloodRegion &&
            position.x > m_FloodMin.x && position.x < m_FloodMax.x &&
            position.z > m_FloodMin.y && position.z < m_FloodMax.y;
    }
    // 水に浸かった範囲があるか、その最小と最大を返している
    bool HasFloodRegion() const { return m_HasFloodRegion; }
    DirectX::SimpleMath::Vector2 GetFloodMin() const { return m_FloodMin; }
    DirectX::SimpleMath::Vector2 GetFloodMax() const { return m_FloodMax; }
};
