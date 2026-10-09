// ============================================================================
// ファイルの役割: キーボードとXInputコントローラーの入力の状態を集め、押した瞬間・離した瞬間を判定している。
// 主な技術: Win32のキー入力（GetKeyboardState）、XInput、前フレームとの比較による押した瞬間の判定、コントローラーの振動
// ============================================================================

#pragma once
#include <memory>
#include <d3d11.h>  // DirectX 11を使うためのヘッダー
#include <DirectXMath.h> // DirectXの数学関連のヘッダー（スティックの値をXMFLOAT2で返すため）

#include <Xinput.h> // XInputを使うためのヘッダー
#pragma comment (lib, "xinput.lib") // XInputのライブラリをリンクしている

// コントローラーのボタンのビット（XINPUT_GAMEPAD_*と同じ値を短い名前にしている）
#define XINPUT_A              0x1000
#define XINPUT_B              0x2000
#define XINPUT_X              0x4000
#define XINPUT_Y              0x8000
#define XINPUT_UP             0x0001
#define XINPUT_DOWN           0x0002
#define XINPUT_LEFT           0x0004
#define XINPUT_RIGHT          0x0008
#define XINPUT_START          0x0010
#define XINPUT_BACK           0x0020
#define XINPUT_LEFT_THUMB     0x0040 // 左スティックの押し込み
#define XINPUT_RIGHT_THUMB    0x0080 // 右スティックの押し込み
#define XINPUT_LEFT_SHOULDER  0x0100 // LB
#define XINPUT_RIGHT_SHOULDER 0x0200 // RB

// 数字キーと英字キーの仮想キーコード（Windowsのヘッダーには定義がないため、ここで定義している）
#define VK_0 0x30
#define VK_1 0x31
#define VK_2 0x32
#define VK_3 0x33
#define VK_4 0x34
#define VK_5 0x35
#define VK_6 0x36
#define VK_7 0x37
#define VK_8 0x38
#define VK_9 0x39
#define VK_A 0x41
#define VK_B 0x42
#define VK_C 0x43
#define VK_D 0x44
#define VK_E 0x45
#define VK_F 0x46
#define VK_G 0x47
#define VK_H 0x48
#define VK_I 0x49
#define VK_J 0x4A
#define VK_K 0x4B
#define VK_L 0x4C
#define VK_M 0x4D
#define VK_N 0x4E
#define VK_O 0x4F
#define VK_P 0x50
#define VK_Q 0x51
#define VK_R 0x52
#define VK_S 0x53
#define VK_T 0x54
#define VK_U 0x55
#define VK_V 0x56
#define VK_W 0x57
#define VK_X 0x58
#define VK_Y 0x59
#define VK_Z 0x5A

// 入力をまとめて扱うクラス。インスタンスは1つだけで、どこからでも静的な関数で読めるようにしている。
class Input {
private:
	// 自身のインスタンス
	static std::unique_ptr<Input> m_Instance;

	// 今のフレームと前のフレームのキーの状態（256個の仮想キー）
	BYTE keyState[256];
	BYTE keyState_old[256];

	// 今のフレームと前のフレームのコントローラーの状態、つながっているか、何番目のコントローラーか
	XINPUT_STATE controllerState;
	XINPUT_STATE controllerState_old;
	bool controllerConnected;
	DWORD controllerIndex;

	// 振動を止めるまでの残り秒数
	float VibrationTimeSeconds;

public:
	Input() = default;
	Input(const Input&) = delete;
	Input& operator=(const Input&) = delete;

	static void Create(); // インスタンスを作っている
	static void Update(); // 毎フレームの最初に、キーとコントローラーの状態を読み直している
	// コントローラーがつながっているか
	static bool IsControllerConnected();
	static void Release(); // 振動を止めて解放している

	// キー入力
	static bool GetKeyPress(int key);   // 押している間ずっとtrue
	static bool GetKeyTrigger(int key); // 押した瞬間だけtrue
	static bool GetKeyRelease(int key); // 離した瞬間だけtrue

	// アナログスティック（-1〜1。中心付近の遊びは0にしている）
	static DirectX::XMFLOAT2 GetLeftAnalogStick(void);
	static DirectX::XMFLOAT2 GetRightAnalogStick(void);

	// アナログトリガー（0〜1）
	static float GetLeftTrigger(void);
	static float GetRightTrigger(void);

	// コントローラーのボタン入力
	static bool GetButtonPress(WORD btn);   // 押している間ずっとtrue
	static bool GetButtonTrigger(WORD btn); // 押した瞬間だけtrue
	static bool GetButtonRelease(WORD btn); // 離した瞬間だけtrue

	// コントローラーを振動させている
	// frame：振動を続ける時間（60fpsで数えたフレーム数。実際は秒に直して使っている）
	// powor：振動の強さ（0〜1）
	static void SetVibration(int frame = 1, float powor = 1);
};

