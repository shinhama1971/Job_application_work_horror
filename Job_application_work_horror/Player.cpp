// ============================================================================
// ファイルの役割: 一人称の移動、視点、懐中電灯、電池、足音、ロッカーに隠れる操作を管理している。
// 主な技術: 一人称の操作、壁との当たり判定と押し戻し、歩くときの頭の揺れ、スポットライト、キーボードとコントローラーの入力の統合
// ============================================================================

#include "Player.h"
#include "MathUtility.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Camera.h"
#include "Renderer.h"
#include "Wall.h"
#include "Door.h"
#include "CeilingLight.h"
#include "Ground.h"
#include "FirstPersonHands.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 作った直後の目の高さ（実際の位置は Scene が配置に合わせて設定し直す）
    constexpr float InitialEyeY = -85.0f;
    // 照り返しの光がこれより弱ければ出さない
    constexpr float MinimumBounceIntensity = 0.01f;
    // 懐中電灯のスイッチの音の高さ
    constexpr float FlashlightClickPitch = 0.94f;
    // ロッカーから出るときの、高くきしむ音（高さと音量）
    constexpr float LockerCreakPitch = 1.35f;
    constexpr float LockerCreakVolume = 0.55f;
    // 階段を1段上るときの振動
    constexpr int StairStepRumbleFrames = 2;
    constexpr float StairStepRumbleStrength = 0.05f;
}

namespace
{
    // --- 足音
    // 押し戻しの後に、1秒あたりこれより速く動けたときだけ鳴らす（壁へ押し続けても鳴らさない）
    constexpr float FootstepMinSpeed = 4.8f;
    // 鳴らす間隔（歩く・走る）
    constexpr float WalkFootstepInterval = 0.46f;
    constexpr float SprintFootstepInterval = 0.31f;
    // 左右の足で少し高さを変える（歩く・走る）。水の中では少し低くする
    constexpr float WalkFootstepPitchLeft = 0.97f;
    constexpr float WalkFootstepPitchRight = 1.03f;
    constexpr float SprintFootstepPitchLeft = 1.08f;
    constexpr float SprintFootstepPitchRight = 1.14f;
    constexpr float WetFootstepPitchRate = 0.92f;
    // 足音の大きさ（2面の影が反応する）：水の中を走る・水の中を歩く・乾いた床を走る（乾いた床を歩くのは0）
    constexpr float WetSprintNoise = 1.0f;
    constexpr float WetWalkNoise = 0.62f;
    constexpr float DrySprintNoise = 0.32f;

    // --- 懐中電灯
    // 照らす距離と、光の強さ
    constexpr float FlashlightRange = 275.0f;
    constexpr float FlashlightIntensity = 1.50f;
    // 光の形：内側の角度までは全部の明るさ、外側の角度で消え、その間はこの乗数で暗くする
    constexpr float FlashlightInnerConeDegrees = 14.0f;
    constexpr float FlashlightOuterConeDegrees = 32.0f;
    constexpr float FlashlightConeFalloff = 1.8f;
    // 近い面を探す一番遠い距離と、近い面で光を弱め始める距離・弱めきる距離までの幅
    constexpr float FlashlightSurfaceSearchDistance = 70.0f;
    constexpr float FlashlightNearSurfaceDistance = 12.0f;
    constexpr float FlashlightNearSurfaceRange = 42.0f;
    // 近い面で弱める明るさの割合（最大）と、縮める照らす距離
    constexpr float FlashlightNearSurfaceDim = 0.20f;
    constexpr float FlashlightNearSurfaceRangeCut = 48.0f;
    // 電池がこの残量（%）を切ると弱り始め、空で明るさ・照らす距離をこの割合だけ下げる
    constexpr float FlashlightWeakBattery = 60.0f;
    constexpr float FlashlightWeakDim = 0.30f;
    constexpr float FlashlightWeakRangeCut = 0.18f;

    // --- 三人称（デバッグ用）の視点：プレイヤーの後ろ・上の距離と、見る高さ
    constexpr float ThirdPersonDistance = 45.0f;
    constexpr float ThirdPersonHeight = 28.0f;
    // 開発用の電池の回復量（Debug と 3面の確認中の B キー）
    constexpr float DebugBatteryRefill = 50.0f;

    // --- 階段を上る演出
    // 1段の奥行きのうち、最初のこの割合で次の段の高さへ上がりきる（残りは段の上を平らに歩く）
    constexpr float ClimbLiftFraction = 0.4f;
    // 段を踏むたびに目が沈む深さ、歩く向きへ視点を向けるのにかける秒数
    constexpr float ClimbStepBob = 0.7f;
    constexpr float ClimbTurnSeconds = 0.8f;
    // 足音の高さ（左右の足）と、段の上で下げる量、音量（段の上・平らな所）
    constexpr float ClimbFootstepPitchLeft = 0.90f;
    constexpr float ClimbFootstepPitchRight = 0.95f;
    constexpr float ClimbStairPitchDrop = 0.06f;
    constexpr float ClimbStairFootstepVolume = 1.15f;
    constexpr float ClimbFlatFootstepVolume = 0.95f;

    // --- 移動：壁と扉で押し戻す回数（部屋の角で隣の壁へめり込んだ場合も判定し直すため2回）、スタミナが切れた瞬間の振動
    constexpr int CollisionPasses = 2;
    constexpr int StaminaOutRumbleFrames = 5;
    constexpr float StaminaOutRumbleStrength = 0.10f;

    // --- 懐中電灯のスイッチの音の高さ（点ける・消す）
    constexpr float FlashlightOnClickPitch = 1.08f;
    constexpr float FlashlightOffClickPitch = 0.94f;
    // 電池の残りが少なくなったときの知らせ（20%を切った・10%を切った＝一番強い知らせの段階）
    constexpr int BatteryCriticalWarning = 2;
    constexpr float BatteryLowPulseStrength = 0.12f;
    constexpr float BatteryLowPulseSeconds = 0.22f;
    constexpr int BatteryLowRumbleFrames = 4;
    constexpr float BatteryLowRumbleStrength = 0.09f;
    constexpr float BatteryCriticalPulseStrength = 0.26f;
    constexpr float BatteryCriticalPulseSeconds = 0.34f;
    constexpr int BatteryCriticalRumbleFrames = 8;
    constexpr float BatteryCriticalRumbleStrength = 0.18f;
    // 電圧が一瞬落ちたときの知らせ（基本＋電池の弱り1のときに足す分）
    constexpr float VoltageDropPulseStrengthBase = 0.055f;
    constexpr float VoltageDropPulseStrengthStress = 0.095f;
    constexpr float VoltageDropPulseSecondsBase = 0.14f;
    constexpr float VoltageDropPulseSecondsStress = 0.08f;
    constexpr int VoltageDropRumbleFramesBase = 2;
    constexpr float VoltageDropRumbleFramesStress = 4.0f;
    constexpr float VoltageDropRumbleStrengthBase = 0.035f;
    constexpr float VoltageDropRumbleStrengthStress = 0.065f;

