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
#include "SceneLayoutBuilder.h"
#include "PropLayoutFile.h"
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
    m_SafeLights.Init(m_Objects.safeZones);
    // 照らすと止まる影の判定と押し戻しに使う壁と扉を、一度だけ集めている（毎フレーム全Objectを調べないため）
    m_ArchiveStalker = Stage1LightStalker{};
    m_FootprintFollower.Reset();
    m_FootprintFollowerUsed = false;
    m_RadioStatic.Reset();
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
    m_ActiveRoute = static_cast<std::size_t>(Stage1RouteLight::Count);
    m_RouteFixtureIndex = 0;
    UpdateRouteLights();
}

// 看板やランプが、自分の光る色で周りの床と壁を照らすようにしている。
// 天井照明だけだった頃は、光源の数の上限（8個）のせいで足せなかった小さな光。
void StageScene::SetupPracticalLights()
{
    m_Objects.exitSign->SetGlowLight(60.0f, 2.2f);
    m_Objects.startDoorSign->SetGlowLight(60.0f, 2.2f);
    m_Objects.doorIndicator->SetGlowLight(45.0f, 2.0f);
    m_Objects.evidenceMarker->SetGlowLight(45.0f, 2.0f);
    for (Wall* marker : m_Objects.loopMarkers)
    {
        marker->SetGlowLight(40.0f, 1.8f);
    }
}

// 小物の配置のファイルを読み直し、今の小物を置き直している（Debug版の F5）。
// 書き間違いがあれば置き直さず、何行目が読めなかったかを画面の左上に出している。
void StageScene::ReloadPropLayout()
{
    const PropLayout layout = PropLayoutFile::Load(Stage1Layout::PropLayoutPath);
    if (!layout.IsValid())
    {
        m_LayoutNotice = "配置のファイルの" + std::to_string(layout.ErrorLine) + "行目が読めない " + layout.Error;
        m_LayoutNoticeTimer = 6.0f;
        return;
    }
    SceneLayoutBuilder builder(*Core::Game::GetInstance(), m_Objects.objectNames);
    PropLayoutFile::Reload(builder, layout, m_Objects.layoutProps);
    m_PropLayoutEditor.SetLayout(layout);
    m_LayoutNotice = "配置のファイルを読み直した（小物 " + std::to_string(layout.Props.size()) + "個）";
    m_LayoutNoticeTimer = 3.0f;
}

