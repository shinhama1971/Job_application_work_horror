// ============================================================================
// ファイルの役割: ヒューズ（1面で集めるアイテム）の形、浮かぶ動き、拾ったときの処理を管理している。
// 主な技術: 箱を組み合わせた形をコードで生成、ゆっくり脈打つ大きさ、調べる操作、時間で回る・浮く動き
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

// 床の上で回りながら浮かんでいるヒューズ。調べると拾われて数が1つ増える。
class Item : public Object, public Interactable
{
private:
    // 頂点・インデックスとGPUのバッファ、マテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    // 拾われたか、配置が有効か、浮く動きの中心の高さ、動きの経過時間
    bool m_IsCollected = false;
    bool m_IsActive = true;
    float m_BaseY = 0.0f;
    float m_AnimationTime = 0.0f;

    // ガラスの筒・両端の金具・中の線（フィラメント）を箱で組み立てている
    void BuildGeometry();


public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 拾われていない有効なヒューズだけが、調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "ヒューズを拾う"; }
    void Interact(Player& player) override;

    // 置き場所を決めている（浮く動きの中心の高さも一緒に決めている）
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    // 拾われたかを返している
    bool IsCollected() const
    {
        return m_IsCollected;
    }

    // 進行に合わせて出したり消したりしている
    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    // 有効かを返している
    bool IsActive() const
    {
        return m_IsActive;
    }
};
