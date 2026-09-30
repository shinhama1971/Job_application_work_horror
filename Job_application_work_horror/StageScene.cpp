// ============================================================================
// ファイルの役割: 1面のステージ配置、ヒューズ探索、電力復旧、出口までの進行を管理します。
// 主な技術: シーン構成、オブジェクト配置、進行状態、環境ストーリーテリング
// ============================================================================

#include "StageScene.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "ShadowMan.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

StageScene::StageScene()
{
    Init();
}

StageScene::~StageScene()
{
    Uninit();
}

// 1面で必要な床、壁、照明、アイテム、進行用Triggerをまとめて配置します。
void StageScene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.52f);
    game->GetPostProcess()->SetLensDirtStrength(0.10f);
    m_CorridorLoopCount = 0;
    m_LastFuseCount = game->GetItemCount();
    m_FuseNoticeTimer = 0.0f;
    m_FuseWatcherNoticeTimer = 0.0f;
    m_FuseWatcherState = 0;
    m_ChargerNoticeTimer = 0.0f;
    m_ChargerHandled = false;
    m_EvidenceNoticeTimer = 0.0f;
    m_LoopCooldown = 0.0f;
    m_LoopNoticeTimer = 0.0f;
    m_EntranceEventTriggered = false;
    m_EntranceEventTimer = -1.0f;
    m_EntranceEventPhase = -1;
    m_StorageScarePhase = 0;
    m_StorageScareTimer = 0.0f;
    m_StorageScareNoticeTimer = 0.0f;
    m_ScareLightSequence.Reset();
    m_PowerSequence.Reset();
    m_StageVisualTimer = 0.0f;
    m_ExitOmenSequence.Reset();
    m_AmbientSounds.Reset();
    m_ProgressHintTimer = 0.0f;

    // 壁・照明・ヒューズ・扉などの配置はStage1Layoutが担当し、使うObjectのポインタをまとめて返します。
    m_Objects = Stage1Layout::Build(*game);
    m_WallWritings.Init(m_Objects.writings);
    m_KeypadDoor.Init(m_Objects.keypad);

    // シーン変更後の初回描画前にカメラとライトを更新します。
    // ImGuiでゲームを停止した場合も、面全体が黒くなることを防ぎます。
    m_Objects.player->Update();

    m_Hud.Init();
    // 監視カメラ巡回は配置済みのObjectを使うため、配置の後に準備します。
    m_Surveillance.Init(m_Objects);
    SetupPracticalLights();
}

// 看板や表示灯が自分の発光色で周囲の床と壁を照らすようにします。
// 天井照明だけだった頃は光源数の上限(8個)で足せなかった小さな光です。
void StageScene::SetupPracticalLights()
{
    m_Objects.exitSign->SetGlowLight(60.0f, 2.2f);
    m_Objects.doorIndicator->SetGlowLight(45.0f, 2.0f);
    m_Objects.evidenceMarker->SetGlowLight(45.0f, 2.0f);
    for (Wall* marker : m_Objects.loopMarkers)
    {
        marker->SetGlowLight(40.0f, 1.8f);
    }
}

