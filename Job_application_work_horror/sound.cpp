// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理します。
// 主な技術: XAudio2、X3DAudio（立体音響）、ローパスフィルター、RIFF/WAVE解析、Source Voice、RAII
// ============================================================================

#include "sound.h"

#include <algorithm>
#include <cmath>

// X3DAudioInitialize / X3DAudioCalculate は XAudio2 2.9 のライブラリに含まれます。
#pragma comment(lib, "xaudio2.lib")

using DirectX::SimpleMath::Vector3;

namespace
{
	// 1ワールド単位はおよそ4cm（目の高さが40）。この距離までは減衰せず、以降は距離に反比例して小さくなります。
	constexpr float SpatialCurveDistance = 30.0f;
	// これより近い音は方向をはっきりさせず、耳元で広がるように聞かせます。
	constexpr float SpatialInnerRadius = 6.0f;
	// 壁の向こうの音量と、こもり具合（ローパスフィルターの周波数）です。
	constexpr float OccludedVolume = 0.38f;
	constexpr float OccludedCutoffHz = 650.0f;
	// 遠くの音ほど高音が空気に吸われる様子を近似します。
	constexpr float OpenAirCutoffHz = 16000.0f;
	constexpr float DistantCutoffHz = 4500.0f;
	constexpr float DistantCutoffRange = 600.0f;
	// 壁の出入りで音がぷつっと変わらないよう、遮蔽量をこの速さで追従させます。
	constexpr float OcclusionResponsePerSecond = 7.0f;

	X3DAUDIO_VECTOR ToX3DAudio(const Vector3& value)
	{
		X3DAUDIO_VECTOR result{};
		result.x = value.x;
		result.y = value.y;
		result.z = value.z;
		return result;
	}
}

#ifdef _XBOX //Big-Endian
#define fourccRIFF 'RIFF'
#define fourccDATA 'data'
#define fourccFMT 'fmt '
#define fourccWAVE 'WAVE'
#define fourccXWMA 'XWMA'
#define fourccDPDS 'dpds'
#endif

Sound::~Sound()
{
	Uninit();
}
#ifndef _XBOX //Little-Endian
#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'
#endif

