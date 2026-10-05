// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理します。
// 主な技術: XAudio2、X3DAudio（立体音響）、ローパスフィルター、RIFF/WAVE解析、Source Voice、RAII
// ============================================================================

#pragma once
#include <array>
#include <functional>
#include <memory>

#include <xaudio2.h>
#include <x3daudio.h>
#include <wrl/client.h>
#include <SimpleMath.h>

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
	SOUND_CUE_PIPE_KNOCK,   // 配管を叩いたような金属音（tools/generate-pipe-knock.ps1で合成）
	SOUND_CUE_HEARTBEAT,    // 自分の心拍「ドクン」1回分（tools/generate-heartbeat-breath.ps1で合成）
	SOUND_CUE_BREATH,       // 自分の呼吸「吐いて吸う」1回分（同上）

	SOUND_LABEL_MAX,
} SOUND_LABEL;

// 立体音響の聞き手（カメラ）。位置と向きはワールド座標（左手系）です。
struct SoundListener
{
	DirectX::SimpleMath::Vector3 Position;
	DirectX::SimpleMath::Vector3 Forward{ 0.0f, 0.0f, 1.0f };
};

class Sound {
private:
	// パラメータ構造体
	typedef struct
	{
		LPCSTR filename;	// 音声ファイルまでのパスを設定
		bool bLoop;			// trueでループ再生（環境音）、falseで1回だけ再生（効果音）。
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
		{"assets/Audio/pipe_knock.wav", false, 0.50f},
		{"assets/Audio/heartbeat.wav", false, 0.70f},
		{"assets/Audio/breath.wav", false, 0.32f},
	};

	// XAudio2本体はCOMのためComPtrで管理します。ボイスはCOMではなくDestroyVoiceで解放します。
	Microsoft::WRL::ComPtr<IXAudio2> m_pXAudio2;
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
	// 聞き手から音源までの間にある遮蔽物の量（0=遮蔽なし、1=完全に壁の向こう）を返す関数です。
	// 壁や扉はゲーム側の型なので、Soundは判定方法を知らずに結果だけを受け取ります。
	using OcclusionQuery = std::function<float(
		const DirectX::SimpleMath::Vector3& listener,
		const DirectX::SimpleMath::Vector3& emitter)>;

	static constexpr size_t MaxSpatialVoices = 16;

private:
	// 位置を持って鳴っている1音分。同じ効果音が重なって鳴っても、それぞれの位置で聞こえます。
	struct SpatialVoice
	{
		IXAudio2SourceVoice* Voice = nullptr;
		SOUND_LABEL Label = SOUND_CUE_AMBIENCE_STAGE1;
		DirectX::SimpleMath::Vector3 Position;
		float Occlusion = 0.0f;		// 急に切り替わらないよう、毎フレーム目標値へ近づけます
		float MinimumOcclusion = 0.0f;	// 壁の判定に関係なく、最低でもこれだけこもらせます（天井裏の音など）
	};

	X3DAUDIO_HANDLE m_X3DAudio{};
	bool m_X3DAudioReady = false;
	UINT32 m_OutputChannels = 2;
	std::array<SpatialVoice, MaxSpatialVoices> m_SpatialVoices{};
	size_t m_NextSpatialVoice = 0;
	SoundListener m_Listener;
	OcclusionQuery m_OcclusionQuery;

	float QueryOcclusion(const DirectX::SimpleMath::Vector3& emitter) const;
	void ApplySpatialMix(SpatialVoice& spatial);
	static void ReleaseSpatialVoice(SpatialVoice& spatial);

public:
	Sound() = default;
	~Sound();

	Sound(const Sound&) = delete;
	Sound& operator=(const Sound&) = delete;

	// ゲームループ開始前に呼び出すサウンドの初期化処理
	HRESULT Init(void);

	// ゲームループ終了後に呼び出すサウンドの解放処理
	void Uninit(void);

	// 引数で指定したサウンドを再生する。volume は素材ごとの音量に掛ける倍率です。
	void Play(SOUND_LABEL label, float pitch = 1.0f, float volume = 1.0f);

	// 引数で指定したサウンドを停止する
	void Stop(SOUND_LABEL label);

	// 引数で指定したサウンドの再生を再開する
	void Resume(SOUND_LABEL label);

	// 全サウンドへ共通で掛かる音量。0.0で消音、1.0で素材設定通り。
	void SetMasterVolume(float volume);

	// ワールド上の位置から効果音を鳴らします。聞き手との位置関係で左右・距離・遮蔽が変わります。
	// volume は素材ごとの音量に掛ける倍率です。
	// minimumOcclusion（0〜1）を指定すると、壁がなくてもその分こもって聞こえます（天井裏・床下の音など）。
	void PlayAt(
		SOUND_LABEL label,
		const DirectX::SimpleMath::Vector3& position,
		float pitch = 1.0f,
		float volume = 1.0f,
		float minimumOcclusion = 0.0f);

	// 毎フレーム、カメラの位置と向きを渡して、鳴っている音の聞こえ方を更新します。
	void UpdateListener(const SoundListener& listener, float deltaTime);

	void SetOcclusionQuery(OcclusionQuery query) { m_OcclusionQuery = std::move(query); }

	// シーン切り替え時に、前の場所で鳴っていた位置付きの音を止めます。
	void StopAllSpatial();

	size_t GetActiveSpatialVoiceCount() const;

};
