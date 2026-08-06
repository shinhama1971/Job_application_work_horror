#pragma once

#include "Object.h"

class ScareTrigger : public Object
{
private:
    DirectX::SimpleMath::Vector3 m_Size =
        DirectX::SimpleMath::Vector3(10.0f, 10.0f, 10.0f);
    DirectX::SimpleMath::Vector3 m_ShadowPosition =
        DirectX::SimpleMath::Vector3::Zero;
    bool m_RequiresPower = false;
    bool m_HasTriggered = false;

    bool IsPlayerInside() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    void SetPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_Position = position;
    }

    void SetSize(const DirectX::SimpleMath::Vector3& size)
    {
        m_Size = size;
    }

    void SetShadowPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_ShadowPosition = position;
    }

    void SetRequiresPower(bool requiresPower)
    {
        m_RequiresPower = requiresPower;
    }
};
