// ============================================================================
// ファイルの役割: 1面の監視カメラの巡回（映像の確認・報告・現地での対処・捕まる）を進めている。
// 主な技術: 有限状態機械、RenderTextureによる別の視点の描画、入力と演出のタイミング合わせ
// 端末で監視映像を確かめて異常のあるカメラを報告し、現地で懐中電灯を当てて対処する。
// 間違った報告や時間切れが続くと捕まり、端末の前へ戻される。
// ============================================================================

#include "StageSurveillanceController.h"

#include "Application.h"
#include "CeilingLight.h"
#include "Door.h"
#include "FuseBox.h"
#include "Game.h"
#include "Hud.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    // 映像を開いてから操作を受け付けるまでの秒数（開いた直後の押し間違いを防いでいる）。
    constexpr float PatrolInputDelay = 0.65f;
    // 判定を間違えたときの表示の秒数、警告を始める残り秒数、照らしたと判定する距離と内積
    constexpr float PatrolWrongFeedbackSeconds = 1.35f;
    constexpr float PatrolWarningTime = 12.0f;
    constexpr float PatrolLookDistance = 110.0f;
    constexpr float PatrolLookAlignment = 0.90f;
    // 異常なしの回の割合。いつも異常がある状態では、報告がただの作業になってしまうためである。
    constexpr float PatrolNoAnomalyChance = 0.25f;

    // float[3]をVector3にしている
    Vector3 ToVector3(const float (&value)[3])
    {
        return Vector3(value[0], value[1], value[2]);
    }

    // 目的表示に出す異常の名前を返している
    const char* GetAnomalyLabel(SurveillancePatrol::AnomalyType type)
    {
        switch (type)
        {
        case SurveillancePatrol::AnomalyType::Figure:
            return "人影";
        case SurveillancePatrol::AnomalyType::LightOut:
            return "消えた照明";
        case SurveillancePatrol::AnomalyType::DoorOpen:
            return "開いた扉";
        case SurveillancePatrol::AnomalyType::None:
            return "異常";
        }
        return "異常";
    }
}

// 状態を最初に戻し、映像を描く960x540のテクスチャ（ライブ映像用と、各カメラの基準映像用）を作っている
void StageSurveillanceController::Init(const StageObjects& objects)
{
    m_Objects = &objects;
    m_Patrol.Reset();
    m_Caught.Reset();
    m_ViewTimer = 0.0f;
    m_Zoomed = false;
    m_ShowReference = false;
    m_ReferenceCapturePending = false;
    m_ReferenceCaptureIndex = 0;
    m_WrongTimer = 0.0f;
    m_WarningCooldown = 0.0f;
    m_NoticeTimer = 0.0f;
    m_NoticeText = "";
    m_CompletedThisFrame = false;

    m_Shader.Create(
        "shader/unlitTextureVS.hlsl",
        "shader/surveillanceFeedPS.hlsl");
    // 大きく表示しても、異常の輪郭が潰れない解像度を確保している。
    m_Feed.Init(960, 540);
    for (Graphics::RenderTexture& reference : m_References)
    {
        reference.Init(960, 540);
    }
}

// 映像用のテクスチャを解放している
void StageSurveillanceController::Uninit()
{
    m_Feed.Uninit();
    for (Graphics::RenderTexture& reference : m_References)
    {
        reference.Uninit();
    }
}

// 巡回を1フレーム進め、このフレームで巡回をすべて終えたかを返している
bool StageSurveillanceController::Update(Player& player, float deltaTime)
{
    m_CompletedThisFrame = false;
    UpdateState(player, deltaTime);
    return m_CompletedThisFrame;
}

