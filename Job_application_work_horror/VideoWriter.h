// ============================================================================
// ファイルの役割: 描き終えた画面を1枚ずつ受け取り、MP4（H.264）の動画として書き出している。
// 主な技術: Media Foundation の Sink Writer（非圧縮のRGB32を渡すと、H.264に符号化してMP4へ書く）
// ・ゲームの自動撮影（CaptureMode のプレイ動画）と、技術デモの比較動画で同じものを使っている。
// ============================================================================

#pragma once

#include <Windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <wrl/client.h>

class VideoWriter
{
public:
    // MP4ファイルを作り、出力（H.264、8Mbps）と入力（RGB32の非圧縮画像）の形式を設定している
    bool Open(const std::wstring& path, UINT width, UINT height, UINT fps);
    // rgbaは上の行から並んだRGBA。RGB32（BGRA、下の行から並ぶ）へ詰め替えてから渡している。
    void WriteFrame(const BYTE* rgba, UINT rowPitch);
    // 書き込みを終えてファイルを閉じ、Media Foundationを終了している
    void Close();
    // 書き込める状態か
    bool IsOpen() const { return m_Open; }

private:
    // 書き込み先、ストリーム番号、画像の大きさ、次のフレームの時刻、1フレームの長さ、開いているか、MFStartupを呼んだか
    Microsoft::WRL::ComPtr<IMFSinkWriter> m_Writer;
    DWORD m_Stream = 0;
    UINT m_Width = 0;
    UINT m_Height = 0;
    LONGLONG m_Time = 0;
    LONGLONG m_FrameDuration = 0;
    bool m_Open = false;
    bool m_Started = false;
};
