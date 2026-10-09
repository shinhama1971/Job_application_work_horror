// ============================================================================
// ファイルの役割: 出口を調べたときの判定と、演出の後に次のシーンへ移る要求を管理している。
// 主な技術: 調べる操作、電力の状態による条件、時間をずらしたシーンの切り替え
// ============================================================================

#pragma once

#include "Object.h"
#include "Interactable.h"

enum class SceneName;

// 見た目のない出口。電力が戻った後に調べると、脱出の演出の後に次のシーンへ移っている。
class ExitTrigger : public Object, public Interactable
{
private:
    // 脱出の演出中か、演出の経過秒、演出の段階、移る先のシーン、調べられるか
    bool m_IsEscaping = false;
    float m_EscapeTimer = 0.0f;
    int m_EscapePhase = 0;
    SceneName m_NextScene;
    bool m_InteractionEnabled = true;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    // 調べられる設定で、脱出の演出中でないときだけ調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return m_InteractionEnabled && !m_IsEscaping;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;
    // 脱出を始めている（演出から直接呼ぶこともある）
    void BeginEscape(Player& player);
    // 脱出の演出中か
    bool IsEscaping() const { return m_IsEscaping; }
    // 脱出の演出の進み具合（0〜1）を返している
    float GetEscapeProgress() const
    {
        const float progress = m_EscapeTimer / 2.12f;
        return progress < 0.0f
            ? 0.0f
            : (progress > 1.0f ? 1.0f : progress);
    }
    // 移る先のシーンを決めている
    void SetNextScene(SceneName nextScene) { m_NextScene = nextScene; }
    // 調べられるかどうかを切り替えている
    void SetInteractionEnabled(bool enabled) { m_InteractionEnabled = enabled; }

    // 置き場所を決めている
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

};