    // --- 懐中電灯の光線：照り返しの点光源を置く、光が最初に当たる面までの一番遠い距離。下を向いているとみなす傾き
    constexpr float FlashlightBounceReach = 180.0f;
    constexpr float FlashlightFloorRayMinSlope = 0.001f;
    // なめらかに近づける割合は「60fpsの1フレームでこの割合だけ近づく」で決め、フレームの長さに合わせて直している
    constexpr float ReferenceFramesPerSecond = 60.0f;
    constexpr float FlashlightNearSurfaceFollowPerFrame = 0.18f;
    // 電池の弱りで光の色を暖かくする量（赤を少し上げ、緑と青を下げる。Stress は電圧の揺れ、Weak は残量の少なさ）
    constexpr float FlashlightStressRedGain = 0.04f;
    constexpr float FlashlightStressGreenLoss = 0.06f;
    constexpr float FlashlightWeakGreenLoss = 0.10f;
    constexpr float FlashlightStressBlueLoss = 0.18f;
    constexpr float FlashlightWeakBlueLoss = 0.32f;
    // 懐中電灯の外の環境光：点けている間は光の外を暗く沈め、消すと照明の光だまりの外はほとんど闇にする。電力が戻ると少し明るい
    const Color FlashlightOnAmbient(0.070f, 0.072f, 0.072f, 1.0f);
    const Color FlashlightOffAmbient(0.045f, 0.047f, 0.050f, 1.0f);
    const Color PoweredFlashlightOnAmbient(0.095f, 0.097f, 0.100f, 1.0f);
    const Color PoweredFlashlightOffAmbient(0.070f, 0.072f, 0.076f, 1.0f);

    // --- 懐中電灯の照り返し：明るさ（懐中電灯の明るさに対する割合、一番遠いときの割合）、面から手前に置く距離と一番近い距離、
    // 置ける一番高い所、届く範囲（基本＋面までの距離に比例）、跳ね返った光の色、位置と明るさを近づける割合
    constexpr float BounceIntensityScale = 0.42f;
    constexpr float BounceFarIntensity = 0.25f;
    constexpr float BounceSurfaceOffset = 10.0f;
    constexpr float BounceMinDistance = 2.0f;
    constexpr float BounceMaxY = -59.0f;
    constexpr float BounceRangeBase = 75.0f;
    constexpr float BounceRangePerDistance = 0.45f;
    constexpr float BounceGreenTint = 0.94f;
    constexpr float BounceBlueTint = 0.84f;
    constexpr float BouncePositionFollowPerFrame = 0.25f;
    constexpr float BounceIntensityFollowPerFrame = 0.20f;

    // --- 手を照らす跳ね返りの光：面までの距離をこの長さで割った比で弱める。何もなくても床から返る分と、面が近いときに足す分
    constexpr float HandBounceSurfaceScale = 16.0f;
    constexpr float HandBounceBase = 0.04f;
    constexpr float HandBounceNear = 0.30f;

    // --- 目の高さの、呼吸のようなゆっくりした揺れ（速さ・高さ）
    constexpr float EyeBreathRadiansPerSecond = 1.15f;
    constexpr float EyeBreathHeight = 0.075f;

    // --- 体の影：一歩は約12なので、この距離を進むと手足の振りが1往復する。振りの大きさが歩く速さに追いつく速さ
    constexpr float BodyWalkCycleDistance = 24.0f;
    constexpr float BodyWalkAmountResponse = 8.0f;
}

// 描画資源とプレイヤーの状態を初期化している。配置する座標はScene側がInitの後に設定している。
void Player::Init()
{
    StaticMesh staticmesh;

    // 三人称のデバッグ表示でだけ見える仮のモデル（一人称では描かず、影・反射にも映らない）。
    // 人型のモデルに差し替える予定にしている。
    std::u8string modelFile = u8"assets/model/golf_ball/golf_ball.obj";
    std::string texDirectory = "assets/model/golf_ball";

    // u8文字列のパスを、Assimpに渡せるstd::stringへ変換している
    std::string tmpStr(
        reinterpret_cast<const char*>(modelFile.c_str()),
        modelFile.size()
    );

    staticmesh.Load(tmpStr, texDirectory);

    SetModelBounds(staticmesh.GetModelBounds());
    m_MeshRenderer.Init(staticmesh);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_subsets = staticmesh.GetSubsets();
    m_Textures = staticmesh.GetTextures();

    // モデルのマテリアルごとにMaterialを作り、同じ番号のテクスチャを貼っている
    std::vector<MATERIAL> materials = staticmesh.GetMaterials();

    for (int i = 0; i < materials.size(); i++)
    {
        std::unique_ptr<Material> m = std::make_unique<Material>();
        m->CreateRequired(materials[i]);
        m->SetShader(&m_Shader);
        m_Materials.push_back(std::move(m));
    }

    size_t count = (std::min)(m_Materials.size(), m_Textures.size());

    for (size_t i = 0; i < count; i++)
    {
        m_Materials[i]->SetTexture(m_Textures[i].get());
    }

    // 移動・懐中電灯・足音の状態を初期化している
    m_Position = Vector3(0.0f, InitialEyeY, 0.0f);
    m_Scale = Vector3(1.0f, 1.0f, 1.0f);
    m_Movement.Initialize();
    m_Flashlight.InitializeRuntimeNotifications();
    m_FlashlightNearSurfaceBlend = 0.0f;
    m_FootstepTimer = 0.0f;
    m_FootstepIndex = 0;
    m_SurfaceNoisePulse = 0.0f;
    m_Hands.Init();
}