// 監視映像の画面（ライブか基準映像か・カメラの名前・巡回の進み具合・間違いの数など）をHUDで描いている
void StageSurveillanceController::DrawFeed(Hud& hud)
{
    const int selectedCamera = m_Patrol.GetSelectedCamera();
    hud.DrawSurveillanceFeed(
        m_ShowReference
            ? m_References[selectedCamera].GetSRV()
            : m_Feed.GetSRV(),
        m_Shader,
        m_ViewTimer,
        StageSurveillanceCameras[selectedCamera].Label,
        selectedCamera,
        StageSurveillanceCameraCount,
        m_Patrol.GetRoundsCleared(),
        SurveillancePatrol::RequiredRounds,
        m_Patrol.GetMistakes(),
        SurveillancePatrol::MistakesUntilCaught,
        m_Zoomed,
        m_ShowReference,
        m_ViewTimer >= PatrolInputDelay && !m_ReferenceCapturePending,
        m_WrongTimer > 0.0f);
}

// 現地確認の間は、向かう先のカメラ・異常の名前・残り秒数・照らした割合を目的表示に渡している
void StageSurveillanceController::FillObjectiveInput(
    Stage1ObjectiveInput& input, const Player& player) const
{
    input.patrolDispatched =
        m_Patrol.GetState() == SurveillancePatrol::State::Dispatched;
    if (input.patrolDispatched)
    {
        const SurveillancePatrol::Anomaly& anomaly = m_Patrol.GetAnomaly();
        input.patrolCameraLabel = StageSurveillanceCameras[anomaly.camera].Label;
        input.patrolAnomalyLabel = GetAnomalyLabel(anomaly.type);
        input.patrolRemainingSeconds = static_cast<int>(
            std::ceil(m_Patrol.GetRemainingTime()));
        input.patrolConfirmPercent = static_cast<int>(
            std::round(m_Patrol.GetConfirmRate() * 100.0f));
    }
    input.flashlightOn = player.IsFlashlightOn();
    input.patrolNotice = m_NoticeTimer > 0.0f;
    input.patrolNoticeText = m_NoticeText;
}

// 状態ごとの処理を呼んでいる（捕まった演出の最中はそれだけ）
void StageSurveillanceController::UpdateState(Player& player, float deltaTime)
{
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);

    if (m_Caught.IsActive())
    {
        UpdateCaught(player, deltaTime);
        return;
    }

    switch (m_Patrol.GetState())
    {
    case SurveillancePatrol::State::Idle:
    {
        FuseBox* terminal = m_Objects->evidenceTerminal;
        if (terminal == nullptr || !terminal->IsActivated())
        {
            return;
        }

        // 基準映像を記録してから異常を出すため、ここでは種類だけを決めている。
        const SurveillancePatrol::Anomaly anomaly = ChooseAnomaly();
        m_Patrol.BeginViewing(anomaly);
        m_ViewTimer = 0.0f;
        m_Zoomed = false;
        m_ShowReference = false;
        m_ReferenceCapturePending = true;
        m_ReferenceCaptureIndex = 0;
        m_WrongTimer = 0.0f;
        player.SetCanControl(false);

        Core::Game* game = Core::Game::GetInstance();
        game->PlayAudioCue(SOUND_CUE_POWER, 0.72f);
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.20f);
        break;
    }
    case SurveillancePatrol::State::Viewing:
        m_ViewTimer += deltaTime;
        UpdateViewing(player);
        break;
    case SurveillancePatrol::State::Dispatched:
        UpdateDispatch(player, deltaTime);
        break;
    case SurveillancePatrol::State::Completed:
        break;
    }
}

