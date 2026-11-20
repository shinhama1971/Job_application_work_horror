// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理している。
// 主な技術: XAudio2、X3DAudio（立体音響）、ローパスフィルター、RIFF/WAVEの解析、Source Voice、RAII
// ============================================================================

#include "sound.h"
#include "MathUtility.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <mmsystem.h>

// X3DAudioInitialize / X3DAudioCalculate は XAudio2 2.9 のライブラリに含まれている。
#pragma comment(lib, "xaudio2.lib")

using DirectX::SimpleMath::Vector3;

namespace
{
	constexpr DWORD RiffChunkId = mmioFOURCC('R', 'I', 'F', 'F');
	constexpr DWORD DataChunkId = mmioFOURCC('d', 'a', 't', 'a');
	constexpr DWORD FormatChunkId = mmioFOURCC('f', 'm', 't', ' ');
	constexpr DWORD WaveFormatId = mmioFOURCC('W', 'A', 'V', 'E');
	constexpr UINT32 MaxSpatialChannels = 8;
	constexpr float MinCuePitch = 0.70f;
	constexpr float MaxCuePitch = 1.35f;
	constexpr float MinSpatialPitch = 0.50f;
	constexpr float MaxSpatialPitch = 1.50f;

	// 1ワールド単位はおよそ4cm（目の高さが40）。この距離までは小さくならず、その先は距離に反比例して小さくなる。
	constexpr float SpatialCurveDistance = 30.0f;
	// これより近い音は方向をはっきりさせず、耳元で広がるように聞かせている。
	constexpr float SpatialInnerRadius = 6.0f;
	// 内側の範囲の角度（この角度の中は向きに関係なく同じ聞こえ方）
	constexpr float SpatialInnerRadiusAngle = X3DAUDIO_PI / 4.0f;
	// 聞き手の向きを決められないとみなす長さの2乗
	constexpr float MinimumDirectionLengthSquared = 0.0001f;
	// 壁の向こうの音の大きさと、こもり具合（ローパスフィルターの周波数）。
	constexpr float OccludedVolume = 0.38f;
	constexpr float OccludedCutoffHz = 650.0f;
	// 遠くの音ほど、高い音が空気に吸われる様子を近似している。
	constexpr float OpenAirCutoffHz = 16000.0f;
	constexpr float DistantCutoffHz = 4500.0f;
	constexpr float DistantCutoffRange = 600.0f;
	// 壁の出入りで音がぷつっと変わらないよう、さえぎる物の量をこの速さで追いかけさせている。
	constexpr float OcclusionResponsePerSecond = 7.0f;

	// SimpleMathのベクトルを、X3DAudioのベクトルに変換している
	X3DAUDIO_VECTOR ToX3DAudio(const Vector3& value)
	{
		X3DAUDIO_VECTOR result{};
		result.x = value.x;
		result.y = value.y;
		result.z = value.z;
		return result;
	}

	void ResetCue(IXAudio2SourceVoice*& voice)
	{
		if (voice == nullptr)
		{
			return;
		}
		if (SUCCEEDED(voice->Stop()) && SUCCEEDED(voice->FlushSourceBuffers()))
		{
			XAUDIO2_VOICE_STATE state{};
			voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			if (state.BuffersQueued == 0)
			{
				return;
			}
		}

		// 停止がまだ反映されていないボイスは壊し、古いバッファを次の再生へ持ち越さない。
		voice->DestroyVoice();
		voice = nullptr;
	}
}

// 壊すときに解放している
Sound::~Sound()
{
	Uninit();
}