// 入力・移動・当たり判定・カメラ・懐中電灯・電池の消費を、1フレーム分更新している。
// 操作できない間もカメラとライトの状態は崩さないようにし、演出から戻りやすくしている。段階ごとの中身は、この下の関数に分けている。
void Player::Update()
{
    // Scene::UpdateはObject::Updateより先に呼ばれるため、前フレームの値を
    // Sceneが読んだ後で、今回の足音の大きさを作り直している。
    m_SurfaceNoisePulse = 0.0f;
    // 階段を自動で上っている間は、操作できない演出の最中でも、歩かせてカメラを段の高さに置いている
    if (m_Climb.Active)
    {
        UpdateStairClimb(Application::GetDeltaTime());
        return;
    }
    if (!m_CanControl)
    {
        m_Movement.StopControl();
        // 操作できない演出の間は、手を下げて画面の外へ出している
        FirstPersonHands::FrameInput handInput;
        handInput.DeltaTime = Application::GetDeltaTime();
        handInput.Lowered = true;
        m_Hands.Update(handInput);
        return;
    }

    Camera* cam = Core::Game::GetInstance()->GetCamera();
    const float deltaTime = Application::GetDeltaTime();
    m_AmbienceTimer += deltaTime;
    m_Flashlight.TickNotice(deltaTime);

    // ロッカーに隠れている間は、見回せる向きを制限し、出る操作を受け付けている。
    if (m_IsHiding)
    {
        UpdateHiding(*cam, deltaTime);
    }
    else
    {
        m_HidingInputTimer = (std::max)(0.0f, m_HidingInputTimer - deltaTime);
    }

    // カメラの水平の向きから、前と右の方向を作っている（WASDと左スティックで移動の向きを決める）
    const float yaw = cam->GetCameraDirection();
    const Vector3 forward(sinf(yaw), 0.0f, cosf(yaw));
    const Vector3 right(sinf(yaw + MathUtility::HalfPi), 0.0f, cosf(yaw + MathUtility::HalfPi));

    const std::vector<Wall*> walls =
        Core::Game::GetInstance()->GetObjects<Wall>();
    const std::vector<Door*> doors =
        Core::Game::GetInstance()->GetObjects<Door>();
    const Vector3 positionBeforeMove = MoveAndCollide(ReadMoveInput(forward, right), walls, doors, deltaTime);
    UpdateFootsteps(positionBeforeMove, deltaTime);

// デバッグ用：一人称と三人称の切り替え（Debug構成で、さらにENABLE_CAMERA_MODE_SHORTCUTSを定義したときだけ）
#if defined(_DEBUG) && defined(ENABLE_CAMERA_MODE_SHORTCUTS)
    if (Input::GetKeyTrigger(VK_R))
    {
        m_IsFPS = true;
    }

    if (Input::GetKeyTrigger(VK_T))
    {
        m_IsFPS = false;
    }

    if (Input::GetButtonTrigger(XINPUT_RIGHT_THUMB))
    {
        m_IsFPS = !m_IsFPS;
    }
#endif

    // 懐中電灯：スイッチと電池、光が当たる面、シェーダーへ渡す光、照り返し
    const FlashlightSystem::FrameState flashlightState = UpdateFlashlightPower(deltaTime);
    const bool renderFlashlight = flashlightState.render;
    const FlashlightBeam beam = TraceFlashlightBeam(*cam, yaw, walls, flashlightState.visible);
    // 電池の弱り具合（60%を切ると0から増え、空で1）。弱るほど光が暗く・黄色く・短くなり、光の輪のむらがはっきりする
    const float batteryWeakness = (std::clamp)((FlashlightWeakBattery - m_Flashlight.GetBattery()) / FlashlightWeakBattery, 0.0f, 1.0f);
    const LIGHT light = BuildFlashlightLight(flashlightState, beam.ClosestSurfaceDistance, batteryWeakness, deltaTime);
    Renderer::SetLight(light);
    UpdateFlashlightBounce(light, renderFlashlight, beam, deltaTime);

    // 手に持った懐中電灯：レンズの光と、前の面で跳ね返って手を照らす光（面が近いほど強い。何もなくても床から少し返る）
    FirstPersonHands::FrameInput handInput;
    handInput.DeltaTime = deltaTime;
    handInput.LensGlow = renderFlashlight ? flashlightState.output * flashlightState.powerBlend : 0.0f;
    handInput.BatteryWeakness = batteryWeakness;
    handInput.BatteryStress = flashlightState.batteryStress;
    handInput.VoltageDrop = flashlightState.voltageDropStarted;
    handInput.NearSurface = m_FlashlightNearSurfaceBlend;
    if (renderFlashlight)
    {
        const float surfaceRatio = beam.ClosestSurfaceDistance / HandBounceSurfaceScale;
        const float bounce = light.Intensity * (HandBounceBase + HandBounceNear / (1.0f + surfaceRatio * surfaceRatio));
        handInput.Bounce = Vector3(light.Diffuse.x, light.Diffuse.y, light.Diffuse.z) * bounce;
    }

    // カメラの位置と向きを更新している（歩くときの頭の揺れを含む）
    m_Movement.UpdateHeadBob(deltaTime);
    UpdateCameraPosition(*cam, forward, right);

    // 手の揺れ：視点の向き・頭の揺れ・走り・スイッチ。ロッカーの中では手を下げている
    handInput.Yaw = yaw;
    handInput.Pitch = cam->GetCameraPitch();
    handInput.HeadBobVertical = m_Movement.GetHeadBobOffset();
    handInput.HeadBobSide = m_Movement.GetHeadBobSideOffset();
    handInput.Sprinting = m_Movement.IsSprinting() && m_Movement.IsMovingHorizontally();
    handInput.Toggled = m_FlashlightToggledThisFrame;
    handInput.Lowered = m_IsHiding || !m_IsFPS || m_AttractMode;
    m_Hands.Update(handInput);
    m_FlashlightToggledThisFrame = false;

    UpdateBodyShadow(forward, deltaTime);
    HandleDebugBatteryKey();
}

