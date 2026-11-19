#pragma once

#include <cstddef>
#include <limits>

namespace CapturePixelFormat
{
    inline constexpr int BytesPerPixel = 4;

    // PNGとMP4への取り込みはBGRA。画像の上下の向きは呼び出し側が決める。
    inline void CopyRgbaToOpaqueBgra(const unsigned char* rgba, unsigned char* bgra, std::size_t pixels)
    {
        constexpr int RedInRgba = 0;
        constexpr int Green = 1;
        constexpr int BlueInRgba = 2;
        constexpr int Alpha = 3;
        for (std::size_t x = 0; x < pixels; ++x)
        {
            const auto offset = x * BytesPerPixel;
            bgra[offset + RedInRgba] = rgba[offset + BlueInRgba];
            bgra[offset + Green] = rgba[offset + Green];
            bgra[offset + BlueInRgba] = rgba[offset + RedInRgba];
            bgra[offset + Alpha] = (std::numeric_limits<unsigned char>::max)();
        }
    }
}
