#pragma once

#include <Windows.h>
#include <SimpleMath.h>

class Camera
{
private:
    POINT m_LastMousePos{};
    bool m_FirstMouse = true;

    bool m_MouseLookEnable = true;

    DirectX::SimpleMath::Vector3 m_Position =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Rotation =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Scale =
        DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

    DirectX::SimpleMath::Vector3 m_Target{};
    DirectX::SimpleMath::Matrix m_ViewMatrix{};

    float m_CameraDirection = 0.0f;
    float m_CameraPitch = 0.0f;

public:
    void Init();
    void Update();
    void SetCamera(int mode = 0);
    void Uninit();

    void SetTarget(DirectX::SimpleMath::Vector3 target);

    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
    }

    DirectX::SimpleMath::Vector3 GetPosition() const
    {
        return m_Position;
    }

    float GetCameraDirection() const
    {
        return m_CameraDirection;
    }

    float GetCameraPitch() const
    {
        return m_CameraPitch;
    }

    DirectX::SimpleMath::Vector3 GetForward() const;
};