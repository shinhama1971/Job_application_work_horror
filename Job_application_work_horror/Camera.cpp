// ============================================================================
// ファイルの役割: 一人称視点の位置、向き、ビュー行列、射影行列を管理します。
// ============================================================================

#include "Renderer.h"
#include "Camera.h"
#include "Application.h"
#include "Input.h"

using namespace DirectX::SimpleMath;

void Camera::Init()
{
    m_Position = Vector3(0.0f, 20.0f, -50.0f);
    m_Target = Vector3(0.0f, 0.0f, 0.0f);

    m_CameraDirection = 3.14f;
    m_CameraPitch = 0.0f;

    m_MouseLookEnable = true;
    m_FirstMouse = true;

    ShowCursor(FALSE);
}

void Camera::Update()
{

    if (m_IsMovie)
    {
        m_MovieTimer += 1.0f / 60.0f;

        float t = m_MovieTimer / m_MovieDuration;
        if (t > 1.0f) t = 1.0f;

        // なめらかに動くようにする補間
        t = t * t * (3.0f - 2.0f * t);

        m_Position = Vector3::Lerp(m_MovieStartPos, m_MovieEndPos, t);
        m_Target = Vector3::Lerp(m_MovieStartTarget, m_MovieEndTarget, t);

        if (m_MovieTimer >= m_MovieDuration)
        {
            m_IsMovie = false;
        }

        return;
    }

    // ESCでマウス視点ON/OFF
    if (Input::GetKeyTrigger(VK_ESCAPE))
    {
        m_MouseLookEnable = !m_MouseLookEnable;
        m_FirstMouse = true;

        if (m_MouseLookEnable)
        {
            ShowCursor(FALSE);
        }
        else
        {
            ShowCursor(TRUE);
        }
    }

    if (m_MouseLookEnable)
    {
        POINT mousePos;
        GetCursorPos(&mousePos);

        if (m_FirstMouse)
        {
            m_LastMousePos = mousePos;
            m_FirstMouse = false;
        }
        else
        {
            float dx = (float)(mousePos.x - m_LastMousePos.x);
            float dy = (float)(mousePos.y - m_LastMousePos.y);

            const float sensitivity =
                0.003f * m_LookSensitivityScale;

            m_CameraDirection += dx * sensitivity;
            m_CameraPitch += dy * sensitivity;

            const float maxPitch = 1.2f;
            const float minPitch = -1.2f;

            if (m_CameraPitch > maxPitch) m_CameraPitch = maxPitch;
            if (m_CameraPitch < minPitch) m_CameraPitch = minPitch;
        }

        // マウスカーソルをウィンドウ中央へ戻す
        int screenWidth = Application::GetWidth();
        int screenHeight = Application::GetHeight();

        POINT screenCenter =
        {
            screenWidth / 2,
            screenHeight / 2
        };

        HWND hWnd = GetActiveWindow();

        if (hWnd)
        {
            ClientToScreen(hWnd, &screenCenter);
            SetCursorPos(screenCenter.x, screenCenter.y);
            m_LastMousePos = screenCenter;
        }
    }

    // デバッグ用：矢印キーでも視点操作
    const DirectX::XMFLOAT2 rightStick = Input::GetRightAnalogStick();
    float controllerLookX = rightStick.x;
    float controllerLookY = rightStick.y;

    if (Input::GetButtonPress(XINPUT_LEFT)) controllerLookX = -1.0f;
    if (Input::GetButtonPress(XINPUT_RIGHT)) controllerLookX = 1.0f;
    if (Input::GetButtonPress(XINPUT_UP)) controllerLookY = 1.0f;
    if (Input::GetButtonPress(XINPUT_DOWN)) controllerLookY = -1.0f;

    const float controllerYawSpeed =
        0.065f * m_LookSensitivityScale;
    const float controllerPitchSpeed =
        0.050f * m_LookSensitivityScale;
    m_CameraDirection += controllerLookX * controllerYawSpeed;
    m_CameraPitch += controllerLookY * controllerPitchSpeed;

    if (Input::GetKeyPress(VK_LEFT))
    {
        m_CameraDirection += 0.05f * m_LookSensitivityScale;
    }

    if (Input::GetKeyPress(VK_RIGHT))
    {
        m_CameraDirection -= 0.05f * m_LookSensitivityScale;
    }

    const float pitchStep = 0.03f * m_LookSensitivityScale;

    if (Input::GetKeyPress(VK_UP))
    {
        m_CameraPitch += pitchStep;
    }

    if (Input::GetKeyPress(VK_DOWN))
    {
        m_CameraPitch -= pitchStep;
    }

    const float maxPitch = 1.2f;
    const float minPitch = -1.2f;

    if (m_CameraPitch > maxPitch) m_CameraPitch = maxPitch;
    if (m_CameraPitch < minPitch) m_CameraPitch = minPitch;
}

