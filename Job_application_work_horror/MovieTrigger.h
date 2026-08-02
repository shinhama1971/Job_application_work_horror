#pragma once

#include "Object.h"
#include <SimpleMath.h>

class MovieTrigger : public Object
{
private:
    DirectX::SimpleMath::Vector3 m_Size =
        DirectX::SimpleMath::Vector3(5.0f, 5.0f, 5.0f);

    DirectX::SimpleMath::Vector3 m_CameraEndPos;
    DirectX::SimpleMath::Vector3 m_CameraEndTarget;

    float m_Duration = 2.0f;
    bool m_IsPlayed = false;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetSize(const DirectX::SimpleMath::Vector3& size)
    {
        m_Size = size;
    }

    void SetCameraEnd(
        const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& target
    )
    {
        m_CameraEndPos = pos;
        m_CameraEndTarget = target;
    }

    void SetDuration(float duration)
    {
        m_Duration = duration;
    }

private:
    bool CheckPlayerInside();
};