// ============================================================================
// ファイルの役割: キーボードとXInputコントローラーの入力の状態を集め、押した瞬間・離した瞬間を判定している。
// 主な技術: Win32のキー入力（GetKeyboardState）、XInput、前フレームとの比較による押した瞬間の判定、コントローラーの振動
// マウスの視点操作はCameraがカーソルの位置から直接読んでいる
// ============================================================================

#include "input.h"
#include "Application.h"

#include <algorithm>
#include <cmath>

namespace
{
	// スティックの値を-1〜1にし、中心付近の遊び（デッドゾーン）を0、その外側を0〜1に広げ直している
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

std::unique_ptr<Input> Input::m_Instance;

// インスタンスを作り、全部の状態を0で初期化している
void Input::Create()
{
	if (m_Instance)return;
	m_Instance = std::make_unique<Input>();

	ZeroMemory(m_Instance->keyState, sizeof(m_Instance->keyState));
	ZeroMemory(m_Instance->keyState_old, sizeof(m_Instance->keyState_old));
	ZeroMemory(&m_Instance->controllerState, sizeof(XINPUT_STATE));
	ZeroMemory(&m_Instance->controllerState_old, sizeof(XINPUT_STATE));
	m_Instance->controllerConnected = false;
	m_Instance->controllerIndex = XUSER_MAX_COUNT;
	m_Instance->VibrationTimeSeconds = 0.0f;
}

void Input::Update()
{
	// 前のフレームの入力を覚えておいている（押した瞬間・離した瞬間の判定に使う）
	for (int i = 0; i < 256; i++) { m_Instance->keyState_old[i] = m_Instance->keyState[i]; }
	m_Instance->controllerState_old = m_Instance->controllerState;

	// キーの状態を読み直している
	GetKeyboardState(m_Instance->keyState);

	// コントローラーの状態を読み直している（XInput）
	XINPUT_STATE nextControllerState{};
	DWORD connectedIndex = XUSER_MAX_COUNT;

	// 前につながっていたコントローラーを優先し、外れていたら0〜3番から最初に見つかったものを使っている
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

	// 振動を続ける残り時間を減らしている
	if (m_Instance->VibrationTimeSeconds > 0.0f) {
		m_Instance->VibrationTimeSeconds -= Application::GetDeltaTime();
		if (m_Instance->VibrationTimeSeconds <= 0.0f) { // 時間が過ぎたら振動を止めている
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
	// 全部のコントローラーの振動を止めている
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = 0;
	vibration.wRightMotorSpeed = 0;
	for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
	{
		XInputSetState(index, &vibration);
	}
	// インスタンスを解放している
	m_Instance.reset();
}

// キー入力（最上位ビットが1なら押されている）
bool Input::GetKeyPress(int key) // 押している間ずっとtrue
{
	return m_Instance->keyState[key] & 0x80;
}
bool Input::GetKeyTrigger(int key) // 押した瞬間だけtrue
{
	return (m_Instance->keyState[key] & 0x80) && !(m_Instance->keyState_old[key] & 0x80);
}
bool Input::GetKeyRelease(int key) // 離した瞬間だけtrue
{
	return !(m_Instance->keyState[key] & 0x80) && (m_Instance->keyState_old[key] & 0x80);
}

// 左スティック
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
// 右スティック
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

// 左トリガー
float Input::GetLeftTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bLeftTrigger; // 0〜255
	return t / 255.0f;
}
// 右トリガー
float Input::GetRightTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bRightTrigger; // 0〜255
	return t / 255.0f;
}

// ボタン入力
bool Input::GetButtonPress(WORD btn) // 押している間ずっとtrue
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0;
}
bool Input::GetButtonTrigger(WORD btn) // 押した瞬間だけtrue
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) == 0;
}
bool Input::GetButtonRelease(WORD btn) // 離した瞬間だけtrue
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) == 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) != 0;
}

// 振動を始めている
void Input::SetVibration(int frame, float powor)
{
	// 振動の設定
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

	// 左右のモーターの強さを設定している（0〜65535）
	vibration.wLeftMotorSpeed = (WORD)(powor * 65535.0f);
	vibration.wRightMotorSpeed = (WORD)(powor * 65535.0f);
	if (m_Instance->controllerConnected)
	{
		XInputSetState(m_Instance->controllerIndex, &vibration);
	}

	// 振動を続ける秒数を入れている（frameを60fpsのフレーム数として秒に直している）
	m_Instance->VibrationTimeSeconds = static_cast<float>(frame) / 60.0f;
}

