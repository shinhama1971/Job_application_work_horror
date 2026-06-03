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
	m_CameraPitch = 0.0f; // 追加
}

void Camera::Update()
{
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

	// ピッチ制限（見上げ/見下ろししすぎ防止）
	const float maxPitch = 1.2f;
	const float minPitch = -1.2f;
	if (m_CameraPitch > maxPitch) m_CameraPitch = maxPitch;
	if (m_CameraPitch < minPitch) m_CameraPitch = minPitch;
	
	// FPS視点では、位置(m_Position)と注視点(m_Target)の更新は
	// GolfBall::Update() の中で行う
}


void Camera::SetCamera(int mode)
{
	//3D
	if (mode == 0)
	{
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

void Camera::Uninit() {}

void Camera::SetTarget(Vector3 target) { m_Target = target; }