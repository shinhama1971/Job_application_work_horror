#pragma once

#include "Object.h"

class ExitTrigger : public Object
{
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

private:
    float m_Radius = 30.0f;
};