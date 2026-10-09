// ============================================================================
// ファイルの役割: 1面のシーンの初期化と、毎フレームの進行の中心（各演出・仕組みの更新を順に呼ぶ）を担当している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、進行の状態の管理、周りの物で物語を伝える演出
// ============================================================================

#include "StageScene.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "Renderer.h"

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

// 作るときに初期化している
StageScene::StageScene()
{
    Init();
}

// 壊すときに後片付けをしている
StageScene::~StageScene()
{
    Uninit();
}

// 1面で必要な床・壁・照明・アイテム・進行用のObjectをまとめて配置し、各仕組みを準備している。
void StageScene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    // 1面は長く放置された施設なので、壁にパネルの継ぎ目・ひび・水の垂れた跡・カビを出している。
    Renderer::SetWallWeathering(1.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.52f);
    game->GetPostProcess()->SetLensDirtStrength(0.10f);
    // 進行の状態をすべて最初に戻している
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
    m_TensionPulse.Reset();
    m_ProgressHintTimer = 0.0f;

    // 壁・照明・ヒューズ・扉などの配置はStage1Layoutが担当し、使うObjectのポインタをまとめて返している。
    m_Objects = Stage1Layout::Build(*game);
    // 壁の文字・暗証番号の扉・隠し部屋・西棟の仕組みに、配置したObjectを渡している
    m_WallWritings.Init(m_Objects.writings);
    m_KeypadDoor.Init(m_Objects.keypad);
    m_HiddenRoom.Init(m_Objects.hiddenRoom);
    m_WestWing.Init(m_Objects.westWing);
    // 照らすと止まる影の判定と押し戻しに使う壁と扉を、一度だけ集めている（毎フレーム全Objectを調べないため）
    m_ArchiveStalker = Stage1LightStalker{};
    m_StalkerWalls = game->GetObjects<Wall>();
    m_StalkerDoors = game->GetObjects<Door>();
    m_ArchiveStalkerNoticeTimer = 0.0f;
    // 部屋の角の暗がり。床（y=-100）と天井の下面（y≒-48.5）の高さと、建物の壁の形を渡している。
    Renderer::SetRoomOcclusion(
        m_Objects.wallFootprints.data(),
        static_cast<unsigned int>(m_Objects.wallFootprints.size()),
        -100.0f, -48.5f, 0.55f);

    // シーンを変えた後の最初の描画の前に、カメラとライトを更新している。
    // ImGuiでゲームを止めた場合も、面全体が黒くなるのを防いでいる。
    m_Objects.player->Update();

    m_Hud.Init();
    // 監視カメラの巡回は配置したObjectを使うため、配置の後に準備している。
    m_Surveillance.Init(m_Objects);
    SetupPracticalLights();
}

// 看板やランプが、自分の光る色で周りの床と壁を照らすようにしている。
// 天井照明だけだった頃は、光源の数の上限（8個）のせいで足せなかった小さな光。
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

// ヒューズの数と電力の状態をもとに、目的表示と演出の段階を更新している。
void StageScene::Update()
{
    Player* player =
        m_Objects.player;

    if (player == nullptr)
    {
        return;
    }

    // 映像の確認中と捕まっている間は操作できないので、操作できるかの判定より前に更新している。
    if (m_Surveillance.Update(*player, Application::GetDeltaTime()))
    {
        // 巡回をすべて終えたら、記録端末の完了の知らせを出している。
        m_EvidenceNoticeTimer = 3.2f;
    }
    // 暗証番号の入力画面も、操作できない間に入力を受け取るため、操作できるかの判定より前に更新している。
    m_KeypadDoor.Update(*player, Application::GetDeltaTime());
    // 操作できない間（映像の確認・捕まっている間）も呼び、途中の物音を打ち切れるようにしている。
    UpdateAmbientSounds();
    if (!player->CanControl())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const float deltaTime = Application::GetDeltaTime();
    // 経過時間と、進行が止まっている時間を数えている。H（LB）が押されたら、すぐに強いヒントを出している
    m_StageVisualTimer += deltaTime;
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 35.0f);
        Input::SetVibration(2, 0.04f);
    }

    // 各知らせの残り時間を減らしている
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
    // ヒューズを拾った瞬間、知らせを出し、画面の光と振動で伝え、廊下に影を出している
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

    // 電力が戻ったら、ヒューズの後に出る影を消している
    if (game->IsPowerRestored())
    {
        ShadowMan* watcher = m_Objects.fuseWatcher;
        if (watcher != nullptr) watcher->SetActive(false);
        m_FuseWatcherState = 0;
    }

    // 非常用充電器を使ったら、電池を回復する代わりに、音で廊下に影を呼んでいる
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

    // 各演出と仕組みを順に更新している
    UpdateCorridorLoop(*player);
    UpdateEntranceThresholdEvent(*player);
    UpdateStorageScare(*player);
    UpdateScareLightSequence();
    UpdatePowerRestoreSequence();
    UpdateExitPowerSequence();
    UpdateExitOmen(*player);
    UpdateWallWritings(*player);
    m_HiddenRoom.Update(*player, deltaTime, m_KeypadDoor.IsSolved());
    UpdateArchiveStalker(*player, deltaTime);
    // 西棟の進行と、水の滴る音・背後で水の中を歩く音などの物音。
    m_WestWingCues.clear();
    m_WestWing.Update(*player, deltaTime, game->GetCamera()->GetForward(), m_WestWingCues);
    for (const AmbientSoundCue& cue : m_WestWingCues)
    {
        game->PlayAudioCueAt(
            cue.Label, cue.Position, cue.Pitch, cue.Volume, cue.MinimumOcclusion);
    }
    UpdateTensionPulse(*player, deltaTime);

    // 出口の扉：送電が終わるまで鍵をかけ、開いた扉を通り抜けたら脱出を始めている
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

    // 出口の表示：送電が終わるまで赤く、終わったら緑で脈打たせている
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

    // 扉のランプ：出口の前兆の間は速く点滅させている
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

    // 画面効果：電池の少なさ・走っているか・ループ廊下の奥への近さで、ノイズと周辺減光を強めている
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

    // ループ廊下の奥ほど圧迫感を強め、周回の数に応じて基準の値も上げている。
    // 電力が戻った後は効果を外し、状況が変わったことを伝えている。
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

    // ホラーらしい暗さを保ちつつ、懐中電灯なしでも時間とともに目が慣れ、
    // 最低限動ける明るさにしている。
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

    // 調べる対象を選び、Eキー（A）で調べている
    m_InteractionSystem.Update(*player);
}



// 描画の設定と画面効果を普段の値に戻し、配置で作ったObjectを破棄している
void StageScene::Uninit()
{
    // ほかの面の壁は前の見た目に戻している（シーンを切り替えるときは、古いSceneの破棄が先に行われる）。
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
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

    // Stage1Layoutが作るときに記録した名前の一覧で破棄している（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
}
