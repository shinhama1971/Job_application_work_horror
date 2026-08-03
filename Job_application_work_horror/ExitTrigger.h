#pragma once

#include "Object.h"
#include "Interactable.h"

class ExitTrigger : public Object, public Interactable
{
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    bool IsInteractionEnabled() const override { return true; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "Leave facility"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

};