// 進む先の天井照明を1灯だけ、切れかけの電球のように不規則にちかちかさせている（ほかの照明は落ち着いて点いたまま）。
// 矢印・明るさ・点灯の順番では道を示さず、「あの電気だけ様子がおかしい」と感じて自分から近づくようにしている。
// 経路の選択は進行を参照するだけで、鍵・アイテム・扉の状態は変更しない。
void StageScene::UpdateRouteLights()
{
    const Core::Game* game = Core::Game::GetInstance();
    const int fuseCount = game->GetItemCount();
    const bool powered = game->IsPowerRestored();
    // 閉じ込めや照明の演出の最中は、演出の明滅と混ざらないよう、どれもちかちかさせない
    const bool allowed = m_Objects.player != nullptr && m_Objects.player->CanControl() &&
        !m_HiddenRoom.IsTrapped() && !m_ScareLightSequence.IsActive();
    const Stage1WestWing::Step westWingStep = m_WestWing.GetStep();
    Stage1RouteLight selected = Stage1RouteLight::Count;
    if (allowed)
    {
        if (powered)
        {
            if (m_PowerSequence.IsExitComplete())
            {
                selected = Stage1RouteLight::ExitDoor;
            }
            else if (m_Objects.exitPowerPanel != nullptr && !m_Objects.exitPowerPanel->IsActivated())
            {
                selected = Stage1RouteLight::ExitPower;
            }
        }
        else if (fuseCount <= 0)
        {
            selected = Stage1RouteLight::StartSearch;
        }
        else if ((fuseCount == 1 && m_CorridorLoopCount < 1) ||
            (fuseCount == 2 && m_CorridorLoopCount < 2))
        {
            selected = Stage1RouteLight::CorridorDoor;
        }
        else if (fuseCount == 1)
        {
            selected = Stage1RouteLight::LeftStorage;
        }
        else if (fuseCount >= 3)
        {
            selected = Stage1RouteLight::MainPower;
        }
        else if (westWingStep == Stage1WestWing::Step::FindKey)
        {
            selected = Stage1RouteLight::RightStorage;
        }
        else if (westWingStep == Stage1WestWing::Step::OpenDoor)
        {
            selected = Stage1RouteLight::WestWing;
        }
        else if (westWingStep == Stage1WestWing::Step::FindFuse)
        {
            selected = Stage1RouteLight::PumpRoom;
        }
        // 排水中は、バルブの赤いランプが目印になるので、ちかちかさせない。
    }

    const std::size_t route = static_cast<std::size_t>(selected);
    if (route != m_ActiveRoute)
    {
        m_ActiveRoute = route;
        m_RouteFixtureIndex = 0;
    }

    // いったん全部の候補を「落ち着いて点いている」状態にし、進む先の1灯だけを後でちかちかさせる
    for (const std::vector<CeilingLight*>& fixtures : m_Objects.routeLights)
    {
        for (CeilingLight* light : fixtures)
        {
            if (light != nullptr)
            {
                light->SetRouteFlicker(false);
            }
        }
    }
    if (route >= m_Objects.routeLights.size() || m_Objects.routeLights[route].empty())
    {
        return;
    }
    const std::vector<CeilingLight*>& fixtures = m_Objects.routeLights[route];

    // 時間で次々に移さず、ちかちかしている灯の下まで歩いて来たときだけ、その先の灯へ移している。
    const Vector3 playerPosition = m_Objects.player->GetPosition();
    const auto distanceSquared = [&playerPosition](const CeilingLight* light)
    {
        if (light == nullptr)
        {
            return 1.0e20f;
        }
        const Vector3 position = light->GetPosition();
        const float x = position.x - playerPosition.x;
        const float z = position.z - playerPosition.z;
        return x * x + z * z;
    };
    while (m_RouteFixtureIndex + 1 < fixtures.size())
    {
        const float currentDistance = distanceSquared(fixtures[m_RouteFixtureIndex]);
        const float nextDistance = distanceSquared(fixtures[m_RouteFixtureIndex + 1]);
        if (currentDistance > 35.0f * 35.0f && nextDistance >= currentDistance)
        {
            break;
        }
        ++m_RouteFixtureIndex;
    }
    if (CeilingLight* light = fixtures[m_RouteFixtureIndex])
    {
        light->SetRouteFlicker(true);
    }
}

