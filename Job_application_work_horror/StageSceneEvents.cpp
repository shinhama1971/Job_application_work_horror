// ============================================================================
// ファイルの役割: 1面の入口演出、廊下ループ、電力復旧、出口イベントを管理します。
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

void StageScene::UpdateEntranceThresholdEvent(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
    Core::Game* game = Core::Game::GetInstance();

    if (!m_EntranceEventTriggered)
    {
        const Vector3 position = player.GetPosition();
        const bool crossedDoorThreshold =
            !game->IsPowerRestored() &&
            position.z > 62.0f &&
            std::abs(position.x) < 58.0f;
        if (!crossedDoorThreshold)
        {
            return;
        }

        // A physical light reaction replaces the old forced-camera cutaway.
        // Control remains with the player, so noticing the event feels earned.
        m_EntranceEventTriggered = true;
        m_EntranceEventTimer = 0.0f;
        m_EntranceEventPhase = 0;

        CeilingLight* lightBehind =
            game->GetObj<CeilingLight>("CeilingLight4");
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.38f, 0.52f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.075f, 0.14f);
        Input::SetVibration(3, 0.07f);
        return;
    }

    if (m_EntranceEventTimer < 0.0f)
    {
        return;
    }
    if (game->IsPowerRestored())
    {
        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = -1;
        return;
    }

    m_EntranceEventTimer += deltaTime;
    if (m_EntranceEventPhase == 0 && m_EntranceEventTimer >= 0.38f)
    {
        CeilingLight* lightAhead =
            game->GetObj<CeilingLight>("CeilingLight5");
        if (lightAhead != nullptr)
        {
            lightAhead->TriggerEventFlicker(0.62f, 0.72f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.10f, 0.18f);
        Input::SetVibration(4, 0.09f);
        m_EntranceEventPhase = 1;
    }
    else if (m_EntranceEventPhase == 1 &&
        m_EntranceEventTimer >= 1.12f)
    {
        CeilingLight* lightBehind =
            game->GetObj<CeilingLight>("CeilingLight4");
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.20f, 0.28f);
        }

        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = 2;
    }
}

void StageScene::UpdateCorridorLoop(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
    if (m_LoopCooldown > 0.0f)
    {
        m_LoopCooldown -= deltaTime;
    }
    if (m_LoopNoticeTimer > 0.0f)
    {
        m_LoopNoticeTimer -= deltaTime;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored() || m_LoopCooldown > 0.0f)
    {
        return;
    }

    const Vector3 position = player.GetPosition();
    const bool insideLoopExit =
        position.x > 178.0f &&
        position.z > 237.0f && position.z < 273.0f;
    if (insideLoopExit)
    {
        AdvanceCorridorLoop(player);
    }
}