//=============================================================================
// 初期化
//=============================================================================
HRESULT Sound::Init()
{
	HRESULT hr;

	HANDLE hFile;
	DWORD  dwChunkSize;
	DWORD  dwChunkPosition;
	DWORD  filetype;

	// COMの初期化
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		return hr;
	}
	m_IsComInitialized = true;

	/**** Create XAudio2 ****/
	// 第2引数の動作フラグはWindowsでは使わないため0を渡します。
	hr = XAudio2Create(m_pXAudio2.ReleaseAndGetAddressOf(), 0);
	if (FAILED(hr)) {
		Uninit();
		return hr;
	}

	/**** Create Mastering Voice ****/
	// チャンネル数・サンプリング周波数は既定値にし、PCの出力設定（ステレオ・5.1chなど）に合わせます。
	hr = m_pXAudio2->CreateMasteringVoice(&m_pMasteringVoice);
	if (FAILED(hr)) {
		Uninit();
		return hr;
	}

	/**** Initialize X3DAudio ****/
	// 出力先（ステレオ・5.1chなど）のスピーカー配置に合わせて、音の振り分けを計算させます。
	// 立体音響だけが使えない環境でも、通常の再生は続けられるようにします。
	{
		DWORD channelMask = 0;
		if (FAILED(m_pMasteringVoice->GetChannelMask(&channelMask)) || channelMask == 0)
		{
			channelMask = SPEAKER_STEREO;
		}
		XAUDIO2_VOICE_DETAILS masterDetails{};
		m_pMasteringVoice->GetVoiceDetails(&masterDetails);
		m_OutputChannels = (std::max)(masterDetails.InputChannels, 1u);
		m_X3DAudioReady = SUCCEEDED(
			X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, m_X3DAudio));
	}

	/**** Initalize Sound ****/
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
		if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			CloseHandle(hFile);
			Uninit();
			return hr;
		}

		// ファイル形式がfourccWAVEまたはXWMAであることを確認します。
		hr = FindChunk(hFile, fourccRIFF, dwChunkSize, dwChunkPosition);
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
		if (filetype != fourccWAVE) {
			CloseHandle(hFile);
			Uninit();
			return E_FAIL;
		}

		hr = FindChunk(hFile, fourccFMT, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE || dwChunkSize > sizeof(m_wfx[i]) ||
			FAILED(ReadChunkData(hFile, &m_wfx[i], dwChunkSize, dwChunkPosition)))
		{
			CloseHandle(hFile);
			Uninit();
			return FAILED(hr) ? hr : E_FAIL;
		}

		// fourccDATAチャンクの内容を再生用オーディオバッファへ設定します。
		hr = FindChunk(hFile, fourccDATA, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE || dwChunkSize == 0)
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

		// ソースボイスへ渡す再生用バッファ。ループ指定の素材（環境音）は無限に繰り返します。
		m_buffer[i].AudioBytes = dwChunkSize;
		m_buffer[i].pAudioData = m_DataBuffer[i].get();
		m_buffer[i].Flags = XAUDIO2_END_OF_STREAM;
		if (m_param[i].bLoop)
			m_buffer[i].LoopCount = XAUDIO2_LOOP_INFINITE;
		else
			m_buffer[i].LoopCount = 0;

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
// 開放処理
//=============================================================================
void Sound::Uninit(void)
{
	// 位置付きの音は音声データ（m_DataBuffer）を参照しているため、データより先に止めます。
	StopAllSpatial();
	m_X3DAudioReady = false;

	for (int i = 0; i < SOUND_LABEL_MAX; i++)
	{
		if (m_pSourceVoice[i])
		{
			m_pSourceVoice[i]->Stop(0);
			m_pSourceVoice[i]->FlushSourceBuffers();
			m_pSourceVoice[i]->DestroyVoice();			// オーディオグラフからソースボイスを削除
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

	// COMの破棄
	if (m_IsComInitialized)
	{
		CoUninitialize();
		m_IsComInitialized = false;
	}
}

//=============================================================================
// 再生
//=============================================================================
void Sound::Play(SOUND_LABEL label, float pitch)
{
	if (!IsValidLabel(label))
	{
		return;
	}

	if (m_pXAudio2 == nullptr)
	{
		return;
	}

	IXAudio2SourceVoice*& pSV = m_pSourceVoice[(int)label];

	if (pSV != nullptr)
	{
		pSV->DestroyVoice();
		pSV = nullptr;
	}

	// ソースボイス作成
	HRESULT hr = m_pXAudio2->CreateSourceVoice(
		&pSV,
		&(m_wfx[(int)label].Format)
	);
	if (FAILED(hr) || pSV == nullptr)
	{
		pSV = nullptr;
		return;
	}

	hr = pSV->SubmitSourceBuffer(&(m_buffer[(int)label]));
	if (FAILED(hr))
	{
		pSV->DestroyVoice();
		pSV = nullptr;
		return;
	}

	// WAVごとの音圧差を吸収し、環境音が効果音を覆わないようにします。
	pSV->SetVolume(m_param[(int)label].volume);
	pSV->SetFrequencyRatio((std::clamp)(pitch, 0.70f, 1.35f));

	// 再生
	pSV->Start(0);

}

//=============================================================================
// 停止
//=============================================================================
void Sound::Stop(SOUND_LABEL label)
{
	if (!IsValidLabel(label))
	{
		return;
	}

	if (m_pSourceVoice[(int)label] == NULL) return;

	XAUDIO2_VOICE_STATE xa2state;
	m_pSourceVoice[(int)label]->GetState(&xa2state);
	if (xa2state.BuffersQueued)
	{
		m_pSourceVoice[(int)label]->Stop(0);
	}
}

//=============================================================================
// 再開（Stopで止めた音を続きから鳴らす）
//=============================================================================
void Sound::Resume(SOUND_LABEL label)
{
	if (!IsValidLabel(label))
	{
		return;
	}

	IXAudio2SourceVoice*& pSV = m_pSourceVoice[(int)label];
	if (pSV != nullptr)
	{
		pSV->Start();
	}
}

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
void Sound::PlayAt(SOUND_LABEL label, const Vector3& position, float pitch, float volume)
{
	if (!IsValidLabel(label) || m_pXAudio2 == nullptr)
	{
		return;
	}
	if (!m_X3DAudioReady)
	{
		// 立体音響が使えない環境では、位置を持たない通常の再生に切り替えます。
		Play(label, pitch);
		return;
	}

	// 鳴り終わった枠を探します。全部使用中なら、いちばん古く鳴らした音を止めて使います。
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
		slot = &m_SpatialVoices[m_NextSpatialVoice];
		m_NextSpatialVoice = (m_NextSpatialVoice + 1) % MaxSpatialVoices;
		ReleaseSpatialVoice(*slot);
	}

	// こもらせるためのローパスフィルターを使えるボイスとして作ります。
	const int index = static_cast<int>(label);
	HRESULT hr = m_pXAudio2->CreateSourceVoice(
		&slot->Voice, &m_wfx[index].Format, XAUDIO2_VOICE_USEFILTER);
	if (FAILED(hr) || slot->Voice == nullptr)
	{
		slot->Voice = nullptr;
		return;
	}

	// ループ指定の素材でも、位置付きの音は1回だけ鳴らします。
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
	// 鳴り始めから壁の向こうの音として聞こえるよう、最初の遮蔽量はその場で求めます。
	slot->Occlusion = QueryOcclusion(position);
	slot->Voice->SetVolume(m_param[index].volume * (std::max)(volume, 0.0f));
	slot->Voice->SetFrequencyRatio((std::clamp)(pitch, 0.50f, 1.50f));
	ApplySpatialMix(*slot);
	slot->Voice->Start(0);
}

void Sound::UpdateListener(const SoundListener& listener, float deltaTime)
{
	m_Listener = listener;
	if (!m_X3DAudioReady)
	{
		return;
	}

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

		// 振り向いたり歩いたりして位置関係が変わった分を、鳴っている途中の音にも反映します。
		const float targetOcclusion = QueryOcclusion(spatial.Position);
		spatial.Occlusion += (targetOcclusion - spatial.Occlusion) * response;
		ApplySpatialMix(spatial);
	}
}

