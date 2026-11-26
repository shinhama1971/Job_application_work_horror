// ============================================================================
// ファイルの役割: 1面で、何も起きていない静かな時間に、ときどき起こる小さな恐怖の出来事の進行。
// 主な技術: 落ち着いた時間を数えて次の出来事を選ぶ予定表、出来事ごとの有限状態機械、
//           出来事の後に心拍と視界の狭まりを強める「怯え」の度合い
//
// 台本の大きな演出（濁流・天井裏の足音・音の部屋など）の合間は、何も起きない時間が長く、慣れて怖くなくなる。
// そこで、ほかの演出が起きていない落ち着いた時間が続くと、次の4つのうちの1つを順番に起こしている。
// ・扉の向こうのノック：通り過ぎた後ろの閉じた扉を、内側から誰かが叩く（P.T.）
// ・懐中電灯が「見られて」消える：懐中電灯が勝手にちらついて消え、点き直すと目の前に人影が立っている（Outlast）
// ・行く手の照明が消えていく：前にある天井の照明が、奥から手前へ1つずつ消えていき、しばらくして戻る
// ・画面が一瞬だけ別の場所：まばたきのように一瞬だけ、血まみれの同じ場所が映る（サイレントヒル）
// どの出来事の後も、しばらく「怯え」（GetFright）が残り、StageScene が心拍・息づかい・視界の狭まりを強めている（Amnesia）。
// 描画・音・照明に依存せず、「いつ・どこで・何をするか」の合図（Cue）だけを返している。照明の操作と音は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <vector>

class Stage1FearEvents final
{
public:
    // 起こす出来事の種類（この順番で回している）
    enum class Kind
    {
        Knock,
        Flashlight,
        LightsOut,
        Otherworld,
        Count
    };

    // StageScene へ渡す合図の種類：ノックを1回鳴らす・懐中電灯をちらつかせる・消す・点け直して人影を出す・人影を消す・
    // 照明を消す・照明を点け直す・別の場所が映った瞬間
    enum class CueType
    {
        Knock,
        FlashlightSputter,
        FlashlightOut,
        FlashlightBack,
        FigureHide,
        LightOff,
        LightOn,
        OtherworldFlash
    };

    struct Cue
    {
        CueType Type = CueType::Knock;
        DirectX::SimpleMath::Vector3 Position;
        int LightNumber = 0;
        float Pitch = 1.0f;
        float Volume = 1.0f;
        float Seconds = 0.0f;
    };

    // 1フレームの入力。どの出来事を起こせるかは、StageScene が今の場所と状態から調べて渡している
    struct Input
    {
        float DeltaTime = 0.0f;
        // ほかの演出が起きておらず、出来事を起こしてよい落ち着いた時間か
        bool Calm = false;
        // ノックを鳴らせる閉じた扉が後ろにあるか、その扉の位置
        bool KnockAvailable = false;
        DirectX::SimpleMath::Vector3 KnockPosition;
        // 懐中電灯が点いているか（点いていなければ、懐中電灯の出来事は起こさない）
        bool FlashlightOn = false;
        // 前にあって点いている天井の照明の番号（CeilingLightAt の番号。奥から手前の順）
        std::vector<int> LightsAhead;
    };

    // 最初の状態に戻している（次の出来事までの時間も最初から数え直す）
    void Reset();
    // 1フレーム分進め、このフレームの合図を cues へ足している
    void Update(const Input& input, std::vector<Cue>& cues);
    // 落ち着いた時間を待たずに、その出来事を今すぐ起こしている（起こせたら true）。撮影での確認用
    bool Start(Kind kind, const Input& input, std::vector<Cue>& cues);

    // 出来事の途中か（この間は、ほかの物音を鳴らさない）
    bool IsActive() const { return m_Active != Kind::Count; }
    // 出来事の後に残る怯えの度合い（0〜1）。心拍・息づかい・視界の狭まりを強めるのに使う
    float GetFright() const { return m_Fright; }
    // 血まみれの別の場所が映っている強さ（0〜1）と、映るたびに変わる乱数の種
    float GetOtherworld() const;
    float GetOtherworldSeed() const { return m_OtherworldSeed; }
    // 消している照明の番号（途中で面を離れたときに点け直すため）
    const std::vector<int>& GetLightsOff() const { return m_LightsOff; }

private:
    bool CanStart(Kind kind, const Input& input) const;
    void UpdateKnock(std::vector<Cue>& cues);
    void UpdateFlashlight(std::vector<Cue>& cues);
    void UpdateLightsOut(std::vector<Cue>& cues);
    void UpdateOtherworld(std::vector<Cue>& cues);
    void Finish();
    void Frighten(float amount);

    // 今起きている出来事（Count なら何も起きていない）と、その中での経過秒数・何段目か
    Kind m_Active = Kind::Count;
    float m_Elapsed = 0.0f;
    int m_Step = 0;
    // 次に起こす出来事と、次の出来事までに残っている落ち着いた時間（秒）、起こした数
    Kind m_Next = Kind::Knock;
    float m_Wait = 0.0f;
    int m_EventCount = 0;
    float m_Fright = 0.0f;
    // ノックの扉の位置、消す照明（奥から手前）と消した照明、別の場所の乱数の種
    DirectX::SimpleMath::Vector3 m_KnockPosition;
    std::vector<int> m_Lights;
    std::vector<int> m_LightsOff;
    float m_OtherworldSeed = 0.0f;
};