// 映像の操作：左右でカメラを切り替え、C（Y）で基準映像、Z（X）で拡大、E（A）で異常あり、Q（B）で異常なしを報告している
void StageSurveillanceController::UpdateViewing(Player& player)
{
    m_WrongTimer = (std::max)(
        0.0f, m_WrongTimer - Application::GetDeltaTime());
    if (m_ReferenceCapturePending ||
        m_ViewTimer < PatrolInputDelay || m_WrongTimer > 0.0f)
    {
        return;
    }

    if (Input::GetKeyTrigger(VK_LEFT) ||
        Input::GetButtonTrigger(XINPUT_LEFT) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_Patrol.SelectCamera(-1, StageSurveillanceCameraCount);
        Input::SetVibration(1, 0.03f);
    }
    else if (Input::GetKeyTrigger(VK_RIGHT) ||
        Input::GetButtonTrigger(XINPUT_RIGHT) ||
        Input::GetButtonTrigger(XINPUT_RIGHT_SHOULDER))
    {
        m_Patrol.SelectCamera(1, StageSurveillanceCameraCount);
        Input::SetVibration(1, 0.03f);
    }

    if (Input::GetKeyTrigger(VK_C) || Input::GetButtonTrigger(XINPUT_Y))
    {
        m_ShowReference = !m_ShowReference;
        // 記録した映像は標準の画角なので、切り替えるときにライブも同じ画角へ戻している。
        m_Zoomed = false;
        Input::SetVibration(1, 0.03f);
    }

    // 広い画角で場所を把握し、必要なときだけ中央を拡大して小さな変化を調べられるようにしている。
    if (!m_ShowReference &&
        (Input::GetKeyTrigger(VK_Z) || Input::GetButtonTrigger(XINPUT_X)))
    {
        m_Zoomed = !m_Zoomed;
        Input::SetVibration(1, 0.03f);
    }

    // 基準映像を見ながら間違えて報告しないよう、報告はライブの画面でだけ受け付けている。
    if (m_ShowReference)
    {
        return;
    }

    SurveillancePatrol::ReportResult result =
        SurveillancePatrol::ReportResult::Ignored;
    if (Input::GetKeyTrigger(VK_E) || Input::GetButtonTrigger(XINPUT_A))
    {
        result = m_Patrol.ReportSelectedCamera();
    }
    else if (Input::GetKeyTrigger(VK_Q) || Input::GetButtonTrigger(XINPUT_B))
    {
        result = m_Patrol.ReportNoAnomaly();
    }

    Core::Game* game = Core::Game::GetInstance();
    switch (result)
    {
    case SurveillancePatrol::ReportResult::Ignored:
        break;
    case SurveillancePatrol::ReportResult::Dispatched:
        m_WarningCooldown = 0.0f;
        EndViewing(player);
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.62f);
        break;
    case SurveillancePatrol::ReportResult::ClearedNoAnomaly:
        EndViewing(player);
        if (m_Patrol.IsCompleted())
        {
            Complete(player);
        }
        else
        {
            ShowNotice("異常なしを確認した", 2.2f);
        }
        break;
    case SurveillancePatrol::ReportResult::Wrong:
        game->RegisterPuzzleMistake();
        m_WrongTimer = PatrolWrongFeedbackSeconds;
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.54f);
        game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.24f);
        Input::SetVibration(5, 0.11f);
        break;
    case SurveillancePatrol::ReportResult::Caught:
        game->RegisterPuzzleMistake();
        StartCaught(player);
        break;
    }
}

// 現地確認：照らし続けたら対処、時間切れなら間違い、間違いが上限なら捕まる
void StageSurveillanceController::UpdateDispatch(Player& player, float deltaTime)
{
    const SurveillancePatrol::Anomaly anomaly = m_Patrol.GetAnomaly();
    const SurveillancePatrol::DispatchResult result =
        m_Patrol.UpdateDispatch(deltaTime, IsIlluminatingAnomaly(player));

    Core::Game* game = Core::Game::GetInstance();
    switch (result)
    {
    case SurveillancePatrol::DispatchResult::None:
        // 残り時間が少ないほど、端末から離れている不安を画面と振動で強めている。
        m_WarningCooldown -= deltaTime;
        if (m_Patrol.GetRemainingTime() <= PatrolWarningTime &&
            m_WarningCooldown <= 0.0f)
        {
            m_WarningCooldown = 3.0f;
            game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.30f);
            Input::SetVibration(4, 0.08f);
        }
        break;
    case SurveillancePatrol::DispatchResult::Resolved:
        SetAnomalyVisible(anomaly, false);
        game->RegisterAnomalyHandled();
        game->GetPostProcess()->TriggerBloomPulse(0.36f, 0.18f);
        Input::SetVibration(4, 0.09f);
        if (m_Patrol.IsCompleted())
        {
            Complete(player);
        }
        else
        {
            ShowNotice("異常を確認した 端末へ戻る", 2.6f);
            if (FuseBox* terminal = m_Objects->evidenceTerminal)
            {
                terminal->SetManualInteractionAllowed(true);
            }
        }
        break;
    case SurveillancePatrol::DispatchResult::TimedOut:
        SetAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        ShowNotice("映像の反応が途絶えた 端末へ戻る", 2.8f);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.62f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.30f);
        if (FuseBox* terminal = m_Objects->evidenceTerminal)
        {
            terminal->SetManualInteractionAllowed(true);
        }
        break;
    case SurveillancePatrol::DispatchResult::Caught:
        SetAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        StartCaught(player);
        break;
    }
}

