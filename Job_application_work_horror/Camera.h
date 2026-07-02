#pragma once

#include	<SimpleMath.h>

class Camera {
private:
	POINT m_LastMousePos{};
	bool m_FirstMouse = true;
	DirectX::SimpleMath::Vector3	m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3	m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3	m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

	DirectX::SimpleMath::Vector3	m_Target{};
	DirectX::SimpleMath::Matrix		m_ViewMatrix{};

	float m_CameraDirection = 0; // �J�����̕����iYaw�j
	float m_CameraPitch = 0.0f;  // �㉺�p�iPitch�j

public:
	void Init();
	void Update();
	void UpdateMouseLook();
	void SetCamera(int mode = 0);
	void Uninit();
	void SetTarget(DirectX::SimpleMath::Vector3 target);

	void SetPosition(DirectX::SimpleMath::Vector3 pos) { m_Position = pos; }
	float GetCameraDirection() const { return m_CameraDirection; }
	float GetCameraPitch() const { return m_CameraPitch; } // ���ǉ�
};
