// ============================================================================
// ファイルの役割: 物語の文章（研究棟からの受信記録と目的、1面のボイスレコーダーの録音と、1面でまた届く無線）。
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

    // 1面に落ちているボイスレコーダーの録音（Stage1Transmissions）。拾った順に関係なく分かるよう、1つずつで話が通じる内容にしている。
    // 4つ目は、森の無線と同じ調査員の声。これを聞くと、森で聞いた無線も、1面で聞こえる無線も、もう本人ではないと分かる。
    inline constexpr int RecordingCount = 4;
    inline constexpr int RecordingLineCount = 3;
    struct Recording
    {
        const char* Header;
        const char* Lines[RecordingLineCount];
    };
    inline constexpr Recording Recordings[RecordingCount] =
    {
        {
            "録音 1/4　——　研究員　三浦",
            {
                "「実験記録、十月二日。アンテナが、どの局も使っていない帯域で声を拾った。」",
                "「……私たちの声だった。昨日この部屋で話した言葉が、そのまま返ってくる。」",
                "「所長は、こちらからも送り返してみろと言った。」",
            },
        },
        {
            "録音 2/4　——　研究員　三浦",
            {
                "「十月五日。送り返した夜に停電した。それから、廊下が終わらない。」",
                "「暗がりで何かが動く。ライトを当てると止まる。……明かりを、絶対に消すな。」",
                "「壁に書いておいた。次に来た人が読めるように。」",
            },
        },
        {
            "録音 3/4　——　警備員　野口",
            {
                "「警備の野口だ。無線で所長の声がした。書類保管室へ来い、と。」",
                "「行った二人は戻らなかった。……所長は、三日前に死んでいる。」",
                "「あれは聞いた声を覚えて、真似る。無線の声を、信じるな。」",
            },
        },
        {
            "録音 4/4　——　第3研究棟　調査員",
            {
                "「……第3研究棟の調査員だ。これを聞いている人へ。」",
                "「この先、無線で俺の声がしても、それはもう俺じゃない。」",
                "「俺は、この部屋で——」",
            },
        },
    };

    // 1面の途中で、また届く無線（Stage1Transmissions）。森と同じ調査員の声で、書類保管室へ誘い、
    // 照らすと止まる影の前で「ライトを消せ」と言う（壁の文字「あかりを けすな」と逆のことを言う、嘘の指示）。
    inline constexpr const char* FalseRadioHeader = "受信　——　第3研究棟";
    // 1本目のヒューズを拾った後
    inline constexpr const char* FalseRadioCallLines[2] =
    {
        "「……聞こえるか。こちら調査員。まだ中にいる。無事だ。」",
        "「書類保管室の奥の机に、予備のヒューズを置いてきた。それを使ってくれ。」",
    };
    // 書類保管室に入ったとき
    inline constexpr const char* FalseRadioRoomLine = "「……そうだ、その部屋だ。奥の机まで行け。」";
    // 影をしばらく照らし、止まっている様子を見たとき
    inline constexpr const char* FalseRadioLightLine = "「……ライトを消せ。あれは、光に寄ってくる。」";
    // 調査員の録音（4つ目）を聞き終えた直後
    inline constexpr const char* FalseRadioRevealLine = "「……聞いたのか。」";

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