// 異常をランダムに選んでいる（25%は異常なし）
SurveillancePatrol::Anomaly StageSurveillanceController::ChooseAnomaly()
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(m_Random) < PatrolNoAnomalyChance)
    {
        return SurveillancePatrol::Anomaly{};
    }

    std::uniform_int_distribution<int> cameraDistribution(
        0, StageSurveillanceCameraCount - 1);
    SurveillancePatrol::Anomaly anomaly;
    anomaly.camera = cameraDistribution(m_Random);

    // カメラごとに、その場所で起こせる異常だけを候補にしている。
    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    SurveillancePatrol::AnomalyType candidates[3] =
    {
        SurveillancePatrol::AnomalyType::Figure
    };
    int candidateCount = 1;
    CeilingLight* anomalyLight = camera.LightNumber > 0
        ? m_Objects->CeilingLightAt(camera.LightNumber)
        : nullptr;
    // 停電中にもともと消えている普通の照明を選ぶと映像に差が出ないため、
    // 電力が戻る前は、点いている非常灯だけを消灯の異常の候補にしている。
    if (anomalyLight != nullptr &&
        (Core::Game::GetInstance()->IsPowerRestored() ||
         anomalyLight->IsEmergencyLight()))
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::LightOut;
    }
    if (camera.SealedDoorIndex >= 0)
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::DoorOpen;
    }
    std::uniform_int_distribution<int> typeDistribution(0, candidateCount - 1);
    anomaly.type = candidates[typeDistribution(m_Random)];
    return anomaly;
}

// 異常の見た目を出したり消したりしている（人影を立たせる・照明を消す・開かずの扉を開ける）
void StageSurveillanceController::SetAnomalyVisible(
    const SurveillancePatrol::Anomaly& anomaly, bool visible)
{
    if (!anomaly.Exists())
    {
        return;
    }

    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    switch (anomaly.type)
    {
    case SurveillancePatrol::AnomalyType::Figure:
        if (ShadowMan* figure = m_Objects->evidenceShadow)
        {
            figure->SetActive(false);
            if (visible)
            {
                const Vector3 position = ToVector3(camera.FigurePosition);
                figure->SetPosition(position.x, position.y, position.z);
                figure->SetActive(true);
                // 現地確認の制限時間より長く残し、見つける前に消えないようにしている。
                figure->EnableGazeScare(
                    SurveillancePatrol::DispatchTimeLimit + 60.0f);
            }
        }
        break;
    case SurveillancePatrol::AnomalyType::LightOut:
        if (CeilingLight* light = m_Objects->CeilingLightAt(camera.LightNumber))
        {
            light->SetForcedOff(visible);
        }
        break;
    case SurveillancePatrol::AnomalyType::DoorOpen:
        if (Door* door = m_Objects->sealedDoors[
            static_cast<std::size_t>(camera.SealedDoorIndex)])
        {
            if (visible)
            {
                door->ForceOpen();
            }
            else
            {
                door->ResetClosed(0);
                door->SetLocked(true);
            }
        }
        break;
    case SurveillancePatrol::AnomalyType::None:
        break;
    }
}

