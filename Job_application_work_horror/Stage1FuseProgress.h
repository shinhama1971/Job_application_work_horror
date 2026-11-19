#pragma once

#include "GameState.h"

namespace Stage1FuseProgress
{
    inline constexpr int BeforeLastFuse = Core::GameState::RequiredFuseCount - 1;
    // 2周を抜けてから、最後のヒューズを探せるようにする。
    inline constexpr int LoopsToUnlockLastFuse = 2;
}
