// ============================================================================
// ファイルの役割: 描き終えた画面を1枚ずつ受け取り、MP4（H.264）の動画として書き出している。
// 主な技術: Media Foundation の Sink Writer（説明はVideoWriter.hを参照）
// ============================================================================

#include "VideoWriter.h"

#include <limits>

#include <mfapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // Media Foundation の時間の単位（100ナノ秒）で数えた1秒と、動画のビットレート（1秒あたり8メガビット）
    constexpr LONGLONG MediaTimeUnitsPerSecond = 10'000'000LL;
    constexpr UINT32 VideoBitrate = 8'000'000;
    // BGRA の1画素のバイト数と、不透明な透明度
    constexpr int BytesPerPixel = 4;
    // BGRAでは、末尾のバイトが透明度。
    constexpr int BgraAlphaByte = BytesPerPixel - 1;
    constexpr BYTE OpaqueAlpha = 255;
}

bool VideoWriter::Open(const std::wstring& path, UINT width, UINT height, UINT fps)
{
    if (m_Started || width == 0 || height == 0 || fps == 0 || FAILED(MFStartup(MF_VERSION)))
    {
        return false;
    }
    m_Started = true;
    const auto failOpen = [this]()
    {
        Close();
        return false;
    };
    m_Width = width;
    m_Height = height;
    m_Time = 0;
    // 1フレームの長さ（Media Foundationの時間の単位は100ナノ秒）
    m_FrameDuration = MediaTimeUnitsPerSecond / fps;

    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &m_Writer)))
    {
        return failOpen();
    }

    // 出力の形式：H.264、プログレッシブ、指定した大きさとフレームレート
    ComPtr<IMFMediaType> outputType;
    if (FAILED(MFCreateMediaType(&outputType)) ||
        FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
        FAILED(outputType->SetUINT32(MF_MT_AVG_BITRATE, VideoBitrate)) ||
        FAILED(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height)) ||
        FAILED(MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fps, 1)) ||
        FAILED(MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(m_Writer->AddStream(outputType.Get(), &m_Stream)))
    {
        return failOpen();
    }

    // 入力の形式：毎フレーム渡す非圧縮のRGB32画像
    ComPtr<IMFMediaType> inputType;
    if (FAILED(MFCreateMediaType(&inputType)) ||
        FAILED(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height)) ||
        FAILED(MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fps, 1)) ||
        FAILED(MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(m_Writer->SetInputMediaType(m_Stream, inputType.Get(), nullptr)) ||
        FAILED(m_Writer->BeginWriting()))
    {
        return failOpen();
    }
    m_Open = true;
    return true;
}

bool VideoWriter::WriteFrame(const BYTE* rgba, UINT rowPitch)
{
    const auto rowBytes = static_cast<UINT64>(m_Width) * BytesPerPixel;
    if (!m_Open || rgba == nullptr || rowBytes > rowPitch ||
        rowBytes > (std::numeric_limits<DWORD>::max)() / m_Height)
    {
        return false;
    }
    const auto size = static_cast<DWORD>(rowBytes * m_Height);
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(size, &buffer)))
    {
        return false;
    }
    // 1行ずつ上下を逆にしながら、RとBを入れ替えてコピーしている（透明度は使わないので不透明）
    BYTE* destination = nullptr;
    if (FAILED(buffer->Lock(&destination, nullptr, nullptr)))
    {
        return false;
    }
    if (destination == nullptr)
    {
        buffer->Unlock();
        return false;
    }
    for (UINT y = 0; y < m_Height; ++y)
    {
        const BYTE* sourceRow = rgba + static_cast<size_t>(y) * rowPitch;
        BYTE* destinationRow = destination +
            static_cast<size_t>(m_Height - 1 - y) * m_Width * BytesPerPixel;
        for (UINT x = 0; x < m_Width; ++x)
        {
            destinationRow[x * BytesPerPixel + 0] = sourceRow[x * BytesPerPixel + 2];
            destinationRow[x * BytesPerPixel + 1] = sourceRow[x * BytesPerPixel + 1];
            destinationRow[x * BytesPerPixel + 2] = sourceRow[x * BytesPerPixel + 0];
            destinationRow[x * BytesPerPixel + BgraAlphaByte] = OpaqueAlpha;
        }
    }
    if (FAILED(buffer->Unlock()) || FAILED(buffer->SetCurrentLength(size)))
    {
        return false;
    }

    // 画像を1枚のサンプルにして、表示する時刻と長さを付けて書き込んでいる
    ComPtr<IMFSample> sample;
    if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
        FAILED(sample->SetSampleTime(m_Time)) || FAILED(sample->SetSampleDuration(m_FrameDuration)) ||
        FAILED(m_Writer->WriteSample(m_Stream, sample.Get())))
    {
        return false;
    }
    m_Time += m_FrameDuration;
    return true;
}

bool VideoWriter::Close()
{
    bool succeeded = true;
    if (m_Open)
    {
        succeeded = SUCCEEDED(m_Writer->Finalize());
        m_Open = false;
    }
    m_Writer.Reset();
    if (m_Started)
    {
        succeeded = SUCCEEDED(MFShutdown()) && succeeded;
        m_Started = false;
    }
    return succeeded;
}