float Sound::QueryOcclusion(const Vector3& emitter) const
{
	if (!m_OcclusionQuery)
	{
		return 0.0f;
	}
	return (std::clamp)(m_OcclusionQuery(m_Listener.Position, emitter), 0.0f, 1.0f);
}

void Sound::ApplySpatialMix(SpatialVoice& spatial)
{
	const WAVEFORMATEX& format = m_wfx[static_cast<int>(spatial.Label)].Format;
	const UINT32 sourceChannels = (std::clamp)(static_cast<UINT32>(format.nChannels), 1u, 8u);
	const UINT32 outputChannels = (std::min)(m_OutputChannels, 8u);

	// 聞き手の上方向は、前方向に直交するように作り直します（X3DAudioの要件）。
	Vector3 forward = m_Listener.Forward;
	if (forward.LengthSquared() < 0.0001f)
	{
		forward = Vector3(0.0f, 0.0f, 1.0f);
	}
	forward.Normalize();
	Vector3 up = Vector3(0.0f, 1.0f, 0.0f) - forward * forward.y;
	if (up.LengthSquared() < 0.0001f)
	{
		up = Vector3(0.0f, 0.0f, 1.0f);
	}
	up.Normalize();

	X3DAUDIO_LISTENER listener{};
	listener.OrientFront = ToX3DAudio(forward);
	listener.OrientTop = ToX3DAudio(up);
	listener.Position = ToX3DAudio(m_Listener.Position);

	// ステレオ素材も、すべてのチャンネルを同じ1点から鳴らします。
	float channelAzimuths[8] = {};
	X3DAUDIO_EMITTER emitter{};
	emitter.Position = ToX3DAudio(spatial.Position);
	emitter.OrientFront = ToX3DAudio(Vector3(0.0f, 0.0f, 1.0f));
	emitter.OrientTop = ToX3DAudio(Vector3(0.0f, 1.0f, 0.0f));
	emitter.ChannelCount = sourceChannels;
	emitter.ChannelRadius = 0.0f;
	emitter.pChannelAzimuths = channelAzimuths;
	emitter.InnerRadius = SpatialInnerRadius;
	emitter.InnerRadiusAngle = X3DAUDIO_PI / 4.0f;
	emitter.CurveDistanceScaler = SpatialCurveDistance;
	emitter.DopplerScaler = 0.0f;

	float matrix[8 * 8] = {};
	X3DAUDIO_DSP_SETTINGS dsp{};
	dsp.SrcChannelCount = sourceChannels;
	dsp.DstChannelCount = outputChannels;
	dsp.pMatrixCoefficients = matrix;
	X3DAudioCalculate(m_X3DAudio, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);

	// 壁の向こうの音は小さく、高音を削ってこもらせます。遠い音も少しだけ高音を落とします。
	const float occlusion = spatial.Occlusion;
	const float occlusionGain = 1.0f + (OccludedVolume - 1.0f) * occlusion;
	for (UINT32 i = 0; i < sourceChannels * outputChannels; ++i)
	{
		matrix[i] *= occlusionGain;
	}
	spatial.Voice->SetOutputMatrix(m_pMasteringVoice, sourceChannels, outputChannels, matrix);

	const float distanceAmount = (std::min)(dsp.EmitterToListenerDistance / DistantCutoffRange, 1.0f);
	const float openCutoff = OpenAirCutoffHz + (DistantCutoffHz - OpenAirCutoffHz) * distanceAmount;
	// 周波数は人の聞こえ方に合わせ、対数的に補間します。
	const float cutoffHz = std::exp(
		std::log(openCutoff) + (std::log(OccludedCutoffHz) - std::log(openCutoff)) * occlusion);
	XAUDIO2_FILTER_PARAMETERS filter{};
	filter.Type = LowPassFilter;
	// XAudio2のフィルター周波数は 2 * sin(π * 周波数 / サンプリング周波数) で表します
	// （xaudio2.h の XAudio2CutoffFrequencyToRadians と同じ式）。
	constexpr float Pi = 3.14159265f;
	const float sampleRate = static_cast<float>((std::max)(format.nSamplesPerSec, 1ul));
	const float radians = 2.0f * std::sin(Pi * (std::min)(cutoffHz / sampleRate, 0.5f));
	filter.Frequency = (std::min)(radians, XAUDIO2_MAX_FILTER_FREQUENCY);
	filter.OneOverQ = 1.0f;
	spatial.Voice->SetFilterParameters(&filter);
}

