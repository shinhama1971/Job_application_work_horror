// ============================================================================
// ファイルの役割: 1面に落ちている鍵（隠し部屋の扉の鍵・西棟の扉の鍵）の見た目と、拾ったときの処理を管理している。
// 主な技術: Interactableインターフェース、箱を組み合わせた形をコードで生成、時間で回る・浮く動き
// 拾ったかどうかはSceneが毎フレーム確かめ、扉を開ける処理はStage1HiddenRoomEventとStage1WestWingが担当している。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

// 床の上で回りながら浮かんでいる鍵。調べると拾われて消える。
class KeyItem : public Object, public Interactable
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

    // 持ち手の輪・軸・歯を箱で組み立てている
    void BuildGeometry();

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    void Uninit() override;

    // 拾われていない有効な鍵だけが、調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "鍵を拾う"; }
    void Interact(Player& player) override;

    // 置き場所を決めている（浮く動きの中心の高さも一緒に決めている）
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
        m_BaseY = y;
    }

    // 進行に合わせて出したり消したりしている・拾われたかを返している
    void SetActive(bool active) { m_IsActive = active; }
    bool IsCollected() const { return m_IsCollected; }
};