//=============================================================================
// 初期化：COMとXAudio2を準備し、全部の素材のWAVを読み込んでいる
//=============================================================================
HRESULT Sound::Init()
{
	if (m_pXAudio2)
	{
		return S_OK;
	}

	HRESULT hr;

	HANDLE hFile;
	DWORD  dwChunkSize;
	DWORD  dwChunkPosition;
	DWORD  filetype;

	// COMを初期化している
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		return hr;
	}
	m_IsComInitialized = true;

	// XAudio2の本体を作っている
	// 2番目の引数の動作の設定はWindowsでは使わないため、0を渡している。
	hr = XAudio2Create(m_pXAudio2.ReleaseAndGetAddressOf(), 0);
	if (FAILED(hr)) {
		Uninit();
		return hr;
	}

	// 最終的に音を出すマスタリングボイスを作っている
	// チャンネル数・サンプリング周波数は既定の値にし、PCの出力の設定（ステレオ・5.1chなど）に合わせている。
	hr = m_pXAudio2->CreateMasteringVoice(&m_pMasteringVoice);
	if (FAILED(hr)) {
		Uninit();
		return hr;
	}

	// 立体音響（X3DAudio）を準備している
	// 出力先（ステレオ・5.1chなど）のスピーカーの配置に合わせて、音の振り分けを計算させている。
	// 立体音響だけが使えない環境でも、普通の再生は続けられるようにしている。
	{
		DWORD channelMask = 0;
		XAUDIO2_VOICE_DETAILS masterDetails{};
		m_pMasteringVoice->GetVoiceDetails(&masterDetails);
		m_OutputChannels = masterDetails.InputChannels;
		// マスクのスピーカー数と実際の出力数が一致するときだけ、振り分け行列を計算する。
		m_X3DAudioReady = SUCCEEDED(m_pMasteringVoice->GetChannelMask(&channelMask)) &&
			m_OutputChannels > 0 && m_OutputChannels <= MaxSpatialChannels &&
			static_cast<UINT32>(std::popcount(channelMask)) == m_OutputChannels &&
			SUCCEEDED(X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, m_X3DAudio));
	}

	// 全部の素材を、WAVファイルから読み込んでいる
	for (int i = 0; i < SOUND_LABEL_MAX; i++)
	{
		memset(&m_wfx[i], 0, sizeof(WAVEFORMATEXTENSIBLE));
		memset(&m_buffer[i], 0, sizeof(XAUDIO2_BUFFER));

		hFile = CreateFileA(m_param[i].filename, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			Uninit();
			return hr;
		}

		// ファイルの形式がWAVEであることを確かめている。
		hr = FindChunk(hFile, RiffChunkId, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE)
		{
			CloseHandle(hFile);
			Uninit();
			return FAILED(hr) ? hr : E_FAIL;
		}
		hr = ReadChunkData(hFile, &filetype, sizeof(DWORD), dwChunkPosition);
		if (FAILED(hr))
		{
			CloseHandle(hFile);
			Uninit();
			return hr;
		}
		if (filetype != WaveFormatId) {
			CloseHandle(hFile);
			Uninit();
			return E_FAIL;
		}

		// 音声の形式（fmtチャンク）を読み込んでいる
		hr = FindChunk(hFile, FormatChunkId, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE || dwChunkSize < offsetof(WAVEFORMATEX, cbSize) ||
			dwChunkSize == offsetof(WAVEFORMATEX, cbSize) + 1 || dwChunkSize > sizeof(m_wfx[i]) ||
			FAILED(ReadChunkData(hFile, &m_wfx[i], dwChunkSize, dwChunkPosition)))
		{
			CloseHandle(hFile);
			Uninit();
			return FAILED(hr) ? hr : E_FAIL;
		}

		if (dwChunkSize >= sizeof(WAVEFORMATEX) &&
			m_wfx[i].Format.cbSize > dwChunkSize - sizeof(WAVEFORMATEX))
		{
			CloseHandle(hFile);
			Uninit();
			return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
		}

		// 音声データ（dataチャンク）の中身を、再生用のバッファへ読み込んでいる。
		hr = FindChunk(hFile, DataChunkId, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE || dwChunkSize == 0 || dwChunkSize > XAUDIO2_MAX_BUFFER_BYTES)
		{
			CloseHandle(hFile);
			Uninit();
			return FAILED(hr) ? hr : E_FAIL;
		}
		m_DataBuffer[i] = std::make_unique<BYTE[]>(dwChunkSize);
		hr = ReadChunkData(
			hFile, m_DataBuffer[i].get(), dwChunkSize, dwChunkPosition);
		if (FAILED(hr))
		{
			CloseHandle(hFile);
			Uninit();
			return hr;
		}

		CloseHandle(hFile);

		// ソースボイスへ渡す再生用のバッファ。ループ指定の素材（環境音）は無限に繰り返している。
		m_buffer[i].AudioBytes = dwChunkSize;
		m_buffer[i].pAudioData = m_DataBuffer[i].get();
		m_buffer[i].Flags = XAUDIO2_END_OF_STREAM;
		if (m_param[i].bLoop)
			m_buffer[i].LoopCount = XAUDIO2_LOOP_INFINITE;
		else
			m_buffer[i].LoopCount = 0;

		// 素材ごとのソースボイスを作っている
		hr = m_pXAudio2->CreateSourceVoice(
			&m_pSourceVoice[i],
			&(m_wfx[i].Format)
		);
		if (FAILED(hr)) {
			Uninit();
			return hr;
		}
	}

	return hr;
}

