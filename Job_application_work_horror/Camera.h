// ============================================================================
// ファイルの役割: 一人称視点の位置、向き、ビュー行列、射影行列を管理します。
// ============================================================================

#pragma once

#include <Windows.h>
#include <SimpleMath.h>

class Camera
{
private:
    POINT m_LastMousePos{};
    bool m_FirstMouse = true;

    bool m_MouseLookEnable = true;
    float m_LookSensitivityScale = 1.0f;

    DirectX::SimpleMath::Vector3 m_Position =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Rotation =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Scale =
        DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

    DirectX::SimpleMath::Vector3 m_Target{};
    DirectX::SimpleMath::Matrix m_ViewMatrix{};

    bool m_UseOverrideMatrices = false;
    DirectX::SimpleMath::Matrix m_OverrideView =
        DirectX::SimpleMath::Matrix::Identity;
    DirectX::SimpleMath::Matrix m_OverrideProjection =
        DirectX::SimpleMath::Matrix::Identity;

    float m_CameraDirection = 0.0f;
    float m_CameraPitch = 0.0f;

    bool m_IsMovie = false;
    float m_MovieTimer = 0.0f;
    float m_MovieDuration = 2.0f;

    DirectX::SimpleMath::Vector3 m_MovieStartPos;
    DirectX::SimpleMath::Vector3 m_MovieEndPos;

    DirectX::SimpleMath::Vector3 m_MovieStartTarget;
    DirectX::SimpleMath::Vector3 m_MovieEndTarget;


public:
    void Init();
    void Update();
    void SetCamera(int mode = 0);
    void Uninit();

    void SetLookSensitivityScale(float scale)
    {
        m_LookSensitivityScale = scale < 0.55f
            ? 0.55f
            : (scale > 1.55f ? 1.55f : scale);
    }

    void SetTarget(DirectX::SimpleMath::Vector3 target);

    void SetOverrideMatrices(
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& projection);
    void ClearOverrideMatrices();

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

    void StartMovieLook(
        const DirectX::SimpleMath::Vector3& endPos,
        const DirectX::SimpleMath::Vector3& endTarget,
        float duration
    );

    bool IsMovie() const
    {
        return m_IsMovie;
    }

    DirectX::SimpleMath::Vector3 GetForward() const;
};
