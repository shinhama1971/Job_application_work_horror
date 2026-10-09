// ============================================================================
// ファイルの役割: XAudio2による効果音・環境音の読み込み、再生、解放を管理している。
// 主な技術: XAudio2、X3DAudio（立体音響）、ローパスフィルター、RIFF/WAVEの解析、Source Voice、RAII
// ============================================================================

#include "sound.h"

#include <algorithm>
#include <cmath>

// X3DAudioInitialize / X3DAudioCalculate は XAudio2 2.9 のライブラリに含まれている。
#pragma comment(lib, "xaudio2.lib")

using DirectX::SimpleMath::Vector3;

namespace
{
	// 1ワールド単位はおよそ4cm（目の高さが40）。この距離までは小さくならず、その先は距離に反比例して小さくなる。
	constexpr float SpatialCurveDistance = 30.0f;
	// これより近い音は方向をはっきりさせず、耳元で広がるように聞かせている。
	constexpr float SpatialInnerRadius = 6.0f;
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
}

#ifdef _XBOX // Xbox（ビッグエンディアン）用のチャンクの名前
#define fourccRIFF 'RIFF'
#define fourccDATA 'data'
#define fourccFMT 'fmt '
#define fourccWAVE 'WAVE'
#define fourccXWMA 'XWMA'
#define fourccDPDS 'dpds'
#endif

// 壊すときに解放している
Sound::~Sound()
{
	Uninit();
}
#ifndef _XBOX // Windows（リトルエンディアン）用のチャンクの名前。4文字を逆順に並べている
#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'
#endif

//=============================================================================
// 初期化：COMとXAudio2を準備し、全部の素材のWAVを読み込んでいる
//=============================================================================
HRESULT Sound::Init()
{
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
		if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			CloseHandle(hFile);
			Uninit();
			return hr;
		}

		// ファイルの形式がWAVEであることを確かめている。
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

		// 音声の形式（fmtチャンク）を読み込んでいる
		hr = FindChunk(hFile, fourccFMT, dwChunkSize, dwChunkPosition);
		if (FAILED(hr) || hr == S_FALSE || dwChunkSize > sizeof(m_wfx[i]) ||
			FAILED(ReadChunkData(hFile, &m_wfx[i], dwChunkSize, dwChunkPosition)))
		{
			CloseHandle(hFile);
			Uninit();
			return FAILED(hr) ? hr : E_FAIL;
		}

		// 音声データ（dataチャンク）の中身を、再生用のバッファへ読み込んでいる。
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
	if (!IsValidLabel(label))
	{
		return;
	}

	if (m_pXAudio2 == nullptr)
	{
		return;
	}

	// 前のソースボイスを壊して、新しく作り直している
	IXAudio2SourceVoice*& pSV = m_pSourceVoice[(int)label];

	if (pSV != nullptr)
	{
		pSV->DestroyVoice();
		pSV = nullptr;
	}

	// ソースボイスを作っている
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

	// WAVごとの音の大きさの差を吸収し、環境音が効果音を覆い隠さないようにしている。
	pSV->SetVolume(m_param[(int)label].volume * (std::max)(volume, 0.0f));
	// 音の高さ（再生速度）は0.70〜1.35倍に制限している
	pSV->SetFrequencyRatio((std::clamp)(pitch, 0.70f, 1.35f));

	// 再生を始めている
	pSV->Start(0);

}

//=============================================================================
// 停止（音声データが残っていれば止めている）
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
// 再開（Stopで止めた音を続きから鳴らしている）
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
	if (!m_X3DAudioReady)
	{
		// 立体音響が使えない環境では、位置を持たない普通の再生に切り替えている。
		Play(label, pitch);
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
		slot = &m_SpatialVoices[m_NextSpatialVoice];
		m_NextSpatialVoice = (m_NextSpatialVoice + 1) % MaxSpatialVoices;
		ReleaseSpatialVoice(*slot);
	}

	// こもらせるためのローパスフィルターを使えるボイスとして作っている。
	const int index = static_cast<int>(label);
	HRESULT hr = m_pXAudio2->CreateSourceVoice(
		&slot->Voice, &m_wfx[index].Format, XAUDIO2_VOICE_USEFILTER);
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
	// 鳴り始めから壁の向こうの音として聞こえるよう、最初のさえぎる物の量はその場で求めている。
	slot->MinimumOcclusion = (std::clamp)(minimumOcclusion, 0.0f, 1.0f);
	slot->Occlusion = (std::max)(QueryOcclusion(position), slot->MinimumOcclusion);
	slot->Voice->SetVolume(m_param[index].volume * (std::max)(volume, 0.0f));
	// 音の高さは0.50〜1.50倍に制限している（位置なしの再生より広い範囲にしている）
	slot->Voice->SetFrequencyRatio((std::clamp)(pitch, 0.50f, 1.50f));
	ApplySpatialMix(*slot);
	slot->Voice->Start(0);
}

// 聞き手を更新し、鳴り終わった音を片付け、鳴っている音のさえぎる物の量と聞こえ方を更新している
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
	const UINT32 sourceChannels = (std::clamp)(static_cast<UINT32>(format.nChannels), 1u, 8u);
	const UINT32 outputChannels = (std::min)(m_OutputChannels, 8u);

	// 聞き手の上方向は、前方向に直交するように作り直している（X3DAudioの決まり）。
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

	// ステレオの素材も、すべてのチャンネルを同じ1点から鳴らしている。
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
	constexpr float Pi = 3.14159265f;
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
	spatial.Occlusion = 0.0f;
	spatial.MinimumOcclusion = 0.0f;
}

// 位置付きの音を全部止めている
void Sound::StopAllSpatial()
{
	for (SpatialVoice& spatial : m_SpatialVoices)
	{
		ReleaseSpatialVoice(spatial);
	}
	m_NextSpatialVoice = 0;
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

// 指定した位置から、指定した大きさだけデータを読み込んでいる
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