//=============================================================================
// 解放処理
//=============================================================================
void Sound::Uninit(void)
{
	// 位置付きの音は音声データ（m_DataBuffer）を参照しているため、データより先に止めている。
	StopAllSpatial();
	m_X3DAudioReady = false;

	for (int i = 0; i < SOUND_LABEL_MAX; i++)
	{
		if (m_pSourceVoice[i])
		{
			m_pSourceVoice[i]->Stop(0);
			m_pSourceVoice[i]->FlushSourceBuffers();
			m_pSourceVoice[i]->DestroyVoice();			// ソースボイスを、音の流れから外して壊している
			m_pSourceVoice[i] = nullptr;
		}

		m_DataBuffer[i].reset();
	}

	if (m_pMasteringVoice)
	{
		m_pMasteringVoice->DestroyVoice();
		m_pMasteringVoice = nullptr;
	}

	if (m_pXAudio2)
	{
		m_pXAudio2.Reset();
	}

	// COMを終了している
	if (m_IsComInitialized)
	{
		CoUninitialize();
		m_IsComInitialized = false;
	}
}

//=============================================================================
// 再生（位置なし）。同じ音を鳴らすと、前の再生を止めて最初から鳴らし直している
//=============================================================================
void Sound::Play(SOUND_LABEL label, float pitch, float volume)
{
	if (!IsValidLabel(label) || m_pXAudio2 == nullptr)
	{
		return;
	}

	IXAudio2SourceVoice*& voice = m_pSourceVoice[label];
	// 初期化時に作ったボイスを使い、前の再生を止めてバッファを空にする。
	ResetCue(voice);
	if (voice == nullptr && FAILED(m_pXAudio2->CreateSourceVoice(&voice, &m_wfx[label].Format)))
	{
		return;
	}
	if (voice == nullptr || FAILED(voice->SubmitSourceBuffer(&m_buffer[label])))
	{
		return;
	}

	// WAVごとの音の大きさの差を吸収し、環境音が効果音を覆い隠さないようにしている。
	voice->SetVolume(m_param[label].volume * (std::max)(volume, 0.0f));
	voice->SetFrequencyRatio((std::clamp)(pitch, MinCuePitch, MaxCuePitch));
	if (FAILED(voice->Start()))
	{
		ResetCue(voice);
	}
}

//=============================================================================
// 停止（現在の再生と再生待ちのバッファを消している）
//=============================================================================
void Sound::Stop(SOUND_LABEL label)
{
	if (IsValidLabel(label))
	{
		ResetCue(m_pSourceVoice[label]);
	}
}

// 全体の音量を0〜1に制限して設定している
void Sound::SetMasterVolume(float volume)
{
	if (m_pMasteringVoice == nullptr)
	{
		return;
	}

	const float safeVolume = (std::clamp)(volume, 0.0f, 1.0f);
	m_pMasteringVoice->SetVolume(safeVolume);
}

