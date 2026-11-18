// ============================================================================
// ファイルの役割: 一人称視点の位置・向きと、描画に使うビュー行列・射影行列を管理している。
// 主な技術: DirectXMath、ビュー行列、透視投影、マウスルック、ヨー・ピッチによる向きの制御
// ============================================================================

#include "Renderer.h"
#include "Camera.h"
#include "Application.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 最初の位置と向き（ヨー。約πなので -Z 方向を向く）。実際の位置と向きは、Scene が配置に合わせて設定し直す
    const Vector3 InitialPosition(0.0f, 20.0f, -50.0f);
    constexpr float InitialYaw = 3.14f;
    // 視点の速さ：マウス（1画素あたりのラジアン）、スティック（1秒あたりのラジアン。横・縦）、デバッグ用の方向キー
    constexpr float MouseRadiansPerPixel = 0.003f;
    constexpr float StickYawSpeed = 3.9f;
    constexpr float StickPitchSpeed = 3.0f;
    constexpr float ArrowKeyYawSpeed = 6.0f;
    // 視線が真上・真下に近く、右方向を作れないとみなす長さの2乗
    constexpr float MinimumRightLengthSquared = 0.0001f;
}

// 初期位置と向きを決め、マウスで視点を動かせる状態にしている（カーソルは隠している）。
// 実際の位置と向きは、Player::Initの後にSceneが配置に合わせて設定し直している。
void Camera::Init()
{
    m_Position = InitialPosition;
    // ヨー（水平の向き）。約πなので、最初は-Z方向を向いている
    m_CameraDirection = InitialYaw;
    m_CameraPitch = 0.0f;

    m_MouseLookEnable = true;
    m_FirstMouse = true;

    ShowCursor(FALSE);
}

// マウス・右スティック・方向キーの入力から、視点の向き（ヨーとピッチ）を1フレーム分更新している。
void Camera::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    // 上下は約±69度までに制限している。
    constexpr float MaxPitch = 1.2f;

    // ESCでマウス視点のON/OFFを切り替え、OFFの間はカーソルを表示している
    if (Input::GetKeyTrigger(VK_ESCAPE))
    {
        SetMouseLookEnabled(!m_MouseLookEnable);

        if (m_MouseLookEnable)
        {
            ShowCursor(FALSE);
        }
        else
        {
            ShowCursor(TRUE);
        }
    }

    // マウス視点：前フレームからのカーソルの移動量を視点の回転に変えている
    const HWND window = Application::GetWindow();
    RECT client{};
    POINT mousePos{};
    if (m_MouseLookEnable && window != nullptr && window == GetForegroundWindow() &&
        GetClientRect(window, &client) && GetCursorPos(&mousePos))
    {
        if (!m_FirstMouse)
        {
            float dx = (float)(mousePos.x - m_LastMousePos.x);
            float dy = (float)(mousePos.y - m_LastMousePos.y);

            // カーソルの移動量は実際の画素単位なので、拡大率が大きい画面では同じ手の動きでも値が大きくなる。
            // 拡大率で割って、拡大率に関係なく同じ視点の速さにしている。
            const float sensitivity =
                MouseRadiansPerPixel * m_LookSensitivityScale /
                (std::max)(Application::GetDpiScale(), 1.0f);

            m_CameraDirection += dx * sensitivity;
            m_CameraPitch += dy * sensitivity;

            m_CameraPitch = std::clamp(m_CameraPitch, -MaxPitch, MaxPitch);
        }

        // マウスカーソルをウィンドウ中央へ戻し、端で止まらずに回り続けられるようにしている（カーソルはウィンドウの座標なので、描画解像度ではなくウィンドウの大きさを使っている）
        POINT screenCenter =
        {
            (client.left + client.right) / 2,
            (client.top + client.bottom) / 2
        };

        m_FirstMouse = !(ClientToScreen(window, &screenCenter) && SetCursorPos(screenCenter.x, screenCenter.y));
        m_LastMousePos = screenCenter;
    }
    else
    {
        ResetMouseLook();
    }

    // コントローラーの右スティックで視点を回している。十字キーも同じ操作として扱っている
    const DirectX::XMFLOAT2 rightStick = Input::GetRightAnalogStick();
    float controllerLookX = rightStick.x;
    float controllerLookY = rightStick.y;

    if (Input::GetButtonPress(XINPUT_LEFT)) controllerLookX = -1.0f;
    if (Input::GetButtonPress(XINPUT_RIGHT)) controllerLookX = 1.0f;
    if (Input::GetButtonPress(XINPUT_UP)) controllerLookY = 1.0f;
    if (Input::GetButtonPress(XINPUT_DOWN)) controllerLookY = -1.0f;

    // スティックは時間あたりの回転速度として使い、横より縦を少し遅くしている
    const float controllerYawSpeed =
        StickYawSpeed * m_LookSensitivityScale * deltaTime;
    const float controllerPitchSpeed =
        StickPitchSpeed * m_LookSensitivityScale * deltaTime;
    m_CameraDirection += controllerLookX * controllerYawSpeed;
    m_CameraPitch += controllerLookY * controllerPitchSpeed;

    // デバッグ用：キーボードの方向キーでも視点を回せるようにしている
    if (m_ArrowKeyLookEnable)
    {
        if (Input::GetKeyPress(VK_LEFT))
        {
            m_CameraDirection += ArrowKeyYawSpeed * m_LookSensitivityScale * deltaTime;
        }

        if (Input::GetKeyPress(VK_RIGHT))
        {
            m_CameraDirection -= ArrowKeyYawSpeed * m_LookSensitivityScale * deltaTime;
        }

        const float pitchStep = 1.8f * m_LookSensitivityScale * deltaTime;

        if (Input::GetKeyPress(VK_UP))
        {
            m_CameraPitch += pitchStep;
        }

        if (Input::GetKeyPress(VK_DOWN))
        {
            m_CameraPitch -= pitchStep;
        }
    }

    // どの入力で動かしても、上下の向きは同じ範囲に制限している
    m_CameraPitch = std::clamp(m_CameraPitch, -MaxPitch, MaxPitch);
}

