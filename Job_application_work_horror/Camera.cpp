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

        // なめらか補間
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

            float sensitivity = 0.003f;

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
    if (Input::GetKeyPress(VK_LEFT))
    {
        m_CameraDirection += 0.05f;
    }

    if (Input::GetKeyPress(VK_RIGHT))
    {
        m_CameraDirection -= 0.05f;
    }

    const float pitchStep = 0.03f;

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
            DirectX::XMConvertToRadians(45.0f);

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
}

void Camera::Uninit()
{
    ShowCursor(TRUE);
}

void Camera::SetTarget(Vector3 target)
{
    m_Target = target;
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