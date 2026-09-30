// ============================================================================
// ファイルの役割: 1面の隠し部屋に落ちている鍵（拾うと閉じ込められた扉が開く）の表示と取得を管理します。
// 主な技術: Interactableインターフェース、箱を組み合わせたプロシージャルメッシュ、時間ベースアニメーション
// 拾ったかどうかはSceneが毎フレーム確認し、扉を開ける処理はStage1HiddenRoomEventが担当します。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

class KeyItem : public Object, public Interactable
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

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    void Uninit() override;

    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "鍵を拾う"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    void SetActive(bool active) { m_IsActive = active; }
    bool IsCollected() const { return m_IsCollected; }
};
