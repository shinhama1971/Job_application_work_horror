// ============================================================================
// ファイルの役割: 1面のHUD、目的表示、進行フィードバックの描画を管理します。
// ============================================================================

#include "StageScene.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

void StageScene::Draw(Camera* camera)
{
    (void)camera;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");

    if (player == nullptr)
    {
        return;
    }

    FuseBox* exitPowerPanel = game->GetObj<FuseBox>("ExitPowerPanel");
    const bool exitPowerActivated =
        exitPowerPanel != nullptr && exitPowerPanel->IsActivated();
    const bool exitPowerReady = m_PowerSequence.IsExitComplete();

    const int fuseCount = game->GetItemCount();
    std::string_view objectiveText;
    if (fuseCount <= 0)
    {
        objectiveText = "開始地点の近くでヒューズを探す";
    }
    else if (fuseCount == 1)
    {
        objectiveText = m_CorridorLoopCount < 1
            ? "中央のドアを開けて廊下の奥へ進む"
            : "左側の部屋でヒューズを探す";
    }
    else if (fuseCount == 2)
    {
        objectiveText = m_CorridorLoopCount < 2
            ? "もう一度廊下の奥まで進む"
            : "右側の部屋でヒューズを探す";
    }
    else
    {
        objectiveText = "左の部屋にある配電盤を調べる";
    }
    ExitTrigger* exitTrigger =
        game->GetObj<ExitTrigger>("ExitTrigger");
    if (exitTrigger != nullptr && exitTrigger->IsEscaping())
    {
        objectiveText = "ドアの先へ移動中";
    }
    else if (m_ChargerNoticeTimer > 0.0f)
    {
        objectiveText = m_FuseWatcherState == 1
            ? "充電音で影が現れた ライトを向ける"
            : "バッテリーを充電した";
    }
    else if (m_EvidenceNoticeTimer > 0.0f)
    {
        objectiveText = "残された記録を回収した 1 / 3";
    }
    else if (m_FuseNoticeTimer > 0.0f)
    {
        if (m_FuseWatcherState == 1)
            objectiveText = "影に懐中電灯を向ける";
        else if (fuseCount == 1) objectiveText = "ヒューズを1本入手";
        else if (fuseCount == 2) objectiveText = "ヒューズを2本入手";
        else objectiveText = "ヒューズを3本入手";
    }
    else if (m_FuseWatcherNoticeTimer > 0.0f)
    {
        objectiveText = m_FuseWatcherState == 1
            ? "影を正面から懐中電灯で照らす"
            : "影が光の中へ消えた";
    }
    else if (m_ExitOmenSequence.GetTimer() > 0.0f)
    {
        objectiveText = m_ExitOmenSequence.GetTimer() > 1.75f
            ? "何かが待っている"
            : "立ち止まらず進む";
    }
    else if (game->IsPowerRestored() &&
        m_PowerSequence.IsRestoreActive() &&
        m_PowerSequence.GetRestoreTimer() < 4.5f)
    {
        objectiveText = m_PowerSequence.GetRestoreTimer() < 1.55f
            ? "電力が復旧した"
            : "出口側の非常送電盤へ向かう";
    }
    else if (exitPowerActivated && !exitPowerReady)
    {
        objectiveText = "非常電源を送電中";
    }
    else if (m_ScareLightSequence.GetNoticeTimer() > 0.0f)
    {
        objectiveText = m_ScareLightSequence.GetNoticeTimer() > 2.65f
            ? "何かに見られている"
            : "点灯した照明をたどる";
    }
    else if (m_LoopNoticeTimer > 0.0f)
    {
        if (m_CorridorLoopCount == 1)
        {
            objectiveText = "廊下の様子が変わった";
        }
        else if (m_CorridorLoopCount == 2)
        {
            objectiveText = "そのまま歩き続ける";
        }
        else
        {
            objectiveText = "後ろを振り返らない";
        }
    }
    else if (m_ProgressHintTimer >= 35.0f)
    {
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            objectiveText = "ヒント 右奥の赤い送電盤を調べる";
        }
        else if (game->IsPowerRestored())
        {
            objectiveText = "ヒント 右奥の緑色の出口へ向かう";
        }
        else if (fuseCount <= 0)
        {
            objectiveText = "ヒント 最初の壁付近を探す";
        }
        else if (fuseCount == 1 && m_CorridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアを開け廊下の奥へ進む";
        }
        else if (fuseCount == 1)
        {
            objectiveText = "ヒント 左奥の部屋を探す";
        }
        else if (fuseCount == 2 && m_CorridorLoopCount < 2)
        {
            objectiveText = "ヒント もう一度廊下の奥まで進む";
        }
        else if (fuseCount == 2)
        {
            objectiveText = "ヒント 右奥の部屋を探す";
        }
        else
        {
            objectiveText = "ヒント 左の部屋の配電盤を調べる";
        }
    }
    else if (m_ProgressHintTimer >= 18.0f)
    {
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            objectiveText = "ヒント 出口手前の送電盤へ向かう";
        }
        else if (game->IsPowerRestored())
        {
            objectiveText = "ヒント 緑色の出口灯をたどる";
        }
        else if (fuseCount <= 0)
        {
            objectiveText = "ヒント 開始地点の周囲を探す";
        }
        else if (fuseCount == 1 && m_CorridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアが進行ルート";
        }
        else if (fuseCount == 1)
        {
            objectiveText = "ヒント 左側の部屋を確認する";
        }
        else if (fuseCount == 2 && m_CorridorLoopCount < 2)
        {
            objectiveText = "ヒント 長い廊下をもう一度進む";
        }
        else if (fuseCount == 2)
        {
            objectiveText = "ヒント 右側の部屋を確認する";
        }
        else
        {
            objectiveText = "ヒント 配電盤へ戻る";
        }
    }
    else if (game->IsPowerRestored() && !exitPowerActivated)
    {
        objectiveText = "出口手前の非常送電盤を操作する";
    }
    else if (game->IsPowerRestored())
    {
        Door* stageExitDoor = game->GetObj<Door>("Stage1ExitDoor");
        objectiveText = stageExitDoor != nullptr && stageExitDoor->IsOpen()
            ? "開いた出口ドアを通り抜ける"
            : "右奥の出口ドアを開ける";
    }

    m_Hud.Draw(
        *player,
        fuseCount,
        m_InteractionSystem.GetPrompt(),
        objectiveText);

    if (m_StageVisualTimer >= 4.20f &&
        (exitTrigger == nullptr || !exitTrigger->IsEscaping()))
    {
        Vector3 guideTarget(0.0f, -99.0f, 315.0f);
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            guideTarget = Vector3(145.0f, -90.0f, 270.0f);
        }
        else if (game->IsPowerRestored())
        {
            guideTarget = Vector3(202.0f, -74.0f, 307.5f);
        }
        else if (fuseCount <= 0)
        {
            guideTarget = Vector3(0.0f, -95.0f, -155.0f);
        }
        else if (fuseCount == 1 && m_CorridorLoopCount >= 1)
        {
            guideTarget = Vector3(-150.0f, -95.0f, -140.0f);
        }
        else if (fuseCount == 2 && m_CorridorLoopCount >= 2)
        {
            guideTarget = Vector3(150.0f, -95.0f, -140.0f);
        }
        else if (fuseCount >= 3)
        {
            guideTarget = Vector3(-180.0f, -90.0f, 35.0f);
        }
        m_Hud.DrawObjectiveGuide(
            *camera,
            player->GetPosition(),
            guideTarget);
    }

    if (m_StageVisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_StageVisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_StageVisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "1階", "ヒューズを集めて電力を復旧する", m_StageVisualTimer);
    }

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->GetPauseSettingIndex(),
            1,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}
