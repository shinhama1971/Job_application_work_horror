#pragma once

#include "Game.h"
#include <iterator>
#include <string_view>

namespace TitleStageChoices
{
    struct Choice
    {
        SceneName Scene;
        std::string_view Name;
        std::string_view Description;
    };

    inline constexpr Choice Entries[] =
    {
        { SceneName::Forest, "1面  森から研究所へ", "雨の森を歩いて研究所に入る  クリアの記録が残る" },
        { SceneName::Stage2, "2面  ループする廊下", "2面から遊ぶ  クリアしても記録には残らない" },
        { SceneName::Intro, "1面  森を飛ばして始める", "受信記録を見て、研究所の中から始める" },
        // 試作中の3面は、提出版ではタイトルから選べないようにしている（開発用の --stage3 で始められる）
    };
    inline constexpr int Count = static_cast<int>(std::size(Entries));
}
