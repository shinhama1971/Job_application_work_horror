// ============================================================================
// ファイルの役割: 1面の前の導入。研究棟からの途切れ途切れの「受信記録」で状況を伝え、最後に目的と操作を見せている。
// 主な技術: 時間で進む演出（1行ずつタイプされる文字、行の間の電波の乱れ、「通信途絶」で無線の雑音が途切れる）、
//           後ろに映す3Dの森（ForestBackdrop。研究棟へゆっくり近づくカメラ）
//
// ・初めて遊んだ人から「何をするゲームか分かりにくい」という意見があり、始まる前に、なぜここにいるのか・何が起きたのか・
//   何をすれば終わりかを伝えるために作った。
// ・Enter（A・START）で、受信記録の途中なら目的の画面へ飛ばし、目的の画面なら1面を始める。
// ・タイトルの「1面」は、森を歩いて研究所に入る（ForestScene。受信記録は歩きながら無線で聞く）。
//   玄関を調べて入ると、ここへ来て目的の画面だけを見せる（SetStartAtMission）。
//   タイトルの「森を飛ばして1面から」のときは、受信記録から全部見せる。
// ============================================================================

#pragma once

#include "Scene.h"
#include "Hud.h"
#include "ForestBackdrop.h"

class IntroScene final : public Scene
{
public:
    IntroScene();
    ~IntroScene() override;

    // 次に作る導入を、目的の画面から始める（森で受信記録を聞き終え、玄関から入ったとき）。作った時点で元に戻る
    static void SetStartAtMission(bool startAtMission) { s_StartAtMission = startAtMission; }

    void Update() override;
    void Draw(Camera* camera) override;

private:
    // 受信記録の時間の流れ（秒）
    static constexpr float HeaderTime = 0.6f;
    static constexpr float LineStartTimes[4] = { 2.2f, 8.4f, 13.8f, 18.6f };
    static constexpr float TypingCharactersPerSecond = 14.0f;
    static constexpr float SignalLostTime = 21.4f;
    static constexpr float MissionTime = 24.0f;

    // 目的の画面へ移っている（受信記録を飛ばしたときも）
    void ShowMission();
    // 今の電波の乱れの強さ（0〜1）。行が始まる瞬間と「通信途絶」で強くしている
    float GetNoise() const;

    Hud m_Hud;
    ForestBackdrop m_Backdrop;
    float m_Time = 0.0f;
    // 目的の画面を出しているか、出してからの秒数
    bool m_ShowingMission = false;
    float m_MissionTimer = 0.0f;
    // 無線の雑音を鳴らしているか
    bool m_StaticPlaying = false;
    // 目的の画面から始めるか（SetStartAtMission）
    static bool s_StartAtMission;
};
