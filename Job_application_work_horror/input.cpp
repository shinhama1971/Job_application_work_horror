// ============================================================================
// ファイルの役割: キーボードとXInputコントローラーの入力の状態を集め、押した瞬間・離した瞬間を判定している。
// 主な技術: Win32のキー入力（GetKeyboardState）、XInput、前フレームとの比較による押した瞬間の判定、コントローラーの振動
// マウスの視点操作はCameraがカーソルの位置から直接読んでいる
// ============================================================================

#include "input.h"
#include "Application.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // GetKeyboardStateが返す各バイトでは、上位ビットが押下状態を表す。
    constexpr BYTE VirtualKeyDownMask = 0x80;
}

namespace
{
    // スティックの値（-32768〜32767）を -1〜1 にする割る数（負の側・正の側）
    constexpr float StickNegativeRange = 32768.0f;
    constexpr float StickPositiveRange = 32767.0f;
	constexpr float VibrationFramesPerSecond = 60.0f;
	constexpr WORD MaximumMotorSpeed = (std::numeric_limits<WORD>::max)();

	// スティックの値を-1〜1にし、中心付近の遊び（デッドゾーン）を0、その外側を0〜1に広げ直している
	float NormalizeStickAxis(SHORT value, SHORT deadZone)
	{
		const float normalized = value < 0
			? static_cast<float>(value) / StickNegativeRange
			: static_cast<float>(value) / StickPositiveRange;
		const float magnitude = std::abs(normalized);
		const float deadZoneRate = static_cast<float>(deadZone) / StickPositiveRange;

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
	CopyMemory(m_Instance->keyState_old, m_Instance->keyState, sizeof(m_Instance->keyState));
	m_Instance->controllerState_old = m_Instance->controllerState;

	// キーの状態を読み直している
	if (!GetKeyboardState(m_Instance->keyState))
	{
		ZeroMemory(m_Instance->keyState, sizeof(m_Instance->keyState));
	}

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

	if (connectedIndex != m_Instance->controllerIndex && m_Instance->VibrationTimeSeconds > 0.0f)
	{
		m_Instance->StopVibration();
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

	// 復帰直後は、その時点で押されているキーやボタンを基準にする。
	if (m_Instance->m_ResyncOnNextUpdate)
	{
		CopyMemory(m_Instance->keyState_old, m_Instance->keyState, sizeof(m_Instance->keyState));
		m_Instance->controllerState_old = m_Instance->controllerState;
		m_Instance->m_ResyncOnNextUpdate = false;
	}

	// 振動を続ける残り時間を減らしている
	if (m_Instance->VibrationTimeSeconds > 0.0f) {
		m_Instance->VibrationTimeSeconds -= Application::GetDeltaTime();
		if (m_Instance->VibrationTimeSeconds <= 0.0f) { // 時間が過ぎたら振動を止めている
			m_Instance->StopVibration();
		}
	}
}

void Input::OnFocusLost()
{
	if (m_Instance)
	{
		if (m_Instance->VibrationTimeSeconds > 0.0f)
		{
			m_Instance->StopVibration();
		}
		m_Instance->m_ResyncOnNextUpdate = true;
	}
}

void Input::StopVibration()
{
	VibrationTimeSeconds = 0.0f;
	if (controllerIndex < XUSER_MAX_COUNT)
	{
		XINPUT_VIBRATION vibration{};
		XInputSetState(controllerIndex, &vibration);
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
	return m_Instance->keyState[key] & VirtualKeyDownMask;
}
bool Input::GetKeyTrigger(int key) // 押した瞬間だけtrue
{
	return (m_Instance->keyState[key] & VirtualKeyDownMask) && !(m_Instance->keyState_old[key] & VirtualKeyDownMask);
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

// ボタン入力
bool Input::GetButtonPress(WORD btn) // 押している間ずっとtrue
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0;
}
bool Input::GetButtonTrigger(WORD btn) // 押した瞬間だけtrue
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) == 0;
}

// 振動を始めている
void Input::SetVibration(int frame, float strength)
{
	if (!m_Instance || !m_Instance->controllerConnected)
	{
		return;
	}
	if (frame <= 0 || strength <= 0.0f || !std::isfinite(strength))
	{
		m_Instance->StopVibration();
		return;
	}

	const WORD motorSpeed = static_cast<WORD>((std::min)(strength, 1.0f) * MaximumMotorSpeed);
	XINPUT_VIBRATION vibration{ motorSpeed, motorSpeed };
	XInputSetState(m_Instance->controllerIndex, &vibration);

	// 振動を続ける秒数を入れている（frameを60fpsのフレーム数として秒に直している）
	m_Instance->VibrationTimeSeconds = static_cast<float>(frame) / VibrationFramesPerSecond;
}