//=============================================================================
// 立体音響（X3DAudio）
//=============================================================================
void Sound::PlayAt(
	SOUND_LABEL label, const Vector3& position, float pitch, float volume, float minimumOcclusion)
{
	if (!IsValidLabel(label) || m_pXAudio2 == nullptr)
	{
		return;
	}
	// 鳴り終わった枠を探している。全部使っていれば、一番古く鳴らした音を止めて使っている。
	SpatialVoice* slot = nullptr;
	for (SpatialVoice& spatial : m_SpatialVoices)
	{
		if (spatial.Voice == nullptr)
		{
			slot = &spatial;
			break;
		}
		XAUDIO2_VOICE_STATE state{};
		spatial.Voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (state.BuffersQueued == 0)
		{
			ReleaseSpatialVoice(spatial);
			slot = &spatial;
			break;
		}
	}
	if (slot == nullptr)
	{
		slot = &*std::ranges::min_element(m_SpatialVoices, {}, &SpatialVoice::StartOrder);
		ReleaseSpatialVoice(*slot);
	}

	// 立体音響が使えない場合も同じ枠で管理し、通常のスピーカー振り分けで鳴らす。
	const int index = static_cast<int>(label);
	HRESULT hr = m_pXAudio2->CreateSourceVoice(
		&slot->Voice, &m_wfx[index].Format, m_X3DAudioReady ? XAUDIO2_VOICE_USEFILTER : 0);
	if (FAILED(hr) || slot->Voice == nullptr)
	{
		slot->Voice = nullptr;
		return;
	}

	// ループ指定の素材でも、位置付きの音は1回だけ鳴らしている。
	XAUDIO2_BUFFER buffer = m_buffer[index];
	buffer.LoopBegin = 0;
	buffer.LoopLength = 0;
	buffer.LoopCount = 0;
	if (FAILED(slot->Voice->SubmitSourceBuffer(&buffer)))
	{
		ReleaseSpatialVoice(*slot);
		return;
	}

	slot->Label = label;
	slot->Position = position;
	slot->Voice->SetVolume(m_param[index].volume * (std::max)(volume, 0.0f));
	slot->Voice->SetFrequencyRatio((std::clamp)(pitch, MinSpatialPitch, MaxSpatialPitch));
	if (m_X3DAudioReady)
	{
		// 鳴り始めから壁の向こうの音として聞こえるよう、最初のさえぎる物の量はその場で求める。
		slot->MinimumOcclusion = (std::clamp)(minimumOcclusion, 0.0f, 1.0f);
		slot->Occlusion = (std::max)(QueryOcclusion(position), slot->MinimumOcclusion);
		ApplySpatialMix(*slot);
	}
	if (FAILED(slot->Voice->Start()))
	{
		ReleaseSpatialVoice(*slot);
		return;
	}
	slot->StartOrder = ++m_SpatialPlayOrder;
}

// 聞き手を更新し、鳴り終わった音を片付け、鳴っている音のさえぎる物の量と聞こえ方を更新している
void Sound::UpdateListener(const SoundListener& listener, float deltaTime)
{
	m_Listener = listener;
	const float response = 1.0f - std::exp(-OcclusionResponsePerSecond * (std::max)(deltaTime, 0.0f));
	for (SpatialVoice& spatial : m_SpatialVoices)
	{
		if (spatial.Voice == nullptr)
		{
			continue;
		}
		XAUDIO2_VOICE_STATE state{};
		spatial.Voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (state.BuffersQueued == 0)
		{
			ReleaseSpatialVoice(spatial);
			continue;
		}
		if (!m_X3DAudioReady)
		{
			continue;
		}

		// 振り向いたり歩いたりして位置関係が変わった分を、鳴っている途中の音にも反映している。
		const float targetOcclusion =
			(std::max)(QueryOcclusion(spatial.Position), spatial.MinimumOcclusion);
		spatial.Occlusion += (targetOcclusion - spatial.Occlusion) * response;
		ApplySpatialMix(spatial);
	}
}

// さえぎる物の量を、登録された関数に問い合わせている（未登録なら0）
float Sound::QueryOcclusion(const Vector3& emitter) const
{
	if (!m_OcclusionQuery)
	{
		return 0.0f;
	}
	return (std::clamp)(m_OcclusionQuery(m_Listener.Position, emitter), 0.0f, 1.0f);
}

