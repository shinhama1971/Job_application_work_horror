// ============================================================================
// ファイルの役割: HUD の各画面（Hud.cpp・HudTitle.cpp・HudMenus.cpp・HudScreens.cpp）で共通に使う、文字と色の小さな計算。
// ============================================================================

#pragma once

#include "StoryText.h"

#include <SimpleMath.h>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace HudText
{
    // UTF-8の文字列で、画面に出る文字の数を数えている（2バイト目以降のバイトは数えない。中央に置くための幅の計算に使っている）
    inline std::size_t CountDisplayedCharacters(std::string_view text)
    {
        std::size_t count = 0;
        for (unsigned char byte : text)
        {
            if (!StoryText::IsUtf8ContinuationByte(byte))
            {
                ++count;
            }
        }
        return count;
    }

    // 色の透明度に、表示の進み具合（0〜1）を掛けている
    inline DirectX::SimpleMath::Color Faded(const DirectX::SimpleMath::Color& color, float reveal)
    {
        return DirectX::SimpleMath::Color(color.x, color.y, color.z, color.w * reveal);
    }

    // 1分・1時間の秒数（時間を分と秒、時・分・秒に分けて見せるため）
    constexpr int SecondsPerMinute = 60;
    constexpr int SecondsPerHour = 3600;
}

// 電波が乱れたように見せる文字（1面の前の導入の「通信途絶」と、無線の字幕で同じ揺れ方を使っている）
namespace SignalTextLayout
{
    using DirectX::SimpleMath::Color;

    // 文字の乱れ（導入と無線の字幕で共通）：sin を使う決まった式で 0〜1 の値を作る係数、1秒あたりの刻み、
    // 横のずれを始めるノイズの強さと幅、色をにじませるノイズの強さとずれ、にじむ赤
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;
    constexpr float NoiseStepsPerSecond = 24.0f;
    constexpr float JitterNoiseLevel = 0.1f;
    constexpr float JitterWidth = 22.0f;
    constexpr float SplitNoiseLevel = 0.3f;
    constexpr float SplitOffset = 4.0f;
    const Color SplitRed(0.95f, 0.18f, 0.16f, 0.35f);
    // 本文の白っぽい文字の色
    const Color LineColor(0.86f, 0.90f, 0.86f, 1.0f);
    // 「通信途絶」：本体の赤、にじむ青緑（ノイズを掛ける）とそのずれ、抜ける横の帯（起こすノイズの強さ・乱数の倍率・
    // 左右のはみ出し・太さ（最低）・色）
    const Color SignalLostRed(0.92f, 0.32f, 0.26f, 0.95f);
    const Color SignalLostCyan(0.18f, 0.85f, 0.90f, 0.40f);
    constexpr float SignalLostSplit = 6.0f;
    constexpr float BarNoiseLevel = 0.2f;
    constexpr float BarSeedStep = 2.3f;
    constexpr float BarSeedPerBar = 5.1f;
    constexpr float BarLeftMargin = 10.0f;
    constexpr float BarExtraWidth = 20.0f;
    constexpr float BarHeightBase = 2.0f;
    // 受信の印の明滅（1秒のうち点いている割合）と、印の色
    constexpr float BlinkOn = 0.6f;
    const Color ReceiveMarkColor(0.48f, 0.90f, 0.58f, 0.90f);
    // 背景の走査線の間隔と色
    constexpr float ScanlineSpacing = 6.0f;
    const Color ScanlineColor(0.0f, 0.0f, 0.0f, 0.10f);

    inline float NoiseHash(float value)
    {
        const float s = std::sin(value * HashFrequency) * HashAmplitude;
        return s - std::floor(s);
    }
}
