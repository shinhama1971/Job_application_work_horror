// ============================================================================
// ファイルの役割: 配置ファイルの「何個でも置ける物」（名前の頭で種類が決まる place 行）を、ステージに作っている。
// 主な技術: 名前の頭での種類の見分け、配置ファイルの読み直しへの対応
//
// ・ExtraLight_   で始まる place は天井照明（CeilingLight）。停電中も赤い非常灯として点き、電力が戻ると普通の照明になる。
// ・ExtraBattery_ で始まる place は電池（BatteryItem）。
// ・どれも、進行のコードが名前で探さない物なので、Unity のマップエディターでコピーして足したり、消したりできる
//   （扉・配電盤・端末など、進行のコードが名前で探す place は足したり消したりできない）。
// ・Debug版で配置ファイルを読み直すと、位置・向き・大きさは置き直す。足した物・消した物は、ステージを始め直すと反映される。
// 1面・2面・3面のどの配置ファイルでも使える。
// ============================================================================

#pragma once

#include "PropLayoutFile.h"

#include <string_view>
#include <vector>

class BatteryItem;
class CeilingLight;
class SceneLayoutBuilder;

namespace LayoutExtras
{
    // 名前の頭（配置ファイルとUnityのマップエディターで同じ物を使う）
    constexpr std::string_view LightPrefix = "ExtraLight_";
    constexpr std::string_view BatteryPrefix = "ExtraBattery_";

    // 作った物
    struct Spawned
    {
        std::vector<CeilingLight*> Lights;
        std::vector<BatteryItem*> Batteries;
    };

    // 配置ファイルの「何個でも置ける物」をすべて作り、読み直したときの置き直し方を bindings に足している
    Spawned Spawn(SceneLayoutBuilder& builder, const PropLayout& layout, PropLayoutFile::PlacementBindings& bindings);
}