// ヒューズ数と電力状態を基準に目的表示とイベント段階を更新します。
void StageScene::Update()
{
    Player* player =
        m_Objects.player;

    if (player == nullptr)
    {
        return;
    }

    // 映像確認中と捕獲中は操作不能なので、操作可否の判定より前に更新します。
    if (m_Surveillance.Update(*player, Application::GetDeltaTime()))
    {
        // 巡回をすべて終えたら、記録端末の完了通知を出します。
        m_EvidenceNoticeTimer = 3.2f;
    }
    // 暗証番号の入力画面も操作不能の間に入力を受け取るため、操作可否の判定より前に更新します。
    m_KeypadDoor.Update(*player, Application::GetDeltaTime());
    // 操作できない間（映像確認・捕獲中）も呼び、途中の物音を打ち切れるようにします。
    UpdateAmbientSounds();
    if (!player->CanControl())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const float deltaTime = Application::GetDeltaTime();
    m_StageVisualTimer += deltaTime;
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 35.0f);
        Input::SetVibration(2, 0.04f);
    }

    m_FuseNoticeTimer = (std::max)(
        0.0f, m_FuseNoticeTimer - deltaTime);
    m_FuseWatcherNoticeTimer = (std::max)(
        0.0f, m_FuseWatcherNoticeTimer - deltaTime);
    m_ChargerNoticeTimer = (std::max)(
        0.0f, m_ChargerNoticeTimer - deltaTime);
    m_EvidenceNoticeTimer = (std::max)(
        0.0f, m_EvidenceNoticeTimer - deltaTime);
    m_StorageScareNoticeTimer = (std::max)(
        0.0f, m_StorageScareNoticeTimer - deltaTime);
    const int currentFuseCount = game->GetItemCount();
    if (currentFuseCount > m_LastFuseCount)
    {
        m_LastFuseCount = currentFuseCount;
        m_ProgressHintTimer = 0.0f;
        m_FuseNoticeTimer = 2.35f;
        game->GetPostProcess()->TriggerBloomPulse(
            0.48f + static_cast<float>(currentFuseCount) * 0.12f,
            0.28f);
        Input::SetVibration(
            4 + currentFuseCount * 2,
            0.10f + static_cast<float>(currentFuseCount) * 0.025f);
        StartFuseWatcher(currentFuseCount);
    }

    if (game->IsPowerRestored())
    {
        ShadowMan* watcher = m_Objects.fuseWatcher;
        if (watcher != nullptr) watcher->SetActive(false);
        m_FuseWatcherState = 0;
    }

    FuseBox* emergencyCharger =
        m_Objects.emergencyCharger;
    if (!m_ChargerHandled && emergencyCharger != nullptr &&
        emergencyCharger->IsActivated())
    {
        m_ChargerHandled = true;
        m_ChargerNoticeTimer = 2.8f;
        player->AddBattery(35.0f);
        game->RegisterChargerUsed();
        StartFuseWatcher(2);

        CeilingLight* chargerLight =
            m_Objects.CeilingLightAt(2);
        if (chargerLight != nullptr)
        {
            chargerLight->TriggerEventFlicker(0.92f, 0.84f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.66f, 0.24f);
        Input::SetVibration(6, 0.14f);
    }

    UpdateCorridorLoop(*player);
    UpdateEntranceThresholdEvent(*player);
    UpdateStorageScare(*player);
    UpdateScareLightSequence();
    UpdatePowerRestoreSequence();
    UpdateExitPowerSequence();
    UpdateExitOmen(*player);
    UpdateWallWritings(*player);

    Door* stageExitDoor = m_Objects.exitDoor;
    ExitTrigger* stageExit = m_Objects.exitTrigger;
    const bool exitPowerReady = m_PowerSequence.IsExitComplete();
    if (stageExitDoor != nullptr)
    {
        stageExitDoor->SetLocked(!exitPowerReady);
        const Vector3 exitPosition = player->GetPosition();
        const bool crossedOpenedDoor =
            stageExitDoor->IsOpen() &&
            exitPosition.x >= 199.0f &&
            std::abs(exitPosition.z - 307.5f) <= 34.0f;
        if (crossedOpenedDoor && stageExit != nullptr)
        {
            stageExit->BeginEscape(*player);
        }
    }

    Wall* stageExitSign = m_Objects.exitSign;
    if (stageExitSign != nullptr)
    {
        const float pulse = 0.78f +
            std::sin(m_StageVisualTimer *
                (game->IsPowerRestored() ? 3.2f : 7.4f)) * 0.16f;
        if (exitPowerReady)
        {
            stageExitSign->SetAppearance(
                Color(0.025f, 0.24f, 0.06f, 1.0f),
                Color(0.006f, 0.36f * pulse, 0.025f, 1.0f),
                34.0f);
        }
        else
        {
            stageExitSign->SetAppearance(
                Color(0.24f, 0.025f, 0.018f, 1.0f),
                Color(0.32f * pulse, 0.004f, 0.002f, 1.0f),
                30.0f);
        }
    }

    Wall* exitIndicator = m_Objects.doorIndicator;
    if (exitIndicator != nullptr)
    {
        const float omenRate = m_ExitOmenSequence.IsTriggered()
            ? 1.0f - (std::clamp)(
                m_ExitOmenSequence.GetTimer() / 3.2f, 0.0f, 1.0f)
            : 0.0f;
        const float indicatorPulse =
            0.72f + std::sin(m_StageVisualTimer *
                (3.2f + omenRate * 8.0f)) *
                (0.10f + omenRate * 0.14f);
        if (exitPowerReady)
        {
            exitIndicator->SetAppearance(
                Color(0.025f, 0.20f, 0.055f, 1.0f),
                Color(0.005f, 0.24f * indicatorPulse, 0.025f, 1.0f),
                30.0f);
        }
        else
        {
            exitIndicator->SetAppearance(
                Color(0.24f, 0.025f, 0.018f, 1.0f),
                Color(0.28f * indicatorPulse, 0.004f, 0.002f, 1.0f),
                28.0f);
        }
    }

    float lowBattery = (25.0f - player->GetBattery()) / 25.0f;
    if (lowBattery < 0.0f) lowBattery = 0.0f;
    if (lowBattery > 1.0f) lowBattery = 1.0f;

    const float powerBlend = game->IsPowerRestored()
        ? (std::clamp)(
            m_PowerSequence.GetRestoreTimer() / 2.5f, 0.0f, 1.0f)
        : 0.0f;
    const float powerCalm = 0.03f * powerBlend;
    const float sprintStress = player->IsSprinting() ? 1.0f : 0.0f;
    const Vector3 playerPosition = player->GetPosition();

    // ループ廊下の奥ほど圧迫感を強め、周回数に応じて基準値も上げます。
    // 通電後は効果を解除し、状況が変わったことを伝えます。
    const float corridorDepth = (std::clamp)(
        (playerPosition.z - 90.0f) / 190.0f,
        0.0f,
        1.0f);
    const float corridorWidthMask = 1.0f - (std::clamp)(
        (std::abs(playerPosition.x) - 75.0f) / 105.0f,
        0.0f,
        1.0f);
    const float loopTension = (std::min)(
        static_cast<float>(m_CorridorLoopCount) * 0.14f,
        0.42f);
    const float poweredExitDepth = (std::clamp)(
        (playerPosition.z - 190.0f) / 105.0f,
        0.0f,
        1.0f);
    const float corridorTension = game->IsPowerRestored()
        ? poweredExitDepth * 0.38f
        : (std::clamp)(
            corridorDepth * corridorWidthMask * 0.72f + loopTension,
            0.0f,
            1.0f);
    game->GetPostProcess()->SetCorridorTension(corridorTension);

    // ホラーらしい暗さを保ちつつ、懐中電灯なしでも時間経過で目が慣れ、
    // 最低限移動できる明るさへ調整します。
    const float targetExposure = game->IsPowerRestored()
        ? 1.12f + (1.01f - 1.12f) * powerBlend
        : (player->IsFlashlightOn() ? 1.04f : 1.15f);
    game->GetPostProcess()->SetExposure(targetExposure);
    const float noiseAmount =
        0.18f - powerCalm + lowBattery * 0.18f +
        sprintStress * 0.04f + corridorTension * 0.055f;
    const float vignetteStrength =
        0.55f - powerCalm + lowBattery * 0.20f +
        sprintStress * 0.06f + corridorTension * 0.075f;

    game->GetPostProcess()->SetAtmosphere(
        noiseAmount,
        vignetteStrength);
    game->GetPostProcess()->SetLensDistortionStrength(
        0.20f + corridorTension * 0.22f);
    game->GetPostProcess()->SetFilmGradeStrength(
        0.52f + corridorTension * 0.18f);
    game->GetPostProcess()->SetLensDirtStrength(
        0.10f + corridorTension * 0.12f);

    m_InteractionSystem.Update(*player);
}



void StageScene::Uninit()
{
    Core::Game::GetInstance()->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    Core::Game::GetInstance()->GetPostProcess()->SetExposure(1.0f);
    Core::Game::GetInstance()->GetPostProcess()->SetCorridorTension(0.0f);
    Core::Game::GetInstance()->GetPostProcess()->SetVolumetricLight(false);
    Core::Game::GetInstance()->GetPostProcess()->SetLensDistortionStrength(0.20f);
    Core::Game::GetInstance()->GetPostProcess()->SetFilmGradeStrength(0.55f);
    Core::Game::GetInstance()->GetPostProcess()->SetLensDirtStrength(0.10f);
    m_Surveillance.Uninit();
    m_Hud.Uninit();

    Core::Game* game = Core::Game::GetInstance();

    // Stage1Layoutが生成時に記録した名前の一覧で破棄します（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
}
