// ============================================================================
// ファイルの役割: 1面の前の物語の文章（研究棟からの受信記録と、目的）。
// 森を歩きながら聞く無線（ForestRadio）と、森を飛ばして始めたときの導入（IntroScene）で同じ文章を使っている。
// あわせて、文章を1文字ずつタイプして出すための、UTF-8 の先頭から何文字かを取り出す処理を置いている。
// ============================================================================

#pragma once

#include <string>

namespace StoryText
{
    // 受信記録の見出しと、途切れ途切れに届く3行
    inline constexpr const char* TransmissionHeader = "受信記録　——　第3研究棟";
    inline constexpr const char* TransmissionLines[3] =
    {
        "「……こちら第3研究棟を探索中。停電した。外との通信が出来ない。聞こえるならだれか応答をしてくれ。」",
        "「……廊下の様子が、おかしい。どれだけ進んでも同じ場所に戻ってくる」",
        "「……誰か、聞こえるか。廊下で、何かが動いている」",
    };
    inline constexpr const char* SignalLost = "——　通信途絶　——";
    // 目的の画面
    inline constexpr const char* MissionLead = "あなたは、連絡の途絶えた研究棟の調査に来た。";
    inline constexpr const char* MissionGoal = "目的：ヒューズを3本集めて配電盤に戻し、電力を復旧して脱出する";

    // UTF-8 の文字列の、先頭から count 文字（バイトではなく文字の数）を返している（1文字ずつタイプされる表示）
    inline std::string Utf8Prefix(const char* text, int count)
    {
        std::string result;
        int characters = 0;
        for (const char* p = text; *p != '\0'; ++p)
        {
            const unsigned char byte = static_cast<unsigned char>(*p);
            // 文字の先頭のバイト（続きのバイト 10xxxxxx 以外）で数えている
            if ((byte & 0xC0) != 0x80)
            {
                if (characters >= count)
                {
                    break;
                }
                ++characters;
            }
            result.push_back(*p);
        }
        return result;
    }

    // UTF-8 の文字列の文字の数
    inline int Utf8Length(const char* text)
    {
        int characters = 0;
        for (const char* p = text; *p != '\0'; ++p)
        {
            if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80)
            {
                ++characters;
            }
        }
        return characters;
    }
}