// ライトを点けて、異常の場所を110以内から視線の中心（内積0.9超）で照らしているかを返している
bool StageSurveillanceController::IsIlluminatingAnomaly(const Player& player) const
{
    // ライトを点けて自分で異常を探す操作を必須にし、現地に着くだけでは完了させていない。
    if (!player.IsFlashlightOn())
    {
        return false;
    }
    const SurveillancePatrol::Anomaly& anomaly = m_Patrol.GetAnomaly();
    if (!anomaly.Exists())
    {
        return false;
    }

    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    Vector3 target;
    switch (anomaly.type)
    {
    case SurveillancePatrol::AnomalyType::Figure:
        target = ToVector3(camera.FigurePosition) + Vector3(0.0f, 17.0f, 0.0f);
        break;
    case SurveillancePatrol::AnomalyType::LightOut:
        target = m_Objects->CeilingLightAt(camera.LightNumber)->GetPosition();
        break;
    case SurveillancePatrol::AnomalyType::DoorOpen:
        target = ToVector3(StageSealedDoors[camera.SealedDoorIndex].Position);
        break;
    case SurveillancePatrol::AnomalyType::None:
        return false;
    }

    // 光が届く距離まで近づき、ライトの中心で照らしている場合だけ対処を進めている。
    Camera* viewCamera = Core::Game::GetInstance()->GetCamera();
    Vector3 toTarget = target - viewCamera->GetPosition();
    const float distance = toTarget.Length();
    if (distance > PatrolLookDistance || distance < 0.001f)
    {
        return false;
    }
    toTarget /= distance;
    return viewCamera->GetForward().Dot(toTarget) > PatrolLookAlignment;
}

// 映像を閉じ、プレイヤーを動けるようにしている
void StageSurveillanceController::EndViewing(Player& player)
{
    player.SetCanControl(true);
    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->ResetActivation();
        // 現地確認の間は端末を操作できないようにし、確かめて戻ってから次の映像を見せている。
        terminal->SetManualInteractionAllowed(
            m_Patrol.GetState() == SurveillancePatrol::State::Idle);
    }
}

// 捕まる演出を始めている（異常の見た目を消し、驚かせて暗転させる）
void StageSurveillanceController::StartCaught(Player& player)
{
    SetAnomalyVisible(m_Patrol.GetAnomaly(), false);
    if (!m_Caught.Start(CaughtSequence::Reason::FinalPursuit))
    {
        return;
    }

    player.SetCanControl(false);
    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    Input::SetVibration(24, 0.72f);
}

void StageSurveillanceController::UpdateCaught(Player& player, float deltaTime)
{
    m_Caught.Advance(deltaTime);
    if (!m_Caught.IsReadyToRecover())
    {
        return;
    }

    // 暗転の間に端末の前へ戻し、巡回をやり直せる状態にしている（対処した回数はそのまま残している）。
    m_Caught.Complete();
    player.SetPosition(Vector3(190.0f, -99.0f, -42.0f));
    player.SetCanControl(true);
    if (ShadowMan* figure = m_Objects->evidenceShadow)
    {
        figure->SetActive(false);
    }
    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->ResetActivation();
        terminal->SetManualInteractionAllowed(true);
    }
    ShowNotice("気がつくと端末の前にいた", 3.0f);
}

void StageSurveillanceController::Complete(Player& player)
{
    // 記録端末の知らせはSceneが出すため、ここでは完了したことだけを伝えている。
    m_CompletedThisFrame = true;
    // ごほうびに電池を少し回復し、端末の目印を緑に変えて、記録を回収したことにしている
    player.AddBattery(8.0f);

    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->SetManualInteractionAllowed(false);
    }
    if (Wall* marker = m_Objects->evidenceMarker)
    {
        marker->SetAppearance(
            Color(0.08f, 0.18f, 0.10f, 1.0f),
            Color(0.16f, 0.52f, 0.22f, 1.0f),
            44.0f);
    }
    if (CeilingLight* roomLight = m_Objects->CeilingLightAt(3))
    {
        roomLight->TriggerEventFlicker(0.84f, 0.92f);
    }

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterEvidenceCollected();
    game->PlayAudioCue(SOUND_CUE_SCARE, 0.76f);
    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.32f);
    game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.20f);
    Input::SetVibration(9, 0.19f);
}