void Sound::ReleaseSpatialVoice(SpatialVoice& spatial)
{
	if (spatial.Voice != nullptr)
	{
		spatial.Voice->Stop(0);
		spatial.Voice->FlushSourceBuffers();
		spatial.Voice->DestroyVoice();
		spatial.Voice = nullptr;
	}
	spatial.Occlusion = 0.0f;
}

void Sound::StopAllSpatial()
{
	for (SpatialVoice& spatial : m_SpatialVoices)
	{
		ReleaseSpatialVoice(spatial);
	}
	m_NextSpatialVoice = 0;
}

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
// ユーティリティ関数群
//=============================================================================
HRESULT Sound::FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD dwChunkType;
	DWORD dwChunkDataSize;
	DWORD dwRIFFDataSize = 0;
	DWORD dwFileType;
	DWORD bytesRead = 0;
	DWORD dwOffset = 0;
	while (hr == S_OK)
	{
		DWORD dwRead;
		if (0 == ReadFile(hFile, &dwChunkType, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());
		if (0 == ReadFile(hFile, &dwChunkDataSize, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());
		switch (dwChunkType)
		{
		case fourccRIFF:
			dwRIFFDataSize = dwChunkDataSize;
			dwChunkDataSize = 4;
			if (0 == ReadFile(hFile, &dwFileType, sizeof(DWORD), &dwRead, NULL))
				hr = HRESULT_FROM_WIN32(GetLastError());
			break;
		default:
			if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, dwChunkDataSize, NULL, FILE_CURRENT))
				return HRESULT_FROM_WIN32(GetLastError());
		}
		dwOffset += sizeof(DWORD) * 2;
		if (dwChunkType == fourcc)
		{
			dwChunkSize = dwChunkDataSize;
			dwChunkDataPosition = dwOffset;
			return S_OK;
		}
		dwOffset += dwChunkDataSize;
		if (bytesRead >= dwRIFFDataSize) return S_FALSE;
	}
	return S_OK;
}

HRESULT Sound::ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, bufferoffset, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD dwRead;
	if (0 == ReadFile(hFile, buffer, buffersize, &dwRead, NULL))
		hr = HRESULT_FROM_WIN32(GetLastError());
	return hr;
}