// 聞き手と音源の位置から、左右の振り分け・距離による減衰・こもり具合を計算してボイスに設定している
void Sound::ApplySpatialMix(SpatialVoice& spatial)
{
	const WAVEFORMATEX& format = m_wfx[static_cast<int>(spatial.Label)].Format;
	const UINT32 sourceChannels = format.nChannels;
	if (sourceChannels == 0 || sourceChannels > MaxSpatialChannels)
	{
		return;
	}
	const UINT32 outputChannels = m_OutputChannels;

	// 聞き手の上方向は、前方向に直交するように作り直している（X3DAudioの決まり）。
	Vector3 forward = m_Listener.Forward;
	if (forward.LengthSquared() < MinimumDirectionLengthSquared)
	{
		forward = Vector3(0.0f, 0.0f, 1.0f);
	}
	forward.Normalize();
	Vector3 up = Vector3(0.0f, 1.0f, 0.0f) - forward * forward.y;
	if (up.LengthSquared() < MinimumDirectionLengthSquared)
	{
		up = Vector3(0.0f, 0.0f, 1.0f);
	}
	up.Normalize();

	X3DAUDIO_LISTENER listener{};
	listener.OrientFront = ToX3DAudio(forward);
	listener.OrientTop = ToX3DAudio(up);
	listener.Position = ToX3DAudio(m_Listener.Position);

	// ステレオの素材も、すべてのチャンネルを同じ1点から鳴らしている。
	float channelAzimuths[MaxSpatialChannels] = {};
	X3DAUDIO_EMITTER emitter{};
	emitter.Position = ToX3DAudio(spatial.Position);
	emitter.OrientFront = ToX3DAudio(Vector3(0.0f, 0.0f, 1.0f));
	emitter.OrientTop = ToX3DAudio(Vector3(0.0f, 1.0f, 0.0f));
	emitter.ChannelCount = sourceChannels;
	emitter.ChannelRadius = 0.0f;
	emitter.pChannelAzimuths = channelAzimuths;
	emitter.InnerRadius = SpatialInnerRadius;
	emitter.InnerRadiusAngle = SpatialInnerRadiusAngle;
	emitter.CurveDistanceScaler = SpatialCurveDistance;
	emitter.DopplerScaler = 0.0f;

	float matrix[MaxSpatialChannels * MaxSpatialChannels] = {};
	X3DAUDIO_DSP_SETTINGS dsp{};
	dsp.SrcChannelCount = sourceChannels;
	dsp.DstChannelCount = outputChannels;
	dsp.pMatrixCoefficients = matrix;
	// X3DAudioで、出力の各チャンネルへの振り分けの行列を計算している
	X3DAudioCalculate(m_X3DAudio, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);

	// 壁の向こうの音は小さく、高い音を削ってこもらせている。遠い音も少しだけ高い音を落としている。
	const float occlusion = spatial.Occlusion;
	const float occlusionGain = 1.0f + (OccludedVolume - 1.0f) * occlusion;
	for (UINT32 i = 0; i < sourceChannels * outputChannels; ++i)
	{
		matrix[i] *= occlusionGain;
	}
	spatial.Voice->SetOutputMatrix(m_pMasteringVoice, sourceChannels, outputChannels, matrix);

	const float distanceAmount = (std::min)(dsp.EmitterToListenerDistance / DistantCutoffRange, 1.0f);
	const float openCutoff = OpenAirCutoffHz + (DistantCutoffHz - OpenAirCutoffHz) * distanceAmount;
	// 周波数は人の聞こえ方に合わせ、対数で補間している。
	const float cutoffHz = std::exp(
		std::log(openCutoff) + (std::log(OccludedCutoffHz) - std::log(openCutoff)) * occlusion);
	XAUDIO2_FILTER_PARAMETERS filter{};
	filter.Type = LowPassFilter;
	// XAudio2のフィルターの周波数は 2 * sin(π * 周波数 / サンプリング周波数) で表している
	// （xaudio2.h の XAudio2CutoffFrequencyToRadians と同じ式）。
	constexpr float Pi = MathUtility::Pi;
	const float sampleRate = static_cast<float>((std::max)(format.nSamplesPerSec, 1ul));
	const float radians = 2.0f * std::sin(Pi * (std::min)(cutoffHz / sampleRate, 0.5f));
	filter.Frequency = (std::min)(radians, XAUDIO2_MAX_FILTER_FREQUENCY);
	filter.OneOverQ = 1.0f;
	spatial.Voice->SetFilterParameters(&filter);
}

// 位置付きのボイスを止めて壊し、枠を空にしている
void Sound::ReleaseSpatialVoice(SpatialVoice& spatial)
{
	if (spatial.Voice != nullptr)
	{
		spatial.Voice->Stop(0);
		spatial.Voice->FlushSourceBuffers();
		spatial.Voice->DestroyVoice();
		spatial.Voice = nullptr;
	}
	spatial = {};
}