// WASDと左スティックから、移動したい向き（水平）を作っている。
// タイトル画面の後ろで映している間（m_AttractMode）と、隠れている間は動かない（タイトルの面の選択の A・D で動かないように）
Vector3 Player::ReadMoveInput(const Vector3& forward, const Vector3& right) const
{
    Vector3 moveDir = Vector3::Zero;
    if (m_AttractMode || m_IsHiding)
    {
        return moveDir;
    }
    if (Input::GetKeyPress(VK_W)) moveDir += forward;
    if (Input::GetKeyPress(VK_S)) moveDir -= forward;
    if (Input::GetKeyPress(VK_A)) moveDir -= right;
    if (Input::GetKeyPress(VK_D)) moveDir += right;

    const DirectX::XMFLOAT2 leftStick = Input::GetLeftAnalogStick();
    moveDir += right * leftStick.x;
    moveDir += forward * leftStick.y;
    return moveDir;
}

// 速さを更新して位置を動かし、壁と扉で押し戻している。動く前の位置を返している（足音で、実際に動けた距離を測るため）
Vector3 Player::MoveAndCollide(const Vector3& moveDir, const std::vector<Wall*>& walls,
    const std::vector<Door*>& doors, float deltaTime)
{
    // Shift（左スティックの押し込み）で走る。走れなくなった瞬間（スタミナ切れ）は振動で知らせている
    const bool wantsToSprint = !m_IsHiding && !m_AttractMode && m_SprintAllowed &&
        (Input::GetKeyPress(VK_SHIFT) ||
            Input::GetButtonPress(XINPUT_LEFT_THUMB));
    if (m_Movement.Update(moveDir, wantsToSprint, deltaTime))
    {
        Input::SetVibration(StaminaOutRumbleFrames, StaminaOutRumbleStrength);
    }

    // 速度で位置を動かし、床より下には行かないようにしている
    const Vector3 positionBeforeMove = m_Position;
    m_Position += m_Movement.GetVelocity() * deltaTime;

    if (m_Position.y < MIN_Y_POSITION)
    {
        m_Position.y = MIN_Y_POSITION;
        m_Movement.StopVerticalVelocity();
    }

    // 壁による押し戻しを2回行い、部屋の角で隣の壁へめり込んだ場合も判定し直している。
    for (int pass = 0; pass < CollisionPasses; ++pass)
    {
        for (const Wall* wall : walls)
        {
            wall->ResolveCollision(m_Position, m_Radius);
        }
        for (const Door* door : doors)
        {
            door->ResolveCollision(m_Position, m_Radius);
        }
    }

    // 隠れている間はロッカーの中に固定している（ロッカーの本体は壁なので、押し戻しで外に出されないよう、後から戻している）。
    if (m_IsHiding)
    {
        m_Position = m_HidePosition;
    }
    return positionBeforeMove;
}

// 押し戻しの後に実際に動けた距離を使うため、壁へ押し続けても足音は鳴らない。
void Player::UpdateFootsteps(const Vector3& positionBeforeMove, float deltaTime)
{
    m_FootstepTimer = (std::max)(0.0f, m_FootstepTimer - deltaTime);
    const Vector3 actualMovement(
        m_Position.x - positionBeforeMove.x,
        0.0f,
        m_Position.z - positionBeforeMove.z);
    const float footstepMovementThreshold = FootstepMinSpeed * deltaTime;
    if (actualMovement.LengthSquared() <=
        footstepMovementThreshold * footstepMovementThreshold ||
        m_FootstepTimer > 0.0f)
    {
        return;
    }

    // 波紋と同じ判定で、足元が水の中なら歩き・走りそれぞれの水音を鳴らす。
    const bool sprinting = m_Movement.IsSprinting();
    bool wetStep = false;
    for (Ground* ground : Core::Game::GetInstance()->GetObjects<Ground>())
    {
        wetStep = ground->TriggerFootstepRipple(
            m_Position, sprinting) || wetStep;
    }
    const float alternatingPitch = (m_FootstepIndex % 2 == 0)
        ? (sprinting ? SprintFootstepPitchLeft : WalkFootstepPitchLeft)
        : (sprinting ? SprintFootstepPitchRight : WalkFootstepPitchRight);
    // 外の森では、雨で濡れた道の足音（歩く・走るで別の音）にしている
    const bool inForest = Core::Game::GetInstance()->GetCurrentSceneName() == SceneName::Forest;
    SOUND_LABEL step = SOUND_CUE_FOOTSTEP;
    if (wetStep)
    {
        step = sprinting ? SOUND_CUE_WATER_RUN : SOUND_CUE_WATER_WALK;
    }
    else if (inForest)
    {
        step = sprinting ? SOUND_CUE_FOREST_RUN : SOUND_CUE_FOREST_STEP;
    }
    Core::Game::GetInstance()->PlayAudioCue(
        step, wetStep ? alternatingPitch * WetFootstepPitchRate : alternatingPitch);
    // 足音の大きさ：水の中なら大きく、走ればさらに大きい（2面の影が反応する）
    m_SurfaceNoisePulse = wetStep
        ? (sprinting ? WetSprintNoise : WetWalkNoise)
        : (sprinting ? DrySprintNoise : 0.0f);
    ++m_FootstepIndex;
    m_FootstepTimer = sprinting ? SprintFootstepInterval : WalkFootstepInterval;
}

// 懐中電灯のスイッチと電池：F（コントローラーはY）で点け消しし（隠れている間はできない）、電池を減らし、
// 20%・10%を切った瞬間と、電圧が一瞬落ちたときに画面と振動で知らせている。このフレームの光の状態を返している
FlashlightSystem::FrameState Player::UpdateFlashlightPower(float deltaTime)
{
    if (!m_IsHiding && !m_AttractMode && (Input::GetKeyTrigger(VK_F) ||
        Input::GetButtonTrigger(XINPUT_Y)))
    {
        if (m_Flashlight.Toggle())
        {
            m_FlashlightToggledThisFrame = true;
            Core::Game::GetInstance()->PlayAudioCue(
                SOUND_CUE_FLASHLIGHT,
                m_Flashlight.IsOn() ? FlashlightOnClickPitch : FlashlightOffClickPitch);
        }
    }

    const int warningLevel = m_Flashlight.UpdateBattery(deltaTime);
    if (warningLevel > 0)
    {
        const bool critical = warningLevel == BatteryCriticalWarning;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            critical ? BatteryCriticalPulseStrength : BatteryLowPulseStrength,
            critical ? BatteryCriticalPulseSeconds : BatteryLowPulseSeconds);
        Input::SetVibration(
            critical ? BatteryCriticalRumbleFrames : BatteryLowRumbleFrames,
            critical ? BatteryCriticalRumbleStrength : BatteryLowRumbleStrength);
    }
    // このフレームの光の強さと、一瞬暗くなる状態を受け取っている
    const FlashlightSystem::FrameState flashlightState =
        m_Flashlight.UpdateFrameState(deltaTime);
    if (flashlightState.voltageDropStarted)
    {
        const float batteryStress = flashlightState.batteryStress;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            VoltageDropPulseStrengthBase + batteryStress * VoltageDropPulseStrengthStress,
            VoltageDropPulseSecondsBase + batteryStress * VoltageDropPulseSecondsStress);
        Input::SetVibration(
            VoltageDropRumbleFramesBase + static_cast<int>(batteryStress * VoltageDropRumbleFramesStress),
            VoltageDropRumbleStrengthBase + batteryStress * VoltageDropRumbleStrengthStress);
    }
    return flashlightState;
}