// 自動撮影から、最後のドアを調べたのと同じように開けている（開けた瞬間の周回の切り替えは、次のUpdateで普段どおり行われる）
void StageScene::RequestDebugAction(SceneDebugAction action)
{
    Door* endDoor = m_Objects.exitDoor;
    Player* player = m_Objects.player;
    if (action == SceneDebugAction::OpenStage1EndDoor &&
        endDoor != nullptr && player != nullptr && endDoor->IsInteractionEnabled())
    {
        endDoor->Interact(*player);
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
        UpdateRouteLights();
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

    // 光の安全地帯：暖かい灯りの下で、ライトを消して休んでいる間は電池が少しずつ戻る。
    // 落ち着きの度合いは、心拍・物音・画面のざらつきを弱めるのに使っている。
    const float safeLightCharge = m_SafeLights.Update(
        deltaTime, player->GetPosition(), player->IsFlashlightOn(), player->GetBattery());
    if (safeLightCharge > 0.0f)
    {
        player->AddBattery(safeLightCharge);
    }

    // 各知らせの残り時間を減らしている
    m_FuseNoticeTimer = (std::max)(
        0.0f, m_FuseNoticeTimer - deltaTime);
    m_FuseWatcherNoticeTimer = (std::max)(
        0.0f, m_FuseWatcherNoticeTimer - deltaTime);
    m_ChargerNoticeTimer = (std::max)(
        0.0f, m_ChargerNoticeTimer - deltaTime);
    m_LayoutNoticeTimer = (std::max)(0.0f, m_LayoutNoticeTimer - deltaTime);
#if defined(_DEBUG)
    // Debug版だけ：F5 で小物の配置のファイル（assets/layout/stage1_props.txt）を読み直し、ゲームを動かしたまま置き直している。
    // 位置を少し直すたびにビルドし直さずに済むようにするため（Release版では効かない）。
    if (Input::GetKeyTrigger(VK_F5))
    {
        ReloadPropLayout();
    }
#endif
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
    UpdateFootprintFollower(*player);
    m_HiddenRoom.Update(*player, deltaTime, m_KeypadDoor.IsSolved());
    UpdateArchiveStalker(*player, deltaTime);
    // 西棟の進行（排水バルブの長押しを含む）と、水の滴る音・背後で水の中を歩く音などの物音。
    m_WestWingCues.clear();
    m_WestWing.Update(*player, deltaTime, game->GetCamera()->GetForward(),
        m_InteractionSystem.GetFocusedInteractable(), m_WestWingCues);
    for (const AmbientSoundCue& cue : m_WestWingCues)
    {
        game->PlayAudioCueAt(
            cue.Label, cue.Position, cue.Pitch, cue.Volume, cue.MinimumOcclusion);
    }
    UpdateRouteLights();
    UpdateTensionPulse(*player, deltaTime);

    // 最後のドア：電力が戻る前は開けて通ると次の周回へ進む（UpdateCorridorLoop）。
    // 電力が戻ってから出口の送電が終わるまでは鍵をかけ、送電が終わったら、開けて通り抜けたときに2面への脱出を始めている。
    Door* stageExitDoor = m_Objects.exitDoor;
    ExitTrigger* stageExit = m_Objects.exitTrigger;
    const bool exitPowerReady = m_PowerSequence.IsExitComplete();
    if (stageExitDoor != nullptr)
    {
        const bool waitingForExitPower = game->IsPowerRestored() && !exitPowerReady;
        // 周回のために開けたまま電力を戻した場合は、閉じてから鍵をかけている（送電の前に通れないようにする）
        if (waitingForExitPower && !stageExitDoor->IsLocked())
        {
            stageExitDoor->ResetClosed(0);
        }
        stageExitDoor->SetLocked(waitingForExitPower);
        if (exitPowerReady && stageExit != nullptr && IsThroughEndDoor(player->GetPosition()))
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
    // 開始地点の奥のドアの裏側の表示は、最後のドアの表示（送電の前の赤）と同じように脈打たせている
    if (m_Objects.startDoorSign != nullptr)
    {
        const float pulse = 0.78f + std::sin(m_StageVisualTimer * 7.4f) * 0.16f;
        m_Objects.startDoorSign->SetAppearance(
            Color(0.24f, 0.025f, 0.018f, 1.0f),
            Color(0.32f * pulse, 0.004f, 0.002f, 1.0f),
            30.0f);
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
    // 暖かい灯りの下（光の安全地帯）では、ざらつきと周辺の暗さを和らげ、ほっとできる画面にしている
    const float safeCalm = m_SafeLights.GetCalm();
    const float noiseAmount =
        0.18f - powerCalm + lowBattery * 0.18f +
        sprintStress * 0.04f + corridorTension * 0.055f - safeCalm * 0.07f;
    const float vignetteStrength =
        0.55f - powerCalm + lowBattery * 0.20f +
        sprintStress * 0.06f + corridorTension * 0.075f - safeCalm * 0.15f;

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
