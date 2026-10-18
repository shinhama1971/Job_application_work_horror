// ============================================================================
// ファイルの役割: 2面の1周目・2周目に、どの異変を「見つけないと扉が開かない異変」にするかを決めている。
// 主な技術: プレイごとのランダムな組み合わせ、入力や描画に依存しない状態クラス
//
// 偽の扉・時計・肖像画・壁の向こうのノック・血の手形・鏡の6つから2つを選んで並べている。毎回違う組み合わせになるため、
// 答えを覚えて進むのではなく、周回ごとに廊下を観察して変化を探す遊びにしている。
// 3周目以降（信号盤パズル）は固定。
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// 2面の異変の種類
enum class Stage2Anomaly
{
    // 異変なし
    None,
    FalseDoor,  // 偽の扉: ライトで照らしてから目を離すと、反対側の壁へ移っている
    Clock,      // 時計: ライトを消して見ると、針が逆回りしている
    Portrait,   // 肖像画: ライトで照らしたまま見つめ続けると、目が開く
    Knocking,   // ノック: 壁の向こうから叩く音がする。音を頼りに出どころの壁を探し、耳を澄ます
    Handprint,  // 血の手形: 照らすと奥の反対側の壁に次の手形が現れる。手形を4つ目までたどる
    Mirror      // 鏡: 鏡をのぞくと、すぐ後ろに人影が映っている。振り返ると誰もいない
};

class Stage2AnomalyPlan final
{
public:
    // 異変を探す周回の数（1周目と2周目）。
    static constexpr int PlannedLoopCount = 2;

    // 6つの候補を混ぜて、先頭の2つを1周目・2周目の異変にしている
    template<typename RandomEngine>
    void Randomize(RandomEngine& random)
    {
        std::array<Stage2Anomaly, 6> candidates =
        {
            Stage2Anomaly::FalseDoor, Stage2Anomaly::Clock,
            Stage2Anomaly::Portrait, Stage2Anomaly::Knocking,
            Stage2Anomaly::Handprint, Stage2Anomaly::Mirror
        };
        std::shuffle(candidates.begin(), candidates.end(), random);
        m_Loops = { candidates[0], candidates[1] };
    }

    // 決まった周回の異変を、指定した異変に置き換えている（デバッグ画面と自動撮影で、試したい異変を出すため）
    void ForceLoop(int loopCount, Stage2Anomaly anomaly) noexcept
    {
        if (loopCount >= 1 && loopCount <= PlannedLoopCount)
        {
            m_Loops[static_cast<std::size_t>(loopCount - 1)] = anomaly;
        }
    }

    // その周回で見つける必要がある異変を返している。0周目と3周目以降はNone。
    Stage2Anomaly GetRequired(int loopCount) const noexcept
    {
        if (loopCount < 1 || loopCount > PlannedLoopCount)
        {
            return Stage2Anomaly::None;
        }
        return m_Loops[static_cast<std::size_t>(loopCount - 1)];
    }

    // その周回で見つける必要があるのが、指定した異変かどうか
    bool IsRequired(int loopCount, Stage2Anomaly anomaly) const noexcept
    {
        return anomaly != Stage2Anomaly::None && GetRequired(loopCount) == anomaly;
    }

    // リザルト画面に出す名前を返している。
    static const char* GetDisplayName(Stage2Anomaly anomaly) noexcept
    {
        switch (anomaly)
        {
        case Stage2Anomaly::FalseDoor: return "偽ドア";
        case Stage2Anomaly::Clock: return "時計";
        case Stage2Anomaly::Portrait: return "肖像画";
        case Stage2Anomaly::Knocking: return "壁のノック";
        case Stage2Anomaly::Handprint: return "血の手形";
        case Stage2Anomaly::Mirror: return "鏡";
        case Stage2Anomaly::None: break;
        }
        return "なし";
    }

    // デバッグ表示用の名前を返している。
    static const char* GetName(Stage2Anomaly anomaly) noexcept
    {
        switch (anomaly)
        {
        case Stage2Anomaly::FalseDoor: return "FalseDoor";
        case Stage2Anomaly::Clock: return "Clock";
        case Stage2Anomaly::Portrait: return "Portrait";
        case Stage2Anomaly::Knocking: return "Knocking";
        case Stage2Anomaly::Handprint: return "Handprint";
        case Stage2Anomaly::Mirror: return "Mirror";
        case Stage2Anomaly::None: break;
        }
        return "None";
    }

private:
    // 乱数で決める前でも遊べるよう、前と同じ順番（偽の扉→時計）を初期値にしている。
    std::array<Stage2Anomaly, PlannedLoopCount> m_Loops =
    {
        Stage2Anomaly::FalseDoor, Stage2Anomaly::Clock
    };
};