// 目の位置から視線の方向へ線を伸ばし、光が最初に当たる壁・床までの距離を求めている
// （FlashlightSurfaceSearchDistance までは近い面で光を弱めるのに、FlashlightBounceReach までは照り返しの位置に使っている）
Player::FlashlightBeam Player::TraceFlashlightBeam(const Camera& camera, float yaw,
    const std::vector<Wall*>& walls, bool visibleLight) const
{
    FlashlightBeam beam;
    beam.Origin = m_Position;
    beam.Origin.y += m_CameraHeightOffset;
    beam.HitDistance = FlashlightBounceReach;
    beam.ClosestSurfaceDistance = FlashlightSurfaceSearchDistance;
    if (!visibleLight)
    {
        return beam;
    }

    const float cameraPitch = camera.GetCameraPitch();
    const float pitchCos = cosf(cameraPitch);
    beam.Direction = Vector3(
        sinf(yaw) * pitchCos,
        sinf(cameraPitch),
        cosf(yaw) * pitchCos);
    beam.Direction.Normalize();
    const Vector3 beamEnd =
        beam.Origin + beam.Direction * FlashlightBounceReach;

    for (const Wall* wall : walls)
    {
        if (wall == nullptr)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (wall->IntersectsInteractionSegment(
            beam.Origin, beamEnd, hitDistance))
        {
            beam.HitDistance = (std::min)(beam.HitDistance, hitDistance);
        }
    }

    // 床はWallとは別に描いているため、下を向いたときは床の高さの平面との距離も計算している。
    if (beam.Direction.y < -FlashlightFloorRayMinSlope)
    {
        const float floorDistance =
            (MIN_Y_POSITION - beam.Origin.y) / beam.Direction.y;
        if (floorDistance >= 0.0f)
        {
            beam.HitDistance = (std::min)(beam.HitDistance, floorDistance);
        }
    }
    beam.ClosestSurfaceDistance = (std::min)(beam.ClosestSurfaceDistance, beam.HitDistance);
    return beam;
}

// シェーダーへ渡す懐中電灯の光（スポットライト）と、懐中電灯の外の環境光を作っている。
// 壁や床が近いときは、懐中電灯の明るさを下げている（明るさが一定だと、近くの面が白く飛んで、材質の細かい模様が消えるため）
LIGHT Player::BuildFlashlightLight(const FlashlightSystem::FrameState& state, float closestSurfaceDistance,
    float batteryWeakness, float deltaTime)
{
    const bool renderFlashlight = state.render;
    const float batteryStress = state.batteryStress;

    // 近い面までの距離が12〜54の間で、近いほど光を弱め、照らす距離も短くしている（急に変わらないよう、なめらかに近づけている）
    const float nearSurfaceTarget = state.visible
        ? MathUtility::Proximity(closestSurfaceDistance, FlashlightNearSurfaceDistance, FlashlightNearSurfaceRange)
        : 0.0f;
    const float nearSurfaceResponse = 1.0f - std::pow(
        1.0f - FlashlightNearSurfaceFollowPerFrame, deltaTime * ReferenceFramesPerSecond);
    m_FlashlightNearSurfaceBlend +=
        (nearSurfaceTarget - m_FlashlightNearSurfaceBlend) *
        nearSurfaceResponse;

    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = renderFlashlight ? TRUE : FALSE;
    // 近い面で弱める量は最大2割。以前は4割強だったが、壁・小物の懐中電灯の拡散光に上限を付けて（flashlightLighting.hlsli の
    // GetFlashlightDiffuseResponse）近くの壁が白く飛ばなくなったため、弱めすぎて「壁に近づくと何も見えない」状態になっていた
    const float proximityExposure =
        1.0f - m_FlashlightNearSurfaceBlend * FlashlightNearSurfaceDim;
    light.Intensity = renderFlashlight
        ? FlashlightIntensity * state.output * proximityExposure *
            state.powerBlend *
            (1.0f - batteryWeakness * FlashlightWeakDim)
        : 0.0f;
    light.Range = (FlashlightRange - m_FlashlightNearSurfaceBlend * FlashlightNearSurfaceRangeCut) * (1.0f - batteryWeakness * FlashlightWeakRangeCut);
    // 手で持ったライトの小さな揺れを加えている。電池が少ないときは視点は動かさず、
    // 光だけに電気的・機械的な不安定さを足している。
    constexpr float FlashlightSwayBase = 0.0035f;
    constexpr float FlashlightSwayBatteryStress = 0.0065f;
    constexpr float FlashlightSwayHorizontalRadiansPerSecond = 1.37f;
    constexpr float FlashlightSwayVerticalRadiansPerSecond = 1.91f;
    constexpr float FlashlightSwayVerticalPhase = 1.2f;
    constexpr float FlashlightSwayVerticalScale = 0.72f;
    const float flashlightSway = renderFlashlight
        ? FlashlightSwayBase + batteryStress * FlashlightSwayBatteryStress
        : 0.0f;
    Vector3 flashlightDirection(
        sinf(m_AmbienceTimer * FlashlightSwayHorizontalRadiansPerSecond) * flashlightSway,
        sinf(m_AmbienceTimer * FlashlightSwayVerticalRadiansPerSecond + FlashlightSwayVerticalPhase) *
            flashlightSway * FlashlightSwayVerticalScale,
        1.0f);
    flashlightDirection.Normalize();
    light.Direction = Vector4(
        flashlightDirection.x,
        flashlightDirection.y,
        flashlightDirection.z,
        0.0f);
    // スポットライトの形：14度の内側は全部の明るさ、32度の外側で消え、その間は1.8乗で暗くしている（照らした輪の外がすぐ闇になる）。
    // 審査会で「暗くて見えない」と言われ、11度・26度から広げた（輪の外の闇は残し、照らせる範囲だけを広げている）
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(FlashlightInnerConeDegrees)),
        cosf(DirectX::XMConvertToRadians(FlashlightOuterConeDegrees)),
        FlashlightConeFalloff,
        batteryWeakness
    );

    if (renderFlashlight)
    {
        // 電池が弱るほど光を暖かい色（弱った電球のような黄色）へ寄せ、残りが少ないことを色でも伝えている。
        light.Diffuse = Color(
            m_LightDiffuseR * (1.0f + batteryStress * FlashlightStressRedGain),
            m_LightDiffuseG * (1.0f - batteryStress * FlashlightStressGreenLoss - batteryWeakness * FlashlightWeakGreenLoss),
            m_LightDiffuseB * (1.0f - batteryStress * FlashlightStressBlueLoss - batteryWeakness * FlashlightWeakBlueLoss),
            1.0f);
        // 懐中電灯を点けている間は、光の外を暗く沈め、照らした所（と天井の照明の光だまり）だけが見えるようにしている
        light.Ambient = FlashlightOnAmbient;
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        // 懐中電灯を消すと、照明の光だまりの外はほとんど闇（壁の輪郭がかすかに分かる程度）
        light.Ambient = FlashlightOffAmbient;
    }

    // 電力が戻ったら、環境光を少し明るくしている（それでも照明の間には暗がりが残る程度にしている）
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = light.FlashlightEnabled ? PoweredFlashlightOnAmbient : PoweredFlashlightOffAmbient;
    }
    return light;
}

