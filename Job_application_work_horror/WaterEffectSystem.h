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
        bool Audible = false; // 雨漏りごとに1つだけ、着水と同時に音を鳴らす。
    };

    // 足音の波紋1つ分（位置・できてからの秒数・消えるまでの秒数・最大の半径）
    struct WaterRipple
    {
        DirectX::SimpleMath::Vector3 Position;
        float Age = 0.0f;
        float Duration = 0.7f;
        float MaxRadius = 3.0f;
    };

    // 描画する水滴
    std::vector<FallingDrop> m_FallingDrops;
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
    // 波のシミュレーションへ渡す、足音と水滴のおもり（xy = x・z、z = 半径、w = 強さ）。Groundが毎フレーム受け取っている
    std::vector<DirectX::SimpleMath::Vector4> m_RippleImpulses;
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
    // シェーダーへ渡す水たまりの表（puddleMask.hlsli の PuddleCellBuffer と同じ並び）
    struct PuddleCellBufferData
    {
        PuddleCellTable Cells{};
        // マス目の区切りをずらす量（x・y = ワールドのx・z）
        DirectX::SimpleMath::Vector4 GridOffset{};
    };
    // 水たまりの置き方。Cellsが空なら、床全体のマス目に乱数で置いている（1面）。
    // 2面のように決まった場所に置くときは、マス目の区切りをずらし、置くマス目と形の乱数を指定する。
    struct PuddleLayout
    {
        // マス目の区切りをずらす量（x・z）。マス目 (i, k) の中心は (i + 0.5, k + 0.5) * 82 - GridOffset になる
        DirectX::SimpleMath::Vector2 GridOffset{};
        // 水たまりを置くマス目と形（x・y = マス目、z = 横のずれと向き、w = 奥行きのずれと幅に使う値。どちらも0〜1）
        std::vector<DirectX::SimpleMath::Vector4> Cells;
    };

private:
    // 水たまりの置き場所はCPUで決め、この表をシェーダーへ渡している。
    // シェーダーで sin を使った乱数を計算すると、GPUの種類で sin の精度が違い、水たまりの場所が変わってしまうためである。
    PuddleCellBufferData m_PuddleCellData{};
    // 水たまりの表・中心・水滴を、置き方に合わせて作っている
    void BuildPuddles(const PuddleLayout& layout);

public:
    // 天井の雨漏り：天井の一点から水滴が落ち続け、真下に小さな水たまりが少しずつ広がっていく
    struct Leak
    {
        DirectX::SimpleMath::Vector2 Center;
        float MaxRadius = 6.0f;      // 広がりきったときの半径
        float GrowSeconds = 60.0f;   // 広がりきるまでの秒数
        float Age = 0.0f;
        float Radius = 0.0f;         // 今の半径
    };
    // シェーダーへ渡す雨漏りの最大数（wetFloorPS の LEAK_COUNT と同じ）
    static constexpr int MaxLeakCount = 8;

private:
    std::vector<Leak> m_Leaks;
    // 雨漏りの真上から落ち続ける水滴を、2つ足している
    void AddLeakDrops(const Leak& leak);

public:
    // 雨漏りを足す（最大 MaxLeakCount 個）・すべて消す・今の雨漏りの一覧
    void AddLeak(const DirectX::SimpleMath::Vector2& center, float maxRadius, float growSeconds);
    void ClearLeaks() { m_Leaks.clear(); }
    const std::vector<Leak>& GetLeaks() const { return m_Leaks; }

    const PuddleCellBufferData& GetPuddleCellData() const { return m_PuddleCellData; }
    // 水たまりを置き直している（2面）。シェーダーへの表は、Groundが書き込み直している
    void SetPuddleLayout(const PuddleLayout& layout) { BuildPuddles(layout); }
    // このフレームに起きた足音と水滴のおもりを受け取り、空にしている（波のシミュレーションへ渡すため）
    std::vector<DirectX::SimpleMath::Vector4> TakeRippleImpulses()
    {
        std::vector<DirectX::SimpleMath::Vector4> impulses;
        impulses.swap(m_RippleImpulses);
        return impulses;
    }

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
