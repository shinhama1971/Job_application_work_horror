// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理します。
// ============================================================================

#pragma once
#include <memory>

#include <xaudio2.h>

// サウンドファイル
typedef enum
{
	SOUND_CUE_AMBIENCE_STAGE1 = 0, // 1面でループする低い環境音
	SOUND_CUE_AMBIENCE_STAGE2,     // 2面の脈動を含む不穏な環境音
	SOUND_CUE_PICKUP,       // ヒューズ・電池を取得した音
	SOUND_CUE_DOOR,         // ドアの開閉・施錠音
	SOUND_CUE_POWER,        // 配電盤や信号装置の起動音
	SOUND_CUE_SCARE,        // 人影出現や捕獲時の衝撃音
	SOUND_CUE_FOOTSTEP,     // 歩行・走行に同期する足音
	SOUND_CUE_WATER_STEP,   // 水たまりを踏んだときの水音
	SOUND_CUE_FLASHLIGHT,   // 懐中電灯スイッチのクリック音

	SOUND_LABEL_MAX,
} SOUND_LABEL;

class Sound {
private:
	// パラメータ構造体
	typedef struct
	{
		LPCSTR filename;	// 音声ファイルまでのパスを設定
		bool bLoop;			// trueでループ。通常BGMはture、SEはfalse。
		float volume;      // 0.0～1.0。素材ごとの音量差をここで吸収する。
	} PARAM;

	PARAM m_param[SOUND_LABEL_MAX] =
	{
		{"assets/Audio/horror_ambient.wav", true, 0.28f},
		{"assets/Audio/stage2_ambient.wav", true, 0.30f},
		{"assets/Audio/pickup.wav", false, 0.56f},
		{"assets/Audio/door_creak.wav", false, 0.48f},
		{"assets/Audio/power_restore.wav", false, 0.62f},
		{"assets/Audio/scare_impact.wav", false, 0.72f},
		{"assets/Audio/footstep.wav", false, 0.40f},
		{"assets/Audio/water_step.wav", false, 0.52f},
		{"assets/Audio/flashlight_click.wav", false, 0.54f},
	};

	IXAudio2* m_pXAudio2 = NULL;
	IXAudio2MasteringVoice* m_pMasteringVoice = NULL;
	IXAudio2SourceVoice* m_pSourceVoice[SOUND_LABEL_MAX]{};
	WAVEFORMATEXTENSIBLE m_wfx[SOUND_LABEL_MAX]{}; // WAVフォーマット
	XAUDIO2_BUFFER m_buffer[SOUND_LABEL_MAX]{};
	std::unique_ptr<BYTE[]> m_DataBuffer[SOUND_LABEL_MAX];
	bool m_IsComInitialized = false;
	static bool IsValidLabel(SOUND_LABEL label)
	{
		return label >= SOUND_CUE_AMBIENCE_STAGE1 && label < SOUND_LABEL_MAX;
	}

	HRESULT FindChunk(HANDLE, DWORD, DWORD&, DWORD&);
	HRESULT ReadChunkData(HANDLE, void*, DWORD, DWORD);

public:
	Sound() = default;
	~Sound();

	Sound(const Sound&) = delete;
	Sound& operator=(const Sound&) = delete;

	// ゲームループ開始前に呼び出すサウンドの初期化処理
	HRESULT Init(void);

	// ゲームループ終了後に呼び出すサウンドの解放処理
	void Uninit(void);

	// 引数で指定したサウンドを再生する
	void Play(SOUND_LABEL label, float pitch = 1.0f);

	// 引数で指定したサウンドを停止する
	void Stop(SOUND_LABEL label);

	// 引数で指定したサウンドの再生を再開する
	void Resume(SOUND_LABEL label);

	// 全サウンドへ共通で掛かる音量。0.0で消音、1.0で素材設定通り。
	void SetMasterVolume(float volume);

};
