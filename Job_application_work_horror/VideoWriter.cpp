// ============================================================================
// ファイルの役割: 描き終えた画面を1枚ずつ受け取り、MP4（H.264）の動画として書き出している。
// 主な技術: Media Foundation の Sink Writer（説明はVideoWriter.hを参照）
// ============================================================================

#include "VideoWriter.h"

#include <mfapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

bool VideoWriter::Open(const std::wstring& path, UINT width, UINT height, UINT fps)
{
    if (FAILED(MFStartup(MF_VERSION)))
    {
        return false;
    }
    m_Started = true;
    m_Width = width;
    m_Height = height;
    m_Time = 0;
    // 1フレームの長さ（Media Foundationの時間の単位は100ナノ秒）
    m_FrameDuration = 10'000'000LL / fps;

    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &m_Writer)))
    {
        return false;
    }

    // 出力の形式：H.264、プログレッシブ、指定した大きさとフレームレート
    ComPtr<IMFMediaType> outputType;
    MFCreateMediaType(&outputType);
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outputType->SetUINT32(MF_MT_AVG_BITRATE, 8'000'000);
    outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(m_Writer->AddStream(outputType.Get(), &m_Stream)))
    {
        return false;
    }

    // 入力の形式：毎フレーム渡す非圧縮のRGB32画像
    ComPtr<IMFMediaType> inputType;
    MFCreateMediaType(&inputType);
    inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(m_Writer->SetInputMediaType(m_Stream, inputType.Get(), nullptr)) ||
        FAILED(m_Writer->BeginWriting()))
    {
        return false;
    }
    m_Open = true;
    return true;
}

void VideoWriter::WriteFrame(const BYTE* rgba, UINT rowPitch)
{
    if (!m_Open)
    {
        return;
    }
    const DWORD size = m_Width * m_Height * 4;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(size, &buffer)))
    {
        return;
    }
    // 1行ずつ上下を逆にしながら、RとBを入れ替えてコピーしている（透明度は使わないので不透明）
    BYTE* destination = nullptr;
    buffer->Lock(&destination, nullptr, nullptr);
    for (UINT y = 0; y < m_Height; ++y)
    {
        const BYTE* sourceRow = rgba + static_cast<size_t>(y) * rowPitch;
        BYTE* destinationRow = destination +
            static_cast<size_t>(m_Height - 1 - y) * m_Width * 4;
        for (UINT x = 0; x < m_Width; ++x)
        {
            destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
            destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
            destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
            destinationRow[x * 4 + 3] = 255;
        }
    }
    buffer->Unlock();
    buffer->SetCurrentLength(size);

    // 画像を1枚のサンプルにして、表示する時刻と長さを付けて書き込んでいる
    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(m_Time);
    sample->SetSampleDuration(m_FrameDuration);
    m_Writer->WriteSample(m_Stream, sample.Get());
    m_Time += m_FrameDuration;
}

void VideoWriter::Close()
{
    if (m_Open)
    {
        m_Writer->Finalize();
        m_Open = false;
    }
    m_Writer.Reset();
    if (m_Started)
    {
        MFShutdown();
        m_Started = false;
    }
}