// 知らせの文章と、表示する秒数を決めている
void StageSurveillanceController::ShowNotice(const char* text, float seconds)
{
    m_NoticeText = text;
    m_NoticeTimer = seconds;
}
// 映像を見ている間だけ、監視カメラの視点で世界を描いている（本描画の前に呼ばれている）
void StageSurveillanceController::RenderFeeds()
{
    if (m_Patrol.GetState() != SurveillancePatrol::State::Viewing)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    if (camera == nullptr)
    {
        return;
    }

    // 本編のライトと点光源を覚えておき、描き終えたら元に戻している
    const LIGHT previousLight = Renderer::GetLight();
    Effect::TiledLighting* tiledLighting = game->GetTiledLighting();
    const std::vector<ENVIRONMENT_POINT_LIGHT> previousPointLights =
        tiledLighting->GetLights();
    // 1台のカメラの視点で描いている（拡大なら画角32度、普段は56度）
    const auto drawCamera = [&](int index, bool zoomed,
        Graphics::RenderTexture& output)
    {
        const StageSurveillanceCamera& spot = StageSurveillanceCameras[index];
        const Vector3 cameraPosition(
            spot.Position[0], spot.Position[1], spot.Position[2]);
        const Vector3 cameraTarget(
            spot.Target[0], spot.Target[1], spot.Target[2]);
        const Matrix view = Matrix::CreateLookAt(
            cameraPosition, cameraTarget, Vector3::Up);
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            DirectX::XMConvertToRadians(zoomed ? 32.0f : 56.0f),
            16.0f / 9.0f, 1.0f, 420.0f);

        output.SetRenderTarget();
        output.Clear(0.018f, 0.028f, 0.022f, 1.0f);
        camera->SetOverrideMatrices(view, projection);

        // 本編の照明は変えず、監視映像だけに補助の環境光を使っている（懐中電灯の光は映さない）。
        LIGHT surveillanceLight = previousLight;
        surveillanceLight.Enable = TRUE;
        surveillanceLight.FlashlightEnabled = FALSE;
        surveillanceLight.Ambient = Color(0.16f, 0.19f, 0.17f, 1.0f);
        Renderer::SetLight(surveillanceLight);

        // 光源の数の上限が大きくなったため、今の照明を押し出さずに補助の光を足せる。
        std::vector<ENVIRONMENT_POINT_LIGHT> surveillanceLights = previousPointLights;
        ENVIRONMENT_POINT_LIGHT helper{};
        helper.PositionRange = Vector4(
            cameraPosition.x, cameraPosition.y, cameraPosition.z, 260.0f);
        helper.ColorIntensity = Vector4(0.72f, 0.92f, 0.78f, 0.52f);
        surveillanceLights.push_back(helper);
        tiledLighting->SetLights(surveillanceLights);

        game->DrawWorldForAuxiliaryCamera(*camera);
    };

    if (m_ReferenceCapturePending)
    {
        // 1フレームに1台ずつ記録し、端末を開いた瞬間の描画の負荷を分散している。全部記録したら異常の見た目を出している。
        drawCamera(m_ReferenceCaptureIndex, false,
            m_References[m_ReferenceCaptureIndex]);
        ++m_ReferenceCaptureIndex;
        if (m_ReferenceCaptureIndex == StageSurveillanceCameraCount)
        {
            SetAnomalyVisible(m_Patrol.GetAnomaly(), true);
            m_ReferenceCapturePending = false;
        }
    }

    // 選んでいるカメラのライブ映像を描いている
    drawCamera(m_Patrol.GetSelectedCamera(), m_Zoomed,
        m_Feed);
    Renderer::SetLight(previousLight);
    tiledLighting->SetLights(previousPointLights);
    camera->ClearOverrideMatrices();
    Renderer::SetBackBufferRenderTarget();
}