// 懐中電灯の照り返し：光が最初に当たった面から少し手前に、弱い点光源を置いている。
// 本物の懐中電灯は、照らした壁や床で跳ね返った光が部屋全体をぼんやり明るくする（近い面ほど強い）。
// 面から少し手前に置き、光の筋（horrorMist.hlsli。高さ-58より上の光源だけ）が出ないよう、高さは BounceMaxY までにしている
void Player::UpdateFlashlightBounce(const LIGHT& light, bool renderFlashlight, const FlashlightBeam& beam, float deltaTime)
{
    float targetIntensity = 0.0f;
    if (renderFlashlight && beam.HitDistance < FlashlightBounceReach)
    {
        const float closeness = 1.0f - beam.HitDistance / FlashlightBounceReach;
        targetIntensity = light.Intensity * BounceIntensityScale *
            (BounceFarIntensity + (1.0f - BounceFarIntensity) * closeness * closeness);
        Vector3 bouncePosition = beam.Origin + beam.Direction * (std::max)(beam.HitDistance - BounceSurfaceOffset, BounceMinDistance);
        bouncePosition.y = (std::min)(bouncePosition.y, BounceMaxY);
        const float follow = m_HasBouncePosition
            ? 1.0f - std::pow(1.0f - BouncePositionFollowPerFrame, deltaTime * ReferenceFramesPerSecond)
            : 1.0f;
        m_BouncePosition += (bouncePosition - m_BouncePosition) * follow;
        m_HasBouncePosition = true;
        m_BounceRange = BounceRangeBase + beam.HitDistance * BounceRangePerDistance;
        // 跳ね返った光は、壁や床の色を少し帯びて暖かくなる
        m_BounceColor = Vector3(light.Diffuse.x, light.Diffuse.y * BounceGreenTint, light.Diffuse.z * BounceBlueTint);
    }
    const float response = 1.0f - std::pow(1.0f - BounceIntensityFollowPerFrame, deltaTime * ReferenceFramesPerSecond);
    m_BounceIntensity += (targetIntensity - m_BounceIntensity) * response;
}

// 目の高さにカメラを置いている（一人称は頭の揺れと呼吸の揺れを足し、三人称（デバッグ用）は後ろ上からのぞき込む）
void Player::UpdateCameraPosition(Camera& camera, const Vector3& forward, const Vector3& right)
{
    if (!m_IsFPS)
    {
        Vector3 camPos = m_Position;
        camPos -= forward * ThirdPersonDistance;
        camPos.y += ThirdPersonHeight;
        camera.SetPosition(camPos);
        return;
    }

    Vector3 eyePos = m_Position;
    eyePos.y += m_CameraHeightOffset;
    // 止まっていても遅い揺れを残し、三脚ではなく呼吸している人の視点に感じられるようにしている。
    const float breath = sinf(m_AmbienceTimer * EyeBreathRadiansPerSecond) * EyeBreathHeight;
    eyePos.y += m_Movement.GetHeadBobOffset() + breath;
    eyePos += right * m_Movement.GetHeadBobSideOffset();
    camera.SetPosition(eyePos);
}

// 体の影：足もとの位置・向き・歩く手足の振りを、壁と床のシェーダーへ渡している（figureShadow.hlsli）。
// BodyWalkCycleDistance 進むと手足の振りが1往復する。ロッカーの中と三人称では影を落とさない
void Player::UpdateBodyShadow(const Vector3& forward, float deltaTime)
{
    const Vector3 velocity = m_Movement.GetVelocity();
    const float walkSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    const float walkRate = (std::min)(walkSpeed / m_Movement.GetMaximumHorizontalSpeed(), 1.0f);
    m_BodyWalkPhase += walkSpeed * deltaTime * (DirectX::XM_2PI / BodyWalkCycleDistance);
    m_BodyWalkAmount += (walkRate - m_BodyWalkAmount) * (1.0f - std::exp(-deltaTime * BodyWalkAmountResponse));
    Renderer::SetPlayerBody(m_Position, forward, std::sin(m_BodyWalkPhase) * m_BodyWalkAmount, m_IsFPS && !m_IsHiding);
}