// 位置付きの音を全部止めている
void Sound::StopAllSpatial()
{
	for (SpatialVoice& spatial : m_SpatialVoices)
	{
		ReleaseSpatialVoice(spatial);
	}
	m_SpatialPlayOrder = 0;
}

void Sound::StopAll()
{
	StopAllSpatial();
	for (int index = 0; index < SOUND_LABEL_MAX; ++index)
	{
		Stop(static_cast<SOUND_LABEL>(index));
	}
}

// 今鳴っている位置付きの音の数を数えている
size_t Sound::GetActiveSpatialVoiceCount() const
{
	size_t count = 0;
	for (const SpatialVoice& spatial : m_SpatialVoices)
	{
		if (spatial.Voice != nullptr)
		{
			++count;
		}
	}
	return count;
}



//=============================================================================
// WAVファイルを読むための補助関数
//=============================================================================
// RIFFの中のチャンクを先頭から順に見ていき、指定した種類（fourcc）のチャンクの大きさと位置を探している
HRESULT Sound::FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
	dwChunkSize = 0;
	dwChunkDataPosition = 0;
	struct ChunkHeader { DWORD Id; DWORD Size; };
	static_assert(sizeof(ChunkHeader) == 2 * sizeof(DWORD));

	LARGE_INTEGER fileSize{};
	if (!GetFileSizeEx(hFile, &fileSize))
		return HRESULT_FROM_WIN32(GetLastError());

	ChunkHeader riff{};
	const HRESULT headerResult = ReadChunkData(hFile, &riff, sizeof(riff), 0);
	if (FAILED(headerResult))
		return headerResult;

	const auto riffEnd = sizeof(riff) + static_cast<ULONGLONG>(riff.Size);
	constexpr auto maxOffset = (std::numeric_limits<DWORD>::max)();
	if (riff.Id != RiffChunkId || riff.Size < sizeof(DWORD) ||
		riffEnd > static_cast<ULONGLONG>(fileSize.QuadPart) || riffEnd > maxOffset)
		return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

	if (fourcc == RiffChunkId)
	{
		dwChunkSize = sizeof(DWORD); // 親チャンクの先頭に WAVE の形式識別子がある。
		dwChunkDataPosition = sizeof(riff);
		return S_OK;
	}

	// RIFF の境界の内側だけを調べ、奇数長のチャンクに続くパディングを飛ばす。
	for (ULONGLONG offset = sizeof(riff) + sizeof(DWORD); offset < riffEnd;)
	{
		if (riffEnd - offset < sizeof(ChunkHeader))
			return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

		ChunkHeader chunk{};
		const HRESULT result = ReadChunkData(hFile, &chunk, sizeof(chunk), static_cast<DWORD>(offset));
		if (FAILED(result))
			return result;

		const auto dataOffset = offset + sizeof(chunk);
		const auto dataEnd = dataOffset + chunk.Size;
		if (dataEnd > riffEnd)
			return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
		if (chunk.Id == fourcc)
		{
			dwChunkSize = chunk.Size;
			dwChunkDataPosition = static_cast<DWORD>(dataOffset);
			return S_OK;
		}
		offset = dataEnd + (chunk.Size & 1u);
	}
	return S_FALSE;
}

// ReadFile は EOF でも成功を返すことがあるため、要求した長さまで読めたか確認する。
HRESULT Sound::ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
	LARGE_INTEGER offset{};
	offset.QuadPart = bufferoffset;
	if (!SetFilePointerEx(hFile, offset, nullptr, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());

	DWORD bytesRead = 0;
	if (!ReadFile(hFile, buffer, buffersize, &bytesRead, nullptr))
		return HRESULT_FROM_WIN32(GetLastError());
	return bytesRead == buffersize ? S_OK : HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}

//=============================================================================
// 鳴っている音の大きさを変えている（ループする音を、鳴らしたまま強弱させる）
//=============================================================================
void Sound::SetVolume(SOUND_LABEL label, float volume)
{
	if (!IsValidLabel(label) || m_pSourceVoice[(int)label] == nullptr)
	{
		return;
	}
	m_pSourceVoice[(int)label]->SetVolume(m_param[(int)label].volume * (std::max)(volume, 0.0f));
}
