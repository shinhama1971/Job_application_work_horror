// ============================================================================
// ファイルの役割: 2面の目的、ヒント、異変フィードバックの描画を管理します。
// ============================================================================

#include "Stage2Scene.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"


void Stage2Scene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    if (player == nullptr)
    {
        return;
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    Door* finalDoor = game->GetObj<Door>("Stage2Door");
    FuseBox* confirmationPanel =
        game->GetObj<FuseBox>("Stage2ConfirmationPanel");
    const bool confirmationPending =
        (m_LoopCount == 1 || m_LoopCount == 2) &&
        confirmationPanel != nullptr &&
        !confirmationPanel->IsActivated() &&
        ((m_LoopCount == 1 && m_FalseDoorMoved) ||
         (m_LoopCount == 2 && m_ClockObservedThisLoop));
    constexpr std::string_view closedLoopObjectives[] =
    {
        "奥のドアを開ける 1回目",
        "奥のドアを開ける 2回目",
        "奥のドアを開ける 3回目"
    };
    constexpr std::string_view openLoopObjectives[] =
    {
        "開いたドアを通り抜ける 1回目",
        "開いたドアを通り抜ける 2回目",
        "開いたドアを通り抜ける 3回目"
    };

    std::string_view objective = "廊下の奥にある出口へ向かう";
    if (m_LoopCount < 3)
    {
        const size_t loopIndex = static_cast<size_t>(m_LoopCount);
        objective = finalDoor != nullptr && finalDoor->IsOpen()
            ? openLoopObjectives[loopIndex]
            : closedLoopObjectives[loopIndex];
    }
    if (m_LoopCount == 1 && !m_FalseDoorMoved)
    {
        objective = m_FalseDoorObserved
            ? "偽物のドアから視線を外す"
            : "懐中電灯で左の偽物のドアを照らす";
    }
    else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
    {
        objective = "ライトを消して左の時計を見る";
    }
    else if (confirmationPending)
    {
        objective = "奥の異常確認スイッチを押す";
    }
    else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
    {
        if (m_SignalStep == 0)
        {
            objective = m_PuzzleMistakeCount >= 2
                ? "再試行補助中 奥の青い信号盤からやり直す"
                : "信号復旧 まず奥の青い信号盤を操作する";
        }
        else if (m_SignalStep == 1)
        {
            objective = "信号復旧 黄色へ戻る 背後の影はライトで追い払う";
        }
        else
        {
            objective = "信号復旧 赤へ戻る 背後の影はライトで追い払う";
        }
    }
    if (exit != nullptr && exit->IsEscaping())
    {
        objective = "脱出中";
    }
    else if (m_GazeNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_CaughtTimer >= 0.0f)
    {
        objective = "捕まった チェックポイントへ戻る";
    }
    else if (m_QuietRecovery.tooClose)
    {
        objective = "影が近すぎる 距離を取りライトを消して止まる";
    }
    else if (m_QuietRecovery.successNotice > 0.0f)
    {
        objective = "気配が遠のいた 静かに探索を続ける";
    }
    else if (m_QuietRecovery.progress > 0.0f)
    {
        objective = "息を潜めている 消灯したまま動かない";
    }
    else if (m_ChargerNoticeTimer > 0.0f)
    {
        objective = "充電器の音で廊下が反応した";
    }
    else if (m_EvidenceNoticeTimer > 0.0f)
    {
        objective = "残された記録を回収した";
    }
    else if (m_SignalNoticeTimer > 0.0f)
    {
        if (m_SignalPuzzleComplete)
        {
            objective = "信号復旧完了 廊下の中央へ進む";
        }
        else if (m_PuzzleFeedbackType == 3)
        {
            objective = "順番が違う 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 4)
        {
            objective = "走る音で同期が切れた 歩いて青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (m_SignalStep == 1)
        {
            objective = "青を確認 黄色へ戻る 背後に注意";
        }
        else
        {
            objective = "黄色を確認 赤へ戻る 背後に注意";
        }
    }
    else if (m_NoiseStalkerNoticeTimer > 0.0f)
    {
        ShadowMan* noiseShadow =
            game->GetObj<ShadowMan>("Stage2NoiseShadow");
        objective = noiseShadow != nullptr && noiseShadow->IsActive()
            ? "水音を聞いた影が来る 振り返ってライトを当てる"
            : "影を追い払った 静かに進む";
    }
    else if (m_WetStepNoticeTimer > 0.0f)
    {
        objective = "水音が廊下に響いた 水たまりは歩いて渡る";
    }
    else if (m_NoiseWarningTimer > 0.0f)
    {
        objective = "足音が響いている 歩いて静める";
    }
    else if (m_PuzzleFeedbackTimer > 0.0f)
    {
        if (m_PuzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (m_PuzzleMistakeCount >= 3)
        {
            objective = "照明が消えた 正しい方法を試す";
        }
        else
        {
            if (m_PuzzleFeedbackType == 1)
            {
                objective = "光が必要だ 懐中電灯でドアを照らす";
            }
            else if (m_PuzzleFeedbackType == 2)
            {
                objective = "光が邪魔だ 懐中電灯を消して時計を見る";
            }
            else if (m_PuzzleFeedbackType == 3)
            {
                objective = "信号の順番が違う 青からやり直す";
            }
            else
            {
                objective = "走る音で同期が切れた 歩いて青からやり直す";
            }
        }
    }
    else if (m_PursuitGazePenaltyTimer > 0.0f)
    {
        objective = "それを見てはいけない";
    }
    else if (m_FinalSequenceTimer >= 0.0f && !m_FinalDoorReady)
    {
        objective = Input::IsControllerConnected()
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (m_FinalPursuitTimer > 0.0f)
    {
        objective = Input::IsControllerConnected()
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (m_FinalDoorReady)
    {
        objective = finalDoor != nullptr && !finalDoor->IsOpen()
            ? "奥のドアを開ける"
            : "開いた出口を通り抜ける";
    }
    else if (m_ScratchNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_FalseDoorNoticeTimer > 0.0f)
    {
        objective = "異常を確認した 奥のスイッチへ進む";
    }
    else if (m_ClockNoticeTimer > 0.0f)
    {
        objective = m_LoopCount == 2
            ? "逆回転を確認した 奥のスイッチへ進む"
            : "時計の時刻が変わった";
    }
    else if (m_PortraitNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_NoticeTimer > 0.0f)
    {
        if (m_LoopCount == 0) objective = "1回目 奥のドアを開ける";
        else if (m_LoopCount == 1) objective = m_FalseDoorMoved
            ? (m_ConfirmationHandledThisLoop
                ? "鍵が開いた 奥のドアへ進む"
                : "奥の異常確認スイッチを押す")
            : "2回目 廊下の変化を探す";
        else if (m_LoopCount == 2) objective = m_ClockObservedThisLoop
            ? (m_ConfirmationHandledThisLoop
                ? "鍵が開いた 後ろを見ずに進む"
                : "奥の異常確認スイッチを押す")
            : "3回目 左の時計を調べる";
        else objective = m_SignalPuzzleComplete
            ? "信号が復旧した 廊下の中央へ進む"
            : "奥の青い信号盤から復旧する";
    }
    else if (m_ProgressHintTimer >= 30.0f)
    {
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            objective = "ヒント ライトで偽物のドアを照らして視線を外す";
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を正面から見る";
        }
        else if (confirmationPending)
        {
            objective = "ヒント 奥の壁にある赤い確認スイッチを押す";
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            objective = m_SignalStep == 0
                ? "ヒント 青は廊下の奥の右壁"
                : m_SignalStep == 1
                    ? "ヒント 黄色は中央左 影は振り返ってライトを当てる"
                    : "ヒント 赤は入口右 影は振り返ってライトを当てる";
        }
        else if (m_FinalDoorReady)
        {
            objective = finalDoor != nullptr && finalDoor->IsOpen()
                ? "ヒント 開いた出口を通り抜ける"
                : "ヒント 今すぐ奥のドアを開ける";
        }
        else if (m_FinalSequenceArmed)
        {
            objective = "ヒント 廊下の中央より先へ進む";
        }
        else if (finalDoor != nullptr && finalDoor->IsOpen())
        {
            objective = "ヒント 開いた奥のドアを通り抜ける";
        }
        else
        {
            objective = "ヒント まっすぐ進み奥のドアを開ける";
        }
    }
    else if (m_ProgressHintTimer >= 15.0f)
    {
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            objective = m_FalseDoorObserved
                ? "ヒント 偽物のドアから視線を外す"
                : "ヒント ライトを点け前方左側の壁を探す";
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を見る";
        }
        else if (confirmationPending)
        {
            objective = "ヒント ドア手前の確認スイッチへ進む";
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            objective = "ヒント 発光している信号盤を 青 黄 赤 の順で操作する";
        }
        else if (m_FinalDoorReady)
        {
            objective = "ヒント 廊下の奥にある出口が開いている";
        }
        else if (m_FinalSequenceArmed)
        {
            objective = "ヒント 廊下をそのまま歩き続ける";
        }
        else if (finalDoor != nullptr && finalDoor->IsOpen())
        {
            objective = "ヒント 奥のドアを開けると次へ進む";
        }
        else
        {
            objective = "ヒント 奥のドアを通り抜ける";
        }
    }
    else if (m_FinalSequenceArmed)
    {
        objective = "廊下の奥にある出口へ向かう";
    }

    m_Hud.Draw(*player, -1, m_InteractionSystem.GetPrompt(), objective);
    float threatRate = 0.0f;
    threatRate = (std::max)(threatRate, m_NoiseThreat * 0.78f);
    if (m_LoopCount == 1 || m_LoopCount == 2)
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleMistakeCount) / 3.0f;
        threatRate = (std::max)(threatRate, observationDanger * 0.72f);
    }
    if (m_FinalPursuitTimer > 0.0f)
    {
        ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = 1.0f - (std::clamp)(
                (distance - 18.0f) / 92.0f, 0.0f, 1.0f);
        }
    }
    ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player->GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    if (m_VisualTimer >= 4.20f &&
        m_CaughtTimer < 0.0f &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        m_Hud.DrawStage2Status(
            m_LoopCount, threatRate, m_FinalDoorReady,
            m_SignalStep,
            m_LoopCount >= 3 && !m_SignalPuzzleComplete);
        if (m_LoopCount < 3 && m_ObservedScareTimer < 0.0f &&
            m_FinalPursuitTimer <= 0.0f && m_FinalSequenceTimer < 0.0f &&
            m_LoopTransitionTimer < 0.0f &&
            (m_NoiseThreat > 0.25f || m_QuietRecovery.progress > 0.0f ||
                m_QuietRecovery.successNotice > 0.0f || m_QuietRecovery.cooldown > 0.0f))
        {
            m_Hud.DrawQuietRecovery(
                m_QuietRecovery.progress / QuietRecovery::RequiredSeconds,
                m_QuietRecovery.cooldown, m_QuietRecovery.successNotice > 0.0f,
                m_QuietRecovery.tooClose);
        }
    }
    if (m_VisualTimer >= 4.20f &&
        m_CaughtTimer < 0.0f &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        Vector3 guideTarget = finalDoor != nullptr
            ? finalDoor->GetPosition()
            : Vector3(0.0f, -74.0f, 140.0f);
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            guideTarget = Vector3(-38.3f, -72.0f, 70.0f);
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            guideTarget = Vector3(-38.0f, -70.0f, -25.0f);
        }
        else if (confirmationPending)
        {
            guideTarget = Vector3(35.5f, -90.0f, 112.0f);
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            if (m_SignalStep == 0)
            {
                guideTarget = Vector3(35.5f, -90.0f, 82.0f);
            }
            else if (m_SignalStep == 1)
            {
                guideTarget = Vector3(-35.5f, -90.0f, 18.0f);
            }
            else
            {
                guideTarget = Vector3(35.5f, -90.0f, -108.0f);
            }
        }
        m_Hud.DrawObjectiveGuide(
            *camera, player->GetPosition(), guideTarget);
    }
    if (m_VisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_VisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_VisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "2階",
            "廊下の変化を見逃さない",
            m_VisualTimer);
    }
    else if (m_LoopTransitionTimer >= 0.0f)
    {
        constexpr std::string_view cycleTitles[] =
        {
            "1回目を通過",
            "2回目を通過",
            "3回目を通過"
        };
        constexpr std::string_view cycleSubtitles[] =
        {
            "同じ廊下へ戻ってきた",
            "何かが移動している",
            "信号復旧を開始する"
        };
        const size_t cycleIndex = static_cast<size_t>((std::clamp)(
            m_LoopCount - 1, 0, 2));
        m_Hud.DrawChapterCard(
            cycleTitles[cycleIndex],
            cycleSubtitles[cycleIndex],
            m_LoopTransitionTimer);
    }
    if (m_LoopBlinkTimer > 0.0f)
    {
        const float blinkRate =
            (std::clamp)(m_LoopBlinkTimer / 0.28f, 0.0f, 1.0f);
        m_Hud.DrawBlink(blinkRate * blinkRate * 0.90f);
    }

    if (m_CaughtTimer >= 0.0f)
    {
        const float caughtFade = (std::clamp)(
            m_CaughtTimer / 0.34f, 0.0f, 1.0f);
        m_Hud.DrawBlink(caughtFade * 0.96f);
    }

    if (exit != nullptr && exit->IsEscaping())
    {
        const float fadeRate = (std::clamp)(
            (exit->GetEscapeProgress() - 0.36f) / 0.64f,
            0.0f, 1.0f);
        const float smoothFade =
            fadeRate * fadeRate * (3.0f - 2.0f * fadeRate);
        m_Hud.DrawBlink(smoothFade * 0.90f);
    }

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->GetPauseSettingIndex(),
            2,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}