// 開発用の電池の回復（Debug版の B キー。3面は開発用プレビューなので、Releaseでも B キーとコントローラーの BACK で使える）
void Player::HandleDebugBatteryKey()
{
    const bool stage3Preview = Core::Game::GetInstance()->GetCurrentSceneName() == SceneName::Stage3;
#ifdef _DEBUG
    constexpr bool allowBatteryKey = true;
#else
    const bool allowBatteryKey = stage3Preview;
#endif
    if ((allowBatteryKey && Input::GetKeyTrigger(VK_B)) ||
        (stage3Preview && Input::GetButtonTrigger(XINPUT_BACK)))
    {
        AddBattery(DebugBatteryRefill);
    }
}

// 一人称のときは懐中電灯を持った右手だけを、デバッグ用の三人称のときはメッシュを描いている。
void Player::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    if (m_BounceIntensity < MinimumBounceIntensity || !m_HasBouncePosition)
    {
        return;
    }
    ENVIRONMENT_POINT_LIGHT bounce{};
    bounce.PositionRange = Vector4(m_BouncePosition.x, m_BouncePosition.y, m_BouncePosition.z, m_BounceRange);
    bounce.ColorIntensity = Vector4(m_BounceColor.x, m_BounceColor.y, m_BounceColor.z, m_BounceIntensity);
    lights.push_back(bounce);
}

void Player::Draw(Camera* cam)
{
    cam->SetCamera();

    // 一人称では体を描かず、懐中電灯を持った右手だけを描いている（プレイヤーの視点の本描画のときだけ。監視映像には映さない）。
    // 三人称はデバッグ用の表示。
    if (m_IsFPS)
    {
        if (cam == Core::Game::GetInstance()->GetCamera())
        {
            m_Hands.Draw(*cam);
        }
        return;
    }

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );

    Matrix t = Matrix::CreateTranslation(m_Position);
    Matrix s = Matrix::CreateScale(m_Scale);

    Matrix worldmtx = s * r * t;
    Renderer::SetWorldMatrix(&worldmtx);

    m_MeshRenderer.BeforeDraw();

    // サブセットごとに、対応するマテリアルを設定して描いている
    for (int i = 0; i < m_subsets.size(); i++)
    {
        m_Materials[m_subsets[i].MaterialIdx]->SetGPU();

        m_MeshRenderer.DrawSubset(
            m_subsets[i].IndexNum,
            m_subsets[i].IndexBase,
            m_subsets[i].VertexBase
        );
    }
}

// 手のモデルの参照を手放し、体の影を消している（プレイヤーのメッシュとマテリアルはメンバーの破棄で解放される）
void Player::Uninit()
{
    m_Hands.Uninit();
    Renderer::SetPlayerBody(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f), 0.0f, false);
}

// ヨーから水平の向きの単位ベクトルを作り、移動と調べる操作で共有している。
DirectX::SimpleMath::Vector3 Player::GetForward() const
{
    Camera* cam = Core::Game::GetInstance()->GetCamera();

    float yaw = cam->GetCameraDirection();

    return DirectX::SimpleMath::Vector3(
        sinf(yaw),
        0.0f,
        cosf(yaw)
    );
}

// ロッカーに入っている。懐中電灯は消し、視点を扉の正面へ向けている。
void Player::EnterHiding(const Vector3& hidePosition, const Vector3& exitPosition, float facing)
{
    if (m_IsHiding)
    {
        return;
    }
    m_IsHiding = true;
    m_HidePosition = hidePosition;
    m_HideExitPosition = exitPosition;
    m_HideFacing = facing;
    m_HidingInputTimer = HidingInputDelay;
    m_Position = hidePosition;
    m_Movement.ResetVelocity();
    if (m_Flashlight.IsOn() && m_Flashlight.Toggle())
    {
        Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_FLASHLIGHT, FlashlightClickPitch);
    }
    Core::Game::GetInstance()->GetCamera()->SetCameraDirection(facing);
}

// 隠れている間の処理。扉のすき間から見える範囲だけ見回せるよう向きを制限し、
// 調べるボタンで外へ出ている。
void Player::UpdateHiding(Camera& camera, float deltaTime)
{
    m_HidingInputTimer = (std::max)(0.0f, m_HidingInputTimer - deltaTime);

    // 扉の正面からのずれを-π〜πに直し、見回せる角度の中に収めている
    float offset = MathUtility::WrapAngle(camera.GetCameraDirection() - m_HideFacing);
    offset = (std::clamp)(offset, -HidingLookRange, HidingLookRange);
    camera.SetCameraDirection(m_HideFacing + offset);

    if (m_HidingInputTimer > 0.0f ||
        !(Input::GetKeyTrigger(VK_E) || Input::GetButtonTrigger(XINPUT_A)))
    {
        return;
    }
    // 外へ出る：出る位置に立たせ、扉の音を鳴らしている
    m_IsHiding = false;
    m_HidingInputTimer = HidingInputDelay;
    m_Position = m_HideExitPosition;
    m_Movement.ResetVelocity();
    Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR, m_HidePosition, LockerCreakPitch, LockerCreakVolume);
}

// 出口の扉の先の階段を上る動き：今の位置から踊り場の奥へ向け、最初の段までの平らな距離と、斜めに歩くときの1段の歩く距離を求めている
Player::StairClimb Player::MakeExitClimb(const Vector3& position, const Vector3& stairAxis, float stairStart,
    float stepRun, float stepRise, int stepCount, const Vector3& landing)
{
    // 段の向きの成分がこれより小さい（ほぼ横向き）ときは、この値で割って、距離が大きくなりすぎないようにしている
    constexpr float minimumForward = 0.1f;
    Vector3 direction = landing - position;
    direction.y = 0.0f;
    direction.Normalize();
    const float forward = (std::max)(direction.Dot(stairAxis), minimumForward);
    StairClimb climb;
    climb.Direction = direction;
    climb.FlatDistance = (std::max)(0.0f, (stairStart - position.Dot(stairAxis)) / forward);
    climb.StepRun = stepRun / forward;
    climb.StepRise = stepRise;
    climb.StepCount = stepCount;
    climb.EndFlatDistance = ExitClimbLandingDepth;
    climb.Speed = ExitClimbSpeed;
    climb.TargetPitch = ExitClimbTargetPitch;
    climb.HoldAtEnd = true;
    return climb;
}

