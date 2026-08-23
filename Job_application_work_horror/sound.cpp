#include "sound.h"

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
	hr = XAudio2Create(&m_pXAudio2, 0);		// 第二引数は､動作フラグ デバッグモードの指定(現在は未使用なので0にする)
	//hr=XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);		// 第三引数は、windowsでは無視
	if (FAILED(hr)) {
		Uninit();
		return hr;
	}

	/**** Create Mastering Voice ****/
	hr = m_pXAudio2->CreateMasteringVoice(&m_pMasteringVoice);			// 今回はＰＣのデフォルト設定に任せている
	/*, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL*/		// 本当６個の引数を持っている
	if (FAILED(hr)) {
		Uninit();
		return hr;
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

		//check the file type, should be fourccWAVE or 'XWMA'
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

		//fill out the audio data buffer with the contents of the fourccDATA chunk
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

		// 	サブミットボイスで利用するサブミットバッファの設定
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
		m_pXAudio2->Release();
		m_pXAudio2 = nullptr;
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
void Sound::Play(SOUND_LABEL label)
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
// 一時停止
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