void Camera::SetCamera(int mode)
{
    if (mode == 0)
    {
        if (m_UseOverrideMatrices)
        {
            Renderer::SetViewMatrix(&m_OverrideView);
            Renderer::SetProjectionMatrix(&m_OverrideProjection);
            return;
        }

        Vector3 forward = GetForward();

        m_Target = m_Position + forward;

        Vector3 up = Vector3(0.0f, 1.0f, 0.0f);

        m_ViewMatrix =
            DirectX::XMMatrixLookAtLH(
                m_Position,
                m_Target,
                up
            );

        Renderer::SetViewMatrix(&m_ViewMatrix);

        constexpr float fieldOfView =
            DirectX::XMConvertToRadians(60.0f);

        float aspectRatio =
            static_cast<float>(Application::GetWidth()) /
            static_cast<float>(Application::GetHeight());

        float nearPlane = 1.0f;
        float farPlane = 1000.0f;

        Matrix projectionMatrix =
            DirectX::XMMatrixPerspectiveFovLH(
                fieldOfView,
                aspectRatio,
                nearPlane,
                farPlane
            );

        Renderer::SetProjectionMatrix(&projectionMatrix);
    }
    else
    {
        Matrix viewMatrix = Matrix::Identity;
        Renderer::SetViewMatrix(&viewMatrix);

        const float halfWidth = static_cast<float>(Application::GetWidth()) * 0.5f;
        const float halfHeight = static_cast<float>(Application::GetHeight()) * 0.5f;

        Matrix projectionMatrix = DirectX::XMMatrixOrthographicOffCenterLH(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            0.0f,
            1.0f
        );
        Renderer::SetProjectionMatrix(&projectionMatrix);
    }
}

void Camera::Uninit()
{
    ShowCursor(TRUE);
}

void Camera::SetTarget(Vector3 target)
{
    m_Target = target;
}

void Camera::SetOverrideMatrices(
    const Matrix& view,
    const Matrix& projection)
{
    m_OverrideView = view;
    m_OverrideProjection = projection;
    m_UseOverrideMatrices = true;
}

void Camera::ClearOverrideMatrices()
{
    m_UseOverrideMatrices = false;
}

Vector3 Camera::GetForward() const
{
    float cp = cosf(m_CameraPitch);

    Vector3 forward(
        sinf(m_CameraDirection) * cp,
        sinf(m_CameraPitch),
        cosf(m_CameraDirection) * cp
    );

    forward.Normalize();

    return forward;
}

void Camera::StartMovieLook(
    const Vector3& endPos,
    const Vector3& endTarget,
    float duration
)
{
    m_IsMovie = true;
    m_MovieTimer = 0.0f;
    m_MovieDuration = duration;

    m_MovieStartPos = m_Position;
    m_MovieEndPos = endPos;

    m_MovieStartTarget = m_Target;
    m_MovieEndTarget = endTarget;
}
