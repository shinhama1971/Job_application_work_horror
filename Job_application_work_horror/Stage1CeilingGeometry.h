#pragma once

namespace Stage1CeilingGeometry
{
    // 配置ファイルの PropCeilingMain などの天井板の下面。
    inline constexpr float UndersideY = -36.5f;
    // 壁の上端と天井に張り付く影は、天井の下面より少し下に置く。
    inline constexpr float WallTopOffset = 0.5f;
    inline constexpr float WallTopY = UndersideY - WallTopOffset;
    // 足音は、天井板より上で鳴らす。
    inline constexpr float VoidOffset = 4.5f;
    inline constexpr float VoidY = UndersideY + VoidOffset;
    // 通路の壁は、配置ファイルの壁と同じ高さ・中心を使う。
    inline constexpr float WallBottomY = -99.0f;
    inline constexpr float WallHeight = WallTopY - WallBottomY;
    inline constexpr float WallCenterY = (WallTopY + WallBottomY) * 0.5f;
}
