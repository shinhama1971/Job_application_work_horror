// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理している。
// 主な技術: XAudio2、X3DAudio（立体音響）、ローパスフィルター、RIFF/WAVEの解析、Source Voice、RAII
// ============================================================================

#pragma once
#include <array>
#include <functional>
#include <memory>

#include <xaudio2.h>
#include <x3daudio.h>
#include <wrl/client.h>
#include <SimpleMath.h>

// 音の種類（m_paramの並びと同じ順番）
typedef enum
{
	SOUND_CUE_AMBIENCE_STAGE1 = 0, // 1面でループする低い環境音
	SOUND_CUE_AMBIENCE_STAGE2,     // 2面の脈打つような不穏な環境音
	SOUND_CUE_PICKUP,       // ヒューズ・電池・鍵を拾った音
	SOUND_CUE_DOOR,         // 扉の開け閉めのきしむ音（ロッカーの扉にも使っている）
	SOUND_CUE_POWER,        // 配電盤や信号盤が動き出す音
	SOUND_CUE_SCARE,        // 人影が現れたときや、捕まったときの衝撃音
	SOUND_CUE_FOOTSTEP,     // 研究所の中（1面・2面）の足音。硬い床を歩く1歩
	SOUND_CUE_WATER_STEP,   // 人影や見えない足跡が水を踏む音
	SOUND_CUE_FLASHLIGHT,   // 懐中電灯のスイッチのクリック音
	SOUND_CUE_PIPE_KNOCK,   // 配管を叩いたような金属音（tools/generate-pipe-knock.ps1で合成）
	SOUND_CUE_HEARTBEAT,    // 自分の心拍「ドクン」1回分（tools/generate-heartbeat-breath.ps1で合成）
	SOUND_CUE_BREATH,       // 自分の呼吸「吐いて吸う」1回分（同上）
	SOUND_CUE_RADIO_STATIC, // 携帯ラジオの雑音のループ（影が近いほど大きくなる。tools/generate-radio-static.ps1で合成）
	SOUND_CUE_RAIN,         // 外の森の雨音のループ（木の葉と地面を打つ雨と、ときどき落ちる大きな滴。tools/generate-rain.ps1で合成）
	SOUND_CUE_DOOR_LOCKED,  // 鍵のかかった扉のドアノブを、がちゃがちゃ回す音
	SOUND_CUE_WATER_DROP,   // 天井から落ちる1滴の水音
	SOUND_CUE_DRAIN,        // 1面の西棟で、水が排水口へ引いていく音（約12秒。バルブを1つ回すごとに1回）
	SOUND_CUE_FOREST_STEP,  // 外の森の、雨で濡れた道を歩く1歩
	SOUND_CUE_FOREST_RUN,   // 外の森の、雨で濡れた道を走る1歩
	SOUND_CUE_WATER_WALK,   // プレイヤーが水の中を歩く1歩
	SOUND_CUE_WATER_RUN,    // プレイヤーが水の中を走る1歩

	// 音の種類の数
	SOUND_LABEL_MAX,
} SOUND_LABEL;

// 立体音響の聞き手（カメラ）。位置と向きはワールド座標（左手系）。
struct SoundListener
{
	DirectX::SimpleMath::Vector3 Position;
	DirectX::SimpleMath::Vector3 Forward{ 0.0f, 0.0f, 1.0f };
};

// ゲームの音をまとめて管理するクラス。Gameが1つ持っている。
class Sound {
private:
	// 音の素材ごとの設定
	typedef struct
	{
		LPCSTR filename;	// 音声ファイルのパス
		bool bLoop;			// trueでループ再生（環境音）、falseで1回だけ再生（効果音）。
		float volume;      // 0.0〜1.0。素材ごとの音量の差をここで吸収している。
	} PARAM;

	// 素材ごとの設定（SOUND_LABELと同じ順番）
	PARAM m_param[SOUND_LABEL_MAX] =
	{
		{"assets/Audio/horror_ambient.wav", true, 0.28f},
		{"assets/Audio/stage2_ambient.wav", true, 0.30f},
		{"assets/Audio/pickup.wav", false, 0.56f},
		{"assets/Audio/door_open.wav", false, 0.48f},
		{"assets/Audio/power_restore.wav", false, 0.62f},
		{"assets/Audio/scare_impact.wav", false, 0.72f},
		{"assets/Audio/footstep_asphalt.wav", false, 0.40f},
		{"assets/Audio/water_step.wav", false, 0.52f},
		{"assets/Audio/flashlight_click.wav", false, 0.54f},
		{"assets/Audio/pipe_knock.wav", false, 0.50f},
		{"assets/Audio/heartbeat.wav", false, 0.70f},
		{"assets/Audio/breath.wav", false, 0.32f},
		{"assets/Audio/radio_static.wav", true, 0.34f},
		{"assets/Audio/rain.wav", true, 0.42f},
		{"assets/Audio/door_locked.wav", false, 0.50f},
		{"assets/Audio/water_drop.wav", false, 0.80f},
		{"assets/Audio/drain.wav", false, 0.60f},
		{"assets/Audio/forest_step_walk.wav", false, 0.42f},
		{"assets/Audio/forest_step_run.wav", false, 0.45f},
		{"assets/Audio/water_step_walk.wav", false, 0.42f},
		{"assets/Audio/water_step_run.wav", false, 0.53f},
	};

