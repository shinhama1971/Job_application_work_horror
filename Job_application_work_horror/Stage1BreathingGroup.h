// ============================================================================
// ファイルの役割: 1面の呼吸する壁の組。組ごとに、呼吸を始める時と膨らむ量を変えている。
// ============================================================================

#pragma once

#include <cstddef>

// 廊下（1周した後から）・中央ホール（3周目から）・建物の外周（3本目のヒューズを拾った後から）
enum class Stage1BreathingGroup
{
    Corridor,
    Hall,
    Building,
    Count
};

constexpr std::size_t Stage1BreathingGroupCount = static_cast<std::size_t>(Stage1BreathingGroup::Count);
