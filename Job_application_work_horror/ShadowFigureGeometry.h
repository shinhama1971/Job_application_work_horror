#pragma once

#include <SimpleMath.h>

namespace ShadowFigureGeometry
{
    // FBXをこの倍率で割って読み込み、描画時に掛け戻す。
    inline const DirectX::SimpleMath::Vector3 ModelScale(8.0f, 16.0f, 8.0f);
    // 森と施設で共通の人影は、モデル本来の身長約31から約54へ伸ばす。
    inline const DirectX::SimpleMath::Vector3 DisplayScale(1.45f, 1.75f, 1.45f);
    inline constexpr float ChestHeight = 30.0f;
}