// 描画に使う行列をシェーダーへ設定している（mode 0は3D、それ以外はHUD用の2D）。
void Camera::SetCamera(int mode)
{
    if (mode == 0)
    {
        // 反射や監視カメラの映像を描いている間は、差し替えた行列を使っている
        if (m_UseOverrideMatrices)
        {
            Renderer::SetViewMatrix(&m_OverrideView);
            Renderer::SetProjectionMatrix(&m_OverrideProjection);
            return;
        }

        Matrix viewMatrix;
        Matrix projectionMatrix;
        GetMainMatrices(viewMatrix, projectionMatrix);
        Renderer::SetViewMatrix(&viewMatrix);
        Renderer::SetProjectionMatrix(&projectionMatrix);
    }
    else
    {
        // 2D：ビュー行列は単位行列にし、画面中央を原点とする正射影にしている（1単位＝1画素）
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

// 終了時に、隠していたマウスカーソルを表示に戻している
void Camera::Uninit()
{
    ShowCursor(TRUE);
}

// プレイヤー視点の本描画に使うビュー・射影行列を作っている（画角は縦60度、描く距離は1〜1000）。
// タイルベースのライトカリングも、同じ行列から各タイルの視錐台を作っている。
void Camera::GetMainMatrices(Matrix& view, Matrix& projection) const
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    view = DirectX::XMMatrixLookAtLH(m_Position, m_Position + GetForward(), up);

    constexpr float fieldOfView = DirectX::XMConvertToRadians(60.0f);
    constexpr float nearPlane = 1.0f;
    constexpr float farPlane = 1000.0f;
    const float aspectRatio =
        static_cast<float>(Application::GetWidth()) /
        static_cast<float>(Application::GetHeight());
    projection = DirectX::XMMatrixPerspectiveFovLH(
        fieldOfView, aspectRatio, nearPlane, farPlane);
}

// 反射や監視カメラなど別の視点で描く間だけ、SetCamera(0)が使う行列を差し替えている。
void Camera::SetOverrideMatrices(
    const Matrix& view,
    const Matrix& projection)
{
    m_OverrideView = view;
    m_OverrideProjection = projection;
    m_UseOverrideMatrices = true;
}

// 差し替えをやめ、プレイヤー視点の行列に戻している。
void Camera::ClearOverrideMatrices()
{
    m_UseOverrideMatrices = false;
}

// ヨーとピッチから、視線の向きの単位ベクトルを計算している（ヨー0で+Z方向）。
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

// 境界球（中心と半径）が今の視野に入るかを、軽い計算で判定している（描画しない物を減らすため）。
bool Camera::IsSphereVisible(
    const Vector3& center,
    float radius,
    bool testVertical) const
{
    // 描画側の射影行列（縦60度、far 1000）に合わせた軽量な視錐台判定。
    // 正確な6平面の判定より余白を広く取り、細長い壁なども見えているのに消えることがないよう、見える側に寄せている。
    radius = (std::max)(radius, 1.0f);
    // 描く距離（1000）より遠ければ見えない
    const Vector3 offset = center - m_Position;
    const float distanceSquared =
        offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    const float farDistance = 1000.0f + radius;
    if (distanceSquared > farDistance * farDistance)
    {
        return false;
    }

    // 視線方向の奥行きを求め、カメラより後ろにあれば見えない
    const Vector3 forward = GetForward();
    const float depth =
        offset.x * forward.x + offset.y * forward.y + offset.z * forward.z;
    if (depth + radius < 0.5f)
    {
        return false;
    }

    // 視線に対する右方向と上方向を作っている（真上・真下を向いたときは右を+Xとしている）
    Vector3 right(forward.z, 0.0f, -forward.x);
    const float rightLengthSquared =
        right.x * right.x + right.z * right.z;
    if (rightLengthSquared <= MinimumRightLengthSquared)
    {
        right = Vector3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        right /= std::sqrt(rightLengthSquared);
    }
    Vector3 up = forward.Cross(right);
    up.Normalize();

    // tan(30度)。画角60度の半分
    constexpr float tanHalfVerticalFov = 0.57735026919f;
    const float height = static_cast<float>((std::max)(Application::GetHeight(), 1u));
    const float aspect = static_cast<float>(Application::GetWidth()) / height;
    const float positiveDepth = (std::max)(depth, 0.0f);
    const float safetyRadius = radius * 1.35f + 2.0f;
    // 左右：奥行きに応じた視野の幅（＋余白）より外にあれば見えない
    const float horizontal = std::abs(
        offset.x * right.x + offset.y * right.y + offset.z * right.z);
    if (horizontal > positiveDepth * tanHalfVerticalFov * aspect + safetyRadius)
    {
        return false;
    }

    // 上下：testVerticalがtrueのときだけ同じように判定している（影の描画では画面の上下の外にある物も影を落とし、水面の反射では上下が反転するため、どちらも上下は判定しない）
    if (testVertical)
    {
        const float vertical = std::abs(
            offset.x * up.x + offset.y * up.y + offset.z * up.z);
        if (vertical > positiveDepth * tanHalfVerticalFov + safetyRadius)
        {
            return false;
        }
    }

    return true;
}