void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_CorridorLoopCount;

    // The camera is updated by Player later in the same frame, hiding a scene
    // reload and preserving the direction in which the player was looking.
    player.SetPosition(Vector3(0.0f, -99.0f, -150.0f));
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;

    for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
    {
        const std::string markerName =
            "PropLoopMarker" + std::to_string(markerIndex);
        Wall* marker = game->GetObj<Wall>(markerName);
        if (marker != nullptr)
        {
            marker->SetVisible(markerIndex <= loopPhase);
            const float intensity =
                0.14f + static_cast<float>(markerIndex) * 0.055f;
            marker->SetAppearance(
                Color(0.26f, 0.012f, 0.008f, 1.0f),
                Color(intensity, 0.002f, 0.001f, 1.0f),
                24.0f);
        }
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        0.24f + static_cast<float>(loopPhase) * 0.13f,
        0.42f + static_cast<float>(loopPhase) * 0.10f);
    Input::SetVibration(7 + loopPhase * 3, 0.18f + loopPhase * 0.04f);

    // Returning to the entrance also restores the corridor door. Reopening
    // the same physical threshold makes each loop feel deliberate.
    Door* loopDoor = game->GetObj<Door>("Door");
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }

    CeilingLight* entranceLight =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middleLight =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* cornerLight =
        game->GetObj<CeilingLight>("CeilingLight8");

    if (loopPhase == 1)
    {
        Item* secondFuse = game->GetObj<Item>("Item2");
        if (secondFuse != nullptr && !secondFuse->IsCollected())
        {
            secondFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(false, 2.4f);
        }
    }
    else if (loopPhase == 2)
    {
        Item* thirdFuse = game->GetObj<Item>("Item3");
        if (thirdFuse != nullptr && !thirdFuse->IsCollected())
        {
            thirdFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 5.8f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, 1.1f);
        }

        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(0.0f, -99.0f, -25.0f);
            });
    }
    else
    {
        if (entranceLight != nullptr)
        {
            entranceLight->SetEmergencyLight(false, 0.0f);
        }
        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 8.2f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, 4.3f);
        }

        // The warning is literal: a single apparition waits behind the player.
        if (m_CorridorLoopCount == 3)
        {
            game->RequestAddObject<ShadowMan>(
                [this](ShadowMan& shadow)
                {
                    shadow.SetPosition(0.0f, -99.0f, -170.0f);
                    shadow.EnableGazeScare();
                    shadow.SetOnObserved(
                        [this]()
                        {
                            StartScareLightSequence();
                        });
                });
        }
    }
}

void StageScene::StartScareLightSequence()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        return;
    }

    m_ScareLightTimer = 0.0f;
    m_ScareLightPhase = 0;
    m_ScareMessageTimer = 3.8f;

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* hall =
        game->GetObj<CeilingLight>("CeilingLight5");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");
    CeilingLight* loopExit =
        game->GetObj<CeilingLight>("CeilingLight7");

    if (entrance != nullptr)
    {
        entrance->SetEmergencyLight(true, 0.35f);
    }
    if (middle != nullptr)
    {
        middle->SetEmergencyLight(false, 8.2f);
    }
    if (hall != nullptr)
    {
        hall->SetEmergencyLight(false, 3.1f);
    }
    if (corner != nullptr)
    {
        corner->SetEmergencyLight(false, 4.3f);
    }
    if (loopExit != nullptr)
    {
        loopExit->SetEmergencyLight(false, 4.5f);
    }
}

void StageScene::UpdateScareLightSequence()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    if (m_ScareMessageTimer > 0.0f)
    {
        m_ScareMessageTimer -= deltaTime;
    }

    if (m_ScareLightTimer < 0.0f)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = -1;
        return;
    }

    m_ScareLightTimer += deltaTime;

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* hall =
        game->GetObj<CeilingLight>("CeilingLight5");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");
    CeilingLight* loopExit =
        game->GetObj<CeilingLight>("CeilingLight7");

    if (m_ScareLightPhase == 0 && m_ScareLightTimer >= 0.45f)
    {
        if (entrance != nullptr)
        {
            entrance->SetEmergencyLight(false, 0.35f);
        }
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 0.75f);
        }
        m_ScareLightPhase = 1;
        game->GetPostProcess()->TriggerHorrorPulse(0.13f, 0.18f);
        Input::SetVibration(5, 0.10f);
    }
    else if (m_ScareLightPhase == 1 && m_ScareLightTimer >= 0.90f)
    {
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, 0.75f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 0.42f);
        }
        m_ScareLightPhase = 2;
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(6, 0.13f);
    }
    else if (m_ScareLightPhase == 2 && m_ScareLightTimer >= 1.38f)
    {
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(false, 0.42f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 0.18f);
        }
        m_ScareLightPhase = 3;
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.20f);
        Input::SetVibration(7, 0.16f);
    }
    else if (m_ScareLightPhase == 3 && m_ScareLightTimer >= 1.92f)
    {
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(false, 0.18f);
        }
        if (loopExit != nullptr)
        {
            loopExit->SetEmergencyLight(true, 0.08f);
        }
        m_ScareLightPhase = 4;
        game->GetPostProcess()->TriggerHorrorPulse(0.19f, 0.22f);
        Input::SetVibration(8, 0.19f);
    }
    else if (m_ScareLightPhase == 4 && m_ScareLightTimer >= 2.65f)
    {
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 2.6f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 3.1f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 4.3f);
        }

        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = 5;
        Input::SetVibration(4, 0.09f);
    }
}

