// ============================================================================
// ファイルの役割: 配電盤（ヒューズを入れて電力を戻す）と、同じ形を使うスイッチ類の、調べたときの処理と見た目を管理している。
// 主な技術: Interactableインターフェース、有限状態機械、進行の条件、照明・効果音・画面効果との連携
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "Interactable.h"

// 壁に付いた箱。主電源の配電盤・出口の送電盤・各種スイッチの3つの役割で使い回している。
class FuseBox : public Object, public Interactable
{
private:
    // 頂点・インデックスとGPUのバッファ、マテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    // 電力を入れたか（スイッチなら押したか）
    bool m_IsPowered = false;
    // 出口の送電盤として使うか
    bool m_IsExitControl = false;
    // スイッチとして使うか、今押せるか、調べるときの文章
    bool m_IsManualControl = false;
    bool m_ManualInteractionAllowed = false;
    const char* m_ManualPrompt = "スイッチを操作する";

    // 電力の状態に合わせて形と色を作っている
    void BuildGeometry();

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 電力を入れる前で、スイッチなら押せる状態のときだけ調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return !m_IsPowered &&
            (!m_IsManualControl || m_ManualInteractionAllowed);
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override
    {
        return m_Position;
    }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;

    // 出口の送電盤として使うかを決めている
    void SetExitControl(bool enabled) { m_IsExitControl = enabled; }
    // スイッチとして使うことにし、調べるときの文章を決めている
    void SetManualControl(const char* prompt)
    {
        m_IsManualControl = true;
        m_ManualPrompt = prompt;
    }
    // スイッチを押せるかどうかを切り替えている
    void SetManualInteractionAllowed(bool allowed)
    {
        m_ManualInteractionAllowed = allowed;
    }
    // 電力を入れていない状態に戻している
    void ResetActivation();
    // 電力を入れたか（スイッチなら押したか）を返している
    bool IsActivated() const { return m_IsPowered; }

    // 置き場所を決めている
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};