// 面の始まりに下の段から上がってくる動き：段の数だけ目の高さを下げた所から、段ごとに上げて、普段の操作に戻る
Player::StairClimb Player::MakeArrivalClimb(const Vector3& direction)
{
    StairClimb arrival;
    arrival.Direction = direction;
    arrival.FlatDistance = ArrivalFlatDistance;
    arrival.StepCount = ArrivalStepCount;
    arrival.EndFlatDistance = ArrivalEndFlatDistance;
    arrival.Speed = ArrivalSpeed;
    arrival.EyeDrop = arrival.StepRise * static_cast<float>(arrival.StepCount);
    arrival.TargetPitch = 0.0f;
    arrival.HoldAtEnd = false;
    return arrival;
}

// 階段を自動で上り始めている。今の足元を始まりの位置にし、今の視点の向きから、歩く向き・決めた上下の向きへ少しずつ向けていく
void Player::StartStairClimb(const StairClimb& climb)
{
    m_Climb = ClimbState{};
    m_Climb.Settings = climb;
    Vector3 direction(climb.Direction.x, 0.0f, climb.Direction.z);
    if (direction.LengthSquared() < 1e-6f)
    {
        direction = Vector3(0.0f, 0.0f, 1.0f);
    }
    direction.Normalize();
    m_Climb.Settings.Direction = direction;
    m_Climb.Active = true;
    m_Climb.Start = m_Position;
    const Camera* camera = Core::Game::GetInstance()->GetCamera();
    m_Climb.StartYaw = camera->GetCameraDirection();
    m_Climb.StartPitch = camera->GetCameraPitch();
    m_Movement.ResetVelocity();
}

// 階段を上る演出を1フレーム進めている。
// ・決めた向きへ一定の速さで歩き、段の奥行きごとに1段ずつ目の高さを上げる（段を踏む瞬間に少しだけ沈んで持ち上がる）。
// ・1段ごとに、少し重い足音を鳴らす（平らな所も同じ間隔で鳴らす）。
// ・視点は、始めたときの向きから、歩く向きと決めた上下の向きへ、0.8秒かけて向ける。
void Player::UpdateStairClimb(float deltaTime)
{
    const StairClimb& settings = m_Climb.Settings;
    const float stairsLength = settings.StepRun * static_cast<float>(settings.StepCount);
    const float total = settings.FlatDistance + stairsLength + settings.EndFlatDistance;
    m_Climb.Time += deltaTime;
    m_Climb.Distance = (std::min)(total, m_Climb.Distance + settings.Speed * deltaTime);
    const bool finished = m_Climb.Distance >= total;

    // 足元は床の高さのまま、決めた向きへ動かしている
    m_Position = Vector3(
        m_Climb.Start.x + settings.Direction.x * m_Climb.Distance,
        m_Climb.Start.y,
        m_Climb.Start.z + settings.Direction.z * m_Climb.Distance);

    // 今いる段の高さ。段の始まりで次の段へ足を乗せ、奥行きの最初の4割で持ち上がる
    const float stairDistance = m_Climb.Distance - settings.FlatDistance;
    float height = 0.0f;
    if (stairDistance > 0.0f)
    {
        const float steps = stairDistance / settings.StepRun;
        const float fullSteps = std::floor(steps);
        if (fullSteps >= static_cast<float>(settings.StepCount))
        {
            height = settings.StepRise * static_cast<float>(settings.StepCount);
        }
        else
        {
            const float fraction = steps - fullSteps;
            const float lift = (std::clamp)(fraction / ClimbLiftFraction, 0.0f, 1.0f);
            height = settings.StepRise * (fullSteps + MathUtility::SmoothCurve(lift));
        }
    }

    // 1段（平らな所は同じ間隔）ごとに足音を鳴らしている。段を上るときは、普段の足音より少し低く重くしている
    const int stepIndex = static_cast<int>(m_Climb.Distance / settings.StepRun);
    if (stepIndex != m_Climb.LastStepIndex && !finished)
    {
        m_Climb.LastStepIndex = stepIndex;
        ++m_FootstepIndex;
        const bool onStairs = stairDistance > 0.0f && stairDistance < stairsLength;
        const float pitch = (m_FootstepIndex % 2 == 0 ? ClimbFootstepPitchLeft : ClimbFootstepPitchRight) - (onStairs ? ClimbStairPitchDrop : 0.0f);
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, m_Position, pitch, onStairs ? ClimbStairFootstepVolume : ClimbFlatFootstepVolume);
        if (onStairs)
        {
            Input::SetVibration(StairStepRumbleFrames, StairStepRumbleStrength);
        }
    }

    // 段を踏むたびに、目が少し沈んで戻る揺れ（歩くときの頭の揺れの代わり）
    const float stepPhase = m_Climb.Distance / settings.StepRun * DirectX::XM_PI;
    const float bob = finished ? 0.0f : -std::abs(std::sin(stepPhase)) * ClimbStepBob;

    Camera* camera = Core::Game::GetInstance()->GetCamera();
    // 歩く向きと決めた上下の向きへ、0.8秒かけて向けている（向きの差は、近い回り方で縮める）
    const float turn = (std::clamp)(m_Climb.Time / ClimbTurnSeconds, 0.0f, 1.0f);
    const float smoothTurn = MathUtility::SmoothCurve(turn);
    const float targetYaw = std::atan2(settings.Direction.x, settings.Direction.z);
    const float yawDifference = std::remainder(targetYaw - m_Climb.StartYaw, DirectX::XM_2PI);
    const float yaw = m_Climb.StartYaw + yawDifference * smoothTurn;
    const float pitch = m_Climb.StartPitch + (settings.TargetPitch - m_Climb.StartPitch) * smoothTurn;
    camera->SetCameraDirection(yaw);
    camera->SetCameraPitch(pitch);

    Vector3 eye = m_Position;
    eye.y += m_CameraHeightOffset - settings.EyeDrop + height + bob;
    camera->SetPosition(eye);

    // 上っている間は、懐中電灯を持った手を下げている（手すりにつかまって上るような姿勢）
    FirstPersonHands::FrameInput handInput;
    handInput.DeltaTime = deltaTime;
    handInput.Lowered = true;
    m_Hands.Update(handInput);

    if (finished && !settings.HoldAtEnd)
    {
        m_Climb.Active = false;
        m_Movement.ResetVelocity();
    }
}