void StageScene::StartFuseWatcher(int fuseCount)
{
    if (fuseCount < 2 || Core::Game::GetInstance()->IsPowerRestored())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* watcher = game->GetObj<ShadowMan>("Stage1FuseWatcher");
    if (watcher == nullptr)
    {
        return;
    }

    watcher->SetActive(false);
    watcher->SetPosition(
        0.0f,
        -99.0f,
        fuseCount == 2 ? -22.0f : 118.0f);
    watcher->SetActive(true);
    watcher->EnableGazeScare(fuseCount == 2 ? 5.8f : 7.2f);
    watcher->SetOnObserved(
        [this]()
        {
            m_FuseWatcherState = 2;
            m_FuseWatcherNoticeTimer = 1.8f;

            Core::Game* game = Core::Game::GetInstance();
            game->RegisterAnomalyHandled();
            ShadowMan* activeWatcher =
                game->GetObj<ShadowMan>("Stage1FuseWatcher");
            if (activeWatcher != nullptr)
            {
                activeWatcher->SetActive(false);
            }

            CeilingLight* reactionLight =
                game->GetObj<CeilingLight>("CeilingLight4");
            if (reactionLight != nullptr)
            {
                reactionLight->TriggerEventFlicker(0.72f, 0.74f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.18f);
            Input::SetVibration(7, 0.16f);
        });

    m_FuseWatcherState = 1;
    m_FuseWatcherNoticeTimer = fuseCount == 2 ? 5.8f : 7.2f;
    game->GetPostProcess()->TriggerHorrorPulse(
        fuseCount == 2 ? 0.20f : 0.32f,
        0.30f);
}

void StageScene::UpdatePowerRestoreSequence()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();

    if (powerRestored && !m_WasPowerRestored)
    {
        m_PowerRestoreTimer = 0.0f;
        m_ProgressHintTimer = 0.0f;
        m_PowerRestorePhase = 0;

        // Power restoration owns the presentation from this point onward.
        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = -1;
        m_ScareMessageTimer = 0.0f;

        for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
        {
            const std::string markerName =
                "PropLoopMarker" + std::to_string(markerIndex);
            Wall* marker = game->GetObj<Wall>(markerName);
            if (marker != nullptr)
            {
                marker->SetVisible(false);
            }
        }
    }

    m_WasPowerRestored = powerRestored;
    if (!powerRestored || m_PowerRestoreTimer < 0.0f)
    {
        return;
    }

    m_PowerRestoreTimer += deltaTime;

    if (m_PowerRestorePhase == 0 && m_PowerRestoreTimer >= 0.38f)
    {
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.55f);
        Input::SetVibration(9, 0.16f);
        m_PowerRestorePhase = 1;
    }
    else if (m_PowerRestorePhase == 1 && m_PowerRestoreTimer >= 1.15f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.22f);
        Input::SetVibration(6, 0.11f);
        m_PowerRestorePhase = 2;
    }
    else if (m_PowerRestorePhase == 2 && m_PowerRestoreTimer >= 2.25f)
    {
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.40f);
        Input::SetVibration(4, 0.07f);
        m_PowerRestorePhase = 3;
    }
}

