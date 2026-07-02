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

	// マウスカーソルを非表示にする
	ShowCursor(FALSE);
}

void Camera::Update()
{
	// マウス移動で視線制御
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

	// マウスカーソルをウィンドウの中央に戻す
	int screenWidth = Application::GetWidth();
	int screenHeight = Application::GetHeight();
	POINT screenCenter = { screenWidth / 2, screenHeight / 2 };

	// ウィンドウの座標に変換
	HWND hWnd = GetActiveWindow();
	if (hWnd)
	{
		ClientToScreen(hWnd, &screenCenter);
		SetCursorPos(screenCenter.x, screenCenter.y);
		m_LastMousePos = screenCenter;
	}

	// 左右キーでカメラ回転（Yaw）
	if (Input::GetKeyPress(VK_LEFT))
	{
		m_CameraDirection += 0.05f;
	}
	if (Input::GetKeyPress(VK_RIGHT))
	{
		m_CameraDirection -= 0.05f;
	}

	// 上下キーでカメラ上下（Pitch）
	const float pitchStep = 0.03f;
	if (Input::GetKeyPress(VK_UP))
	{
		m_CameraPitch += pitchStep;
	}
	if (Input::GetKeyPress(VK_DOWN))
	{
		m_CameraPitch -= pitchStep;
	}
}

void Camera::SetCamera(int mode)
{
	//3D
	if (mode == 0)
	{
		// カメラの向きからターゲット位置を計算
		Vector3 forward;
		forward.x = cosf(m_CameraPitch) * sinf(m_CameraDirection);
		forward.y = -sinf(m_CameraPitch);
		forward.z = cosf(m_CameraPitch) * cosf(m_CameraDirection);
		forward.Normalize();

		m_Target = m_Position + forward;

		Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
		m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Target, up);

		Renderer::SetViewMatrix(&m_ViewMatrix);

		constexpr float fieldOfView = DirectX::XMConvertToRadians(45.0f);
		float aspectRatio = static_cast<float>(Application::GetWidth()) / static_cast<float>(Application::GetHeight());
		float nearPlane = 1.0f;
		float farPlane = 1000.0f;

		Matrix projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fieldOfView, aspectRatio, nearPlane, farPlane);
		Renderer::SetProjectionMatrix(&projectionMatrix);
	}
	/*
	//2D
	else if (mode == 1)
	{
		Vector3 pos = { 0.0f,0.0f,-10.0f };
		Vector3 tgt = { 0.0f,0.0f,1.0f };
		Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
		m_ViewMatrix = DirectX::XMMatrixLookAtLH(pos, tgt, up);
		Renderer::SetViewMatrix(&m_ViewMatrix);

		float nearPlane = 1.0f;
		float farPlane = 1000.0f;
		Matrix projectionMatrix = DirectX::XMMatrixOrthographicLH(
			static_cast<float>(Application::GetWidth()),
			static_cast<float>(Application::GetHeight()),
			nearPlane, farPlane);
		projectionMatrix = DirectX::XMMatrixTranspose(projectionMatrix);
		Renderer::SetProjectionMatrix(&projectionMatrix);
	}*/
}

void Camera::Uninit() 
{
	// マウスカーソルを再表示
	ShowCursor(TRUE);
}

void Camera::SetTarget(Vector3 target) { m_Target = target; }
