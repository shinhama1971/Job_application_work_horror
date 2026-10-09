// ============================================================================
// ファイルの役割: 扉の形と描画、開くときの動き、鍵がかかっているときの反応、プレイヤーとの当たり判定を管理している。
// 主な技術: 有限状態機械、拡大・回転・移動の行列、蝶番を軸にした回転、回転を打ち消した空間での当たり判定、調べる操作
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "Interactable.h"

// 調べると開く扉。鍵をかけておくこともでき、閉じている間はプレイヤーと音をさえぎっている。
class Door : public Object, public Interactable
{
private:
    // 頂点・インデックスとGPUのバッファ、扉本体と漏れ光のマテリアル、扉本体のインデックス数
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;
    std::unique_ptr<Material> m_LeakMaterial;
    size_t m_DoorIndexCount = 0;

    // 開ききったか、開いている途中か、鍵がかかっているか、鍵がかかった扉を揺らす残り秒数
    bool m_IsOpen = false;
    bool m_IsOpening = false;
    bool m_IsLocked = false;
    float m_LockedRattleTimer = 0.0f;

    // 置いた位置（閉じた状態の扉の中心）
    DirectX::SimpleMath::Vector3 m_StartPosition;
    // 開いた角度（ラジアン）、開く速さ、開く前のためる残り秒数とその長さ、ループ廊下の周回
    float m_OpenAngle = 0.0f;
    float m_OpenSpeedPerSecond = 1.92f;
    float m_OpenDelayTimer = 0.0f;
    float m_OpenDelayDuration = 0.06f;
    int m_LoopPhase = 0;

    // 蝶番を軸に開いた角度を含めた、扉のワールド行列を作っている
    DirectX::SimpleMath::Matrix GetDoorWorldMatrix() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return true; }
    // 扉の本体は不透明なので、深度プリパスに本体だけを描いている（隙間の漏れ光は描かない）
    bool WritesDepthPrepass() const override { return true; }
    void DrawDepthPrepass(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 閉じている間だけ調べる対象になっている
    bool IsInteractionEnabled() const override { return !m_IsOpen && !m_IsOpening; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override
    {
        return DirectX::SimpleMath::Vector3(
            m_Position.x, m_Position.y, m_Position.z);
    }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;
    // プレイヤーが閉じた扉板にめり込んでいたら外へ押し出している
    void ResolveCollision(
        DirectX::SimpleMath::Vector3& position,
        float radius) const;
    // 閉じた扉板が線分をさえぎるかを返している。立体音響で、扉の向こうの音をこもらせるために使っている。
    bool BlocksSoundSegment(
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end) const;
    // 閉じた状態に戻し、周回に合わせた開き方を決めている（鍵は外れる）
    void ResetClosed(int loopPhase = 0);
    // 監視カメラの異常用に、操作されないまま開いた状態へ切り替えている。
    // 音や画面効果は呼び出し側で出している。
    void ForceOpen()
    {
        m_IsOpen = true;
        m_IsOpening = false;
        m_OpenAngle = 1.50f;
        m_LockedRattleTimer = 0.0f;
        m_OpenDelayTimer = 0.0f;
    }
    // 鍵をかける・外す、鍵がかかっているかを返している
    void SetLocked(bool locked) { m_IsLocked = locked; }
    bool IsLocked() const { return m_IsLocked; }

    // 置き場所を決めている（閉じた位置としても覚えている）
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_StartPosition = m_Position;
    }

    // 開ききったかを返している
    bool IsOpen() const
    {
        return m_IsOpen;
    }
};