void StageScene::UpdateExitPowerSequence()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    Core::Game* game = Core::Game::GetInstance();
    FuseBox* panel = game->GetObj<FuseBox>("ExitPowerPanel");
    if (panel == nullptr || !panel->IsActivated())
    {
        return;
    }

    if (m_ExitPowerEventTimer < 0.0f)
    {
        m_ExitPowerEventTimer = 0.0f;
        m_ExitPowerEventPhase = 0;
        m_ProgressHintTimer = 0.0f;

        CeilingLight* corner = game->GetObj<CeilingLight>("CeilingLight7");
        CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight8");
        if (corner != nullptr) corner->SetForcedOff(true);
        if (exitLight != nullptr) exitLight->SetForcedOff(true);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.28f);
        return;
    }

    if (m_ExitPowerSequenceComplete)
    {
        return;
    }

    m_ExitPowerEventTimer += deltaTime;
    if (m_ExitPowerEventPhase == 0 && m_ExitPowerEventTimer >= 0.28f)
    {
        CeilingLight* corner = game->GetObj<CeilingLight>("CeilingLight7");
        if (corner != nullptr)
        {
            corner->SetForcedOff(false);
            corner->SetEmergencyLight(false, 0.0f);
            corner->TriggerEventFlicker(0.72f, 0.82f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.68f, 0.24f);
        Input::SetVibration(5, 0.11f);
        m_ExitPowerEventPhase = 1;
    }
    else if (m_ExitPowerEventPhase == 1 && m_ExitPowerEventTimer >= 0.78f)
    {
        CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight8");
        if (exitLight != nullptr)
        {
            exitLight->SetForcedOff(false);
            exitLight->SetEmergencyLight(false, 0.0f);
            exitLight->TriggerEventFlicker(0.82f, 0.92f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.30f);
        Input::SetVibration(7, 0.15f);
        m_ExitPowerEventPhase = 2;
    }
    else if (m_ExitPowerEventPhase == 2 && m_ExitPowerEventTimer >= 1.30f)
    {
        m_ExitPowerSequenceComplete = true;
        m_ExitPowerEventPhase = 3;
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.42f);
        Input::SetVibration(10, 0.20f);
    }
}

void StageScene::UpdateExitOmen(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
    m_ExitOmenTimer =
        (std::max)(0.0f, m_ExitOmenTimer - deltaTime);
    if (m_ExitOmenTriggered)
    {
        Core::Game* game = Core::Game::GetInstance();
        if (m_ExitOmenPhase == 0 && m_ExitOmenTimer <= 2.45f)
        {
            CeilingLight* lightBehind =
                game->GetObj<CeilingLight>("CeilingLight8");
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
            game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.30f);
            Input::SetVibration(7, 0.16f);
            m_ExitOmenPhase = 1;
        }
        else if (m_ExitOmenPhase == 1 && m_ExitOmenTimer <= 1.35f)
        {
            CeilingLight* exitLight =
                game->GetObj<CeilingLight>("CeilingLight7");
            if (exitLight != nullptr)
            {
                exitLight->SetFaulted(true);
                exitLight->TriggerEventFlicker(1.10f, 0.88f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.44f, 0.20f);
            m_ExitOmenPhase = 2;
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    if (!m_ExitPowerSequenceComplete ||
        playerPosition.z < 215.0f ||
        playerPosition.x < 28.0f)
    {
        return;
    }

    m_ExitOmenTriggered = true;
    m_ExitOmenTimer = 3.2f;
    m_ExitOmenPhase = 0;

    ShadowMan* shadow =
        game->GetObj<ShadowMan>("Stage1ExitOmen");
    if (shadow != nullptr)
    {
        shadow->SetActive(true);
        shadow->EnableGazeScare(4.2f);
    }

    const char* exitLightNames[] =
    {
        "CeilingLight7", "CeilingLight8"
    };
    for (const char* lightName : exitLightNames)
    {
        CeilingLight* light =
            game->GetObj<CeilingLight>(lightName);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, 0.78f);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.42f);
    game->GetPostProcess()->TriggerBloomPulse(0.54f, 0.24f);
    Input::SetVibration(10, 0.22f);
}

