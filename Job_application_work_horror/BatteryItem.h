// ============================================================================
// ファイルの役割: 懐中電灯の電池を回復するアイテムの形・見た目・取ったときの演出を管理している。
// 主な技術: Interactableインターフェース、円柱の頂点をコードで生成、マテリアルの使い分け、時間で回る・浮く動き
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

// 床に置かれた電池。調べると懐中電灯の電池を回復し、そのまま消える。
class BatteryItem : public Object, public Interactable
{
private:
    // 頂点とインデックス（BuildGeometryで作り、GPUのバッファへ送っている）
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    // 本体・金属部分・残量の帯のマテリアル
    std::unique_ptr<Material> m_BodyMaterial;
    std::unique_ptr<Material> m_MetalMaterial;
    std::unique_ptr<Material> m_ChargeMaterial;

    // マテリアルごとに描くインデックスの範囲
    unsigned int m_BodyIndexCount = 0;
    unsigned int m_MetalIndexStart = 0;
    unsigned int m_MetalIndexCount = 0;
    unsigned int m_ChargeIndexStart = 0;
    unsigned int m_ChargeIndexCount = 0;

    // 拾われたか、配置が有効か、回復量（%）、浮く動きの中心の高さ、動きの経過時間
    bool m_IsCollected = false;
    bool m_IsActive = true;
    float m_RecoverValue = 30.0f;
    float m_BaseY = 0.0f;
    float m_AnimationTime = 0.0f;

    // 円柱を重ねて電池の形を作っている
    void BuildGeometry();

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 拾われていない間だけ、調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "電池を拾う"; }
    void Interact(Player& player) override;

    // 置き場所を決めている（浮く動きの中心の高さも一緒に決めている）
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    // 進行に合わせて出したり消したりしている
    void SetActive(bool active)
    {
        m_IsActive = active;
    }
};
