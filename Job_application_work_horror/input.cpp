#include "input.h"

#include <algorithm>
#include <cmath>

namespace
{
	float NormalizeStickAxis(SHORT value, SHORT deadZone)
	{
		const float normalized = value < 0
			? static_cast<float>(value) / 32768.0f
			: static_cast<float>(value) / 32767.0f;
		const float magnitude = std::abs(normalized);
		const float deadZoneRate = static_cast<float>(deadZone) / 32767.0f;

		if (magnitude <= deadZoneRate)
		{
			return 0.0f;
		}

		const float scaled = (magnitude - deadZoneRate) / (1.0f - deadZoneRate);
		return std::copysign((std::min)(scaled, 1.0f), normalized);
	}
}

Input* Input::m_Instance = {};

void Input::Create()
{
	if (m_Instance)return;
	m_Instance = new Input;

	ZeroMemory(m_Instance->keyState, sizeof(m_Instance->keyState));
	ZeroMemory(m_Instance->keyState_old, sizeof(m_Instance->keyState_old));
	ZeroMemory(&m_Instance->controllerState, sizeof(XINPUT_STATE));
	ZeroMemory(&m_Instance->controllerState_old, sizeof(XINPUT_STATE));
	m_Instance->controllerConnected = false;
	m_Instance->controllerIndex = XUSER_MAX_COUNT;
	m_Instance->VibrationTime = 0;
}

void Input::Update()
{
	//1フレーム前の入力を記録しておく
	for (int i = 0; i < 256; i++) { m_Instance->keyState_old[i] = m_Instance->keyState[i]; }
	m_Instance->controllerState_old = m_Instance->controllerState;

	//キー入力を更新
	BOOL hr = GetKeyboardState(m_Instance->keyState);

	//コントローラー入力を更新(XInput)
	XINPUT_STATE nextControllerState{};
	DWORD connectedIndex = XUSER_MAX_COUNT;

	if (m_Instance->controllerIndex < XUSER_MAX_COUNT &&
		XInputGetState(
			m_Instance->controllerIndex,
			&nextControllerState) == ERROR_SUCCESS)
	{
		connectedIndex = m_Instance->controllerIndex;
	}
	else
	{
		for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
		{
			ZeroMemory(&nextControllerState, sizeof(XINPUT_STATE));
			if (XInputGetState(index, &nextControllerState) == ERROR_SUCCESS)
			{
				connectedIndex = index;
				break;
			}
		}
	}

	m_Instance->controllerConnected = connectedIndex < XUSER_MAX_COUNT;
	m_Instance->controllerIndex = connectedIndex;
	if (m_Instance->controllerConnected)
	{
		m_Instance->controllerState = nextControllerState;
	}
	else
	{
		ZeroMemory(&m_Instance->controllerState, sizeof(XINPUT_STATE));
	}

	//振動継続時間をカウント
	if (m_Instance->VibrationTime > 0) {
		m_Instance->VibrationTime--;
		if (m_Instance->VibrationTime == 0) { //振動継続時間が経った時に振動を止める
			XINPUT_VIBRATION vibration;
			ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
			vibration.wLeftMotorSpeed = 0;
			vibration.wRightMotorSpeed = 0;
			if (m_Instance->controllerConnected)
			{
				XInputSetState(m_Instance->controllerIndex, &vibration);
			}
		}
	}
}

bool Input::IsControllerConnected()
{
	return m_Instance != nullptr && m_Instance->controllerConnected;
}

void Input::Release()
{
	//振動を終了させる
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = 0;
	vibration.wRightMotorSpeed = 0;
	for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
	{
		XInputSetState(index, &vibration);
	}
	//解放
	if (m_Instance)
	{
		delete m_Instance;
		m_Instance = NULL;
	}
}

//キー入力
bool Input::GetKeyPress(int key) //プレス
{
	return m_Instance->keyState[key] & 0x80;
}
bool Input::GetKeyTrigger(int key) //トリガー
{
	return (m_Instance->keyState[key] & 0x80) && !(m_Instance->keyState_old[key] & 0x80);
}
bool Input::GetKeyRelease(int key) //リリース
{
	return !(m_Instance->keyState[key] & 0x80) && (m_Instance->keyState_old[key] & 0x80);
}

//左アナログスティック
DirectX::XMFLOAT2 Input::GetLeftAnalogStick(void)
{
	return DirectX::XMFLOAT2(
		NormalizeStickAxis(
			m_Instance->controllerState.Gamepad.sThumbLX,
			XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
		NormalizeStickAxis(
			m_Instance->controllerState.Gamepad.sThumbLY,
			XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
	);
}
//右アナログスティック
DirectX::XMFLOAT2 Input::GetRightAnalogStick(void)
{
	return DirectX::XMFLOAT2(
		NormalizeStickAxis(
			m_Instance->controllerState.Gamepad.sThumbRX,
			XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
		NormalizeStickAxis(
			m_Instance->controllerState.Gamepad.sThumbRY,
			XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
	);
}

//左トリガー
float Input::GetLeftTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bLeftTrigger; // 0～255
	return t / 255.0f;
}
//右トリガー
float Input::GetRightTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bRightTrigger; // 0～255
	return t / 255.0f;
}

//ボタン入力
bool Input::GetButtonPress(WORD btn) //プレス
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0;
}
bool Input::GetButtonTrigger(WORD btn) //トリガー
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) == 0;
}
bool Input::GetButtonRelease(WORD btn) //リリース
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) == 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) != 0;
}

//振動
void Input::SetVibration(int frame, float powor)
{
	// XINPUT_VIBRATION構造体のインスタンスを作成
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

	// モーターの強度を設定（0～65535）
	vibration.wLeftMotorSpeed = (WORD)(powor * 65535.0f);
	vibration.wRightMotorSpeed = (WORD)(powor * 65535.0f);
	if (m_Instance->controllerConnected)
	{
		XInputSetState(m_Instance->controllerIndex, &vibration);
	}

	//振動継続時間を代入
	m_Instance->VibrationTime = frame;
}