	// XAudio2本体はCOMなのでComPtrで管理している。ボイスはCOMではないので、DestroyVoiceで解放している。
	Microsoft::WRL::ComPtr<IXAudio2> m_pXAudio2;
	// 最終的に音を出すマスタリングボイス、種類ごとのソースボイス
	IXAudio2MasteringVoice* m_pMasteringVoice = NULL;
	IXAudio2SourceVoice* m_pSourceVoice[SOUND_LABEL_MAX]{};
	WAVEFORMATEXTENSIBLE m_wfx[SOUND_LABEL_MAX]{}; // WAVの形式
	// 再生用のバッファ、読み込んだ音声データ、COMを初期化したか
	XAUDIO2_BUFFER m_buffer[SOUND_LABEL_MAX]{};
	std::unique_ptr<BYTE[]> m_DataBuffer[SOUND_LABEL_MAX];
	bool m_IsComInitialized = false;
	// 音の種類が範囲内か
	static bool IsValidLabel(SOUND_LABEL label)
	{
		return label >= SOUND_CUE_AMBIENCE_STAGE1 && label < SOUND_LABEL_MAX;
	}

	// WAVファイルの中から、指定したチャンクの位置と大きさを探している／その中身を読み込んでいる
	HRESULT FindChunk(HANDLE, DWORD, DWORD&, DWORD&);
	HRESULT ReadChunkData(HANDLE, void*, DWORD, DWORD);

public:
	// 聞き手から音源までの間にある、さえぎる物の量（0=さえぎる物なし、1=完全に壁の向こう）を返す関数の型。
	// 壁や扉はゲーム側の型なので、Soundは判定のしかたを知らずに結果だけを受け取っている。
	using OcclusionQuery = std::function<float(
		const DirectX::SimpleMath::Vector3& listener,
		const DirectX::SimpleMath::Vector3& emitter)>;

	// 同時に鳴らせる位置付きの音の数
	static constexpr size_t MaxSpatialVoices = 16;

private:
	// 位置を持って鳴っている1音分。同じ効果音が重なって鳴っても、それぞれの位置で聞こえる。
	struct SpatialVoice
	{
		IXAudio2SourceVoice* Voice = nullptr;
		SOUND_LABEL Label = SOUND_CUE_AMBIENCE_STAGE1;
		DirectX::SimpleMath::Vector3 Position;
		float Occlusion = 0.0f;		// 急に切り替わらないよう、毎フレーム目標の値へ近づけている
		float MinimumOcclusion = 0.0f;	// 壁の判定に関係なく、最低でもこれだけこもらせている（天井裏の音など）
	};

	// X3DAudioの準備、使えるか、出力のチャンネル数
	X3DAUDIO_HANDLE m_X3DAudio{};
	bool m_X3DAudioReady = false;
	UINT32 m_OutputChannels = 2;
	// 位置付きの音の枠、次に使う枠、聞き手、さえぎる物の判定の関数
	std::array<SpatialVoice, MaxSpatialVoices> m_SpatialVoices{};
	size_t m_NextSpatialVoice = 0;
	SoundListener m_Listener;
	OcclusionQuery m_OcclusionQuery;

	// さえぎる物の量を問い合わせている／立体音響の計算結果をボイスに反映している／ボイスを解放している
	float QueryOcclusion(const DirectX::SimpleMath::Vector3& emitter) const;
	void ApplySpatialMix(SpatialVoice& spatial);
	static void ReleaseSpatialVoice(SpatialVoice& spatial);

public:
	Sound() = default;
	~Sound();

	Sound(const Sound&) = delete;
	Sound& operator=(const Sound&) = delete;

	// ゲームループを始める前に呼ぶ、音の初期化（全部の素材を読み込んでいる）
	HRESULT Init(void);

	// ゲームループを終えた後に呼ぶ、音の解放
	void Uninit(void);

	// 指定した音を位置なしで再生している。volume は素材ごとの音量に掛ける倍率。
	void Play(SOUND_LABEL label, float pitch = 1.0f, float volume = 1.0f);

	// 指定した音を止めている
	void Stop(SOUND_LABEL label);

	// 鳴っている音の大きさを変えている（ループする音を、鳴らしたまま強弱させるのに使う）。volume は素材ごとの音量に掛ける倍率
	void SetVolume(SOUND_LABEL label, float volume);

	// 指定した音の再生を、止めた所から再開している
	void Resume(SOUND_LABEL label);

	// 全部の音に共通で掛かる音量を設定している。0.0で消音、1.0で素材の設定どおり。
	void SetMasterVolume(float volume);

	// ワールド上の位置から効果音を鳴らしている。聞き手との位置関係で、左右・距離・さえぎる物の効果が変わる。
	// volume は素材ごとの音量に掛ける倍率。
	// minimumOcclusion（0〜1）を指定すると、壁がなくてもその分こもって聞こえる（天井裏・床下の音など）。
	void PlayAt(
		SOUND_LABEL label,
		const DirectX::SimpleMath::Vector3& position,
		float pitch = 1.0f,
		float volume = 1.0f,
		float minimumOcclusion = 0.0f);

	// 毎フレーム、カメラの位置と向きを渡して、鳴っている音の聞こえ方を更新している。
	void UpdateListener(const SoundListener& listener, float deltaTime);

	// さえぎる物の判定の関数を登録している（Gameが壁と扉で判定する関数を渡している）
	void SetOcclusionQuery(OcclusionQuery query) { m_OcclusionQuery = std::move(query); }

	// シーンを切り替えるときに、前の場所で鳴っていた位置付きの音を止めている。
	void StopAllSpatial();

	// 今鳴っている位置付きの音の数を返している（デバッグ画面用）
	size_t GetActiveSpatialVoiceCount() const;

};
