#pragma once

#include "Object.h"
#include "Interactable.h"

enum class SceneName;

class ExitTrigger : public Object, public Interactable
{
private:
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

    bool IsInteractionEnabled() const override
    {
        return m_InteractionEnabled && !m_IsEscaping;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;
    bool IsEscaping() const { return m_IsEscaping; }
    float GetEscapeProgress() const
    {
        const float progress = m_EscapeTimer / 2.12f;
        return progress < 0.0f
            ? 0.0f
            : (progress > 1.0f ? 1.0f : progress);
    }
    void SetNextScene(SceneName nextScene) { m_NextScene = nextScene; }
    void SetInteractionEnabled(bool enabled) { m_InteractionEnabled = enabled; }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

};
