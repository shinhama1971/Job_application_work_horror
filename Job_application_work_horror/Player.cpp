// ============================================================================
// ファイルの役割: 一人称の移動、視点、懐中電灯、電池、足音、ロッカーに隠れる操作を管理している。
// 主な技術: 一人称の操作、壁との当たり判定と押し戻し、歩くときの頭の揺れ、スポットライト、キーボードとコントローラーの入力の統合
// ============================================================================

#include "Player.h"
#include "Application.h"
#include "Game.h"
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
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_subsets = staticmesh.GetSubsets();
    m_Textures = staticmesh.GetTextures();

    // モデルのマテリアルごとにMaterialを作り、同じ番号のテクスチャを貼っている
    std::vector<MATERIAL> materials = staticmesh.GetMaterials();

    for (int i = 0; i < materials.size(); i++)
    {
        std::unique_ptr<Material> m = std::make_unique<Material>();
        m->Create(materials[i]);
        m->SetShader(&m_Shader);
        m_Materials.push_back(std::move(m));
    }

    size_t count = (std::min)(m_Materials.size(), m_Textures.size());

    for (size_t i = 0; i < count; i++)
    {
        m_Materials[i]->SetTexture(m_Textures[i].get());
    }

    // 移動・懐中電灯・足音の状態を初期化している
    m_Position = Vector3(0.0f, -85.0f, 0.0f);
    m_Scale = Vector3(1.0f, 1.0f, 1.0f);
    m_Movement.Initialize();
    m_Flashlight.InitializeRuntimeNotifications();
    m_FlashlightNearSurfaceBlend = 0.0f;
    m_FootstepTimer = 0.0f;
    m_FootstepIndex = 0;
    m_SurfaceNoisePulse = 0.0f;
    m_WetSurfaceOverride = false;
    m_Hands.Init();
}

// 入力・移動・当たり判定・カメラ・懐中電灯・電池の消費を、1フレーム分更新している。
// 操作できない間もカメラとライトの状態は崩さないようにし、演出から戻りやすくしている。
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
    float yaw = cam->GetCameraDirection();

    Vector3 forward(sinf(yaw), 0.0f, cosf(yaw));
    Vector3 right(sinf(yaw + 1.5708f), 0.0f, cosf(yaw + 1.5708f));

    Vector3 moveDir = Vector3::Zero;

    // タイトル画面の後ろで映している間（m_AttractMode）は、操作の入力を読まない（タイトルの面の選択の A・D で動かないように）
    if (!m_AttractMode)
    {
        if (Input::GetKeyPress(VK_W)) moveDir += forward;
        if (Input::GetKeyPress(VK_S)) moveDir -= forward;
        if (Input::GetKeyPress(VK_A)) moveDir -= right;
        if (Input::GetKeyPress(VK_D)) moveDir += right;

        const DirectX::XMFLOAT2 leftStick = Input::GetLeftAnalogStick();
        moveDir += right * leftStick.x;
        moveDir += forward * leftStick.y;
    }

    if (m_IsHiding)
    {
        moveDir = Vector3::Zero;
    }
    // Shift（左スティックの押し込み）で走る。走れなくなった瞬間（スタミナ切れ）は振動で知らせている
    const bool wantsToSprint = !m_IsHiding && !m_AttractMode && m_SprintAllowed &&
        (Input::GetKeyPress(VK_SHIFT) ||
            Input::GetButtonPress(XINPUT_LEFT_THUMB));
    if (m_Movement.Update(moveDir, wantsToSprint, deltaTime))
    {
        Input::SetVibration(5, 0.10f);
    }

    // 速度で位置を動かし、床より下には行かないようにしている
    const Vector3 positionBeforeMove = m_Position;
    m_Position += m_Movement.GetVelocity() * deltaTime;

    if (m_Position.y < -99.0f)
    {
        m_Position.y = -99.0f;
        m_Movement.StopVerticalVelocity();
    }

    const std::vector<Wall*> walls =
        Core::Game::GetInstance()->GetObjects<Wall>();
    const std::vector<Door*> doors =
        Core::Game::GetInstance()->GetObjects<Door>();

    // 壁による押し戻しを2回行い、部屋の角で隣の壁へめり込んだ場合も判定し直している。
    for (int pass = 0; pass < 2; ++pass)
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

    // 押し戻しの後に実際に動けた距離を使うため、壁へ押し続けても足音は鳴らない。
    m_FootstepTimer = (std::max)(0.0f, m_FootstepTimer - deltaTime);
    const Vector3 actualMovement(
        m_Position.x - positionBeforeMove.x,
        0.0f,
        m_Position.z - positionBeforeMove.z);
    const float footstepMovementThreshold = 4.8f * deltaTime;
    if (actualMovement.LengthSquared() >
        footstepMovementThreshold * footstepMovementThreshold &&
        m_FootstepTimer <= 0.0f)
    {
        // 波紋と同じ判定で、足元が水の中なら歩き・走りそれぞれの水音を鳴らす。
        const bool sprinting = m_Movement.IsSprinting();
        bool wetStep = m_WetSurfaceOverride;
        for (Ground* ground : Core::Game::GetInstance()->GetObjects<Ground>())
        {
            wetStep = ground->TriggerFootstepRipple(
                m_Position, sprinting) || wetStep;
        }
        const float alternatingPitch = (m_FootstepIndex % 2 == 0)
            ? (sprinting ? 1.08f : 0.97f)
            : (sprinting ? 1.14f : 1.03f);
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
            step, wetStep ? alternatingPitch * 0.92f : alternatingPitch);
        // 足音の大きさ：水の中なら大きく、走ればさらに大きい（2面の影が反応する）
        m_SurfaceNoisePulse = wetStep
            ? (sprinting ? 1.0f : 0.62f)
            : (sprinting ? 0.32f : 0.0f);
        ++m_FootstepIndex;
        m_FootstepTimer = sprinting ? 0.31f : 0.46f;
    }
    m_WetSurfaceOverride = false;

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

    // F（コントローラーはY）で懐中電灯を点けたり消したりしている（隠れている間はできない）
    if (!m_IsHiding && !m_AttractMode && (Input::GetKeyTrigger(VK_F) ||
        Input::GetButtonTrigger(XINPUT_Y)))
    {
       
        if (m_Flashlight.Toggle())
        {
            m_FlashlightToggledThisFrame = true;
            Core::Game::GetInstance()->PlayAudioCue(
                SOUND_CUE_FLASHLIGHT,
                m_Flashlight.IsOn() ? 1.08f : 0.94f);
        }
    }

    // 電池を減らし、20%・10%を切った瞬間に画面と振動で知らせている
    const int warningLevel = m_Flashlight.UpdateBattery(deltaTime);
    if (warningLevel > 0)
    {
        Core::Game* game = Core::Game::GetInstance();
        game->GetPostProcess()->TriggerHorrorPulse(
            warningLevel == 2 ? 0.26f : 0.12f,
            warningLevel == 2 ? 0.34f : 0.22f);
        Input::SetVibration(
            warningLevel == 2 ? 8 : 4,
            warningLevel == 2 ? 0.18f : 0.09f);
    }
    // このフレームの光の強さと、一瞬暗くなる状態を受け取っている
    const FlashlightSystem::FrameState flashlightState =
        m_Flashlight.UpdateFrameState(deltaTime);
    const bool visibleLight = flashlightState.visible;
    const bool renderFlashlight = flashlightState.render;
    const float lightOutput = flashlightState.output;
    const float batteryStress = flashlightState.batteryStress;

    if (flashlightState.voltageDropStarted)
    {
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            0.055f + batteryStress * 0.095f,
            0.14f + batteryStress * 0.08f);
        Input::SetVibration(
            2 + static_cast<int>(batteryStress * 4.0f),
            0.035f + batteryStress * 0.065f);
    }
    // 壁や床が近いときは、懐中電灯の明るさを下げている。
    // 明るさが一定だと、近くの面が白く飛んで、材質の細かい模様が消えるためである。
    float closestSurfaceDistance = 70.0f;
    // 照り返しの点光源を置く、光が最初に当たる面までの距離（BounceReach より遠ければ、照り返しは出さない）
    constexpr float BounceReach = 180.0f;
    float beamHitDistance = BounceReach;
    Vector3 beamOrigin = m_Position;
    beamOrigin.y += m_CameraHeightOffset;
    Vector3 beamDirection = Vector3::UnitZ;
    if (visibleLight)
    {
        // 目の位置から視線の方向へ線を伸ばし、最も近い壁までの距離を求めている
        // （70までは近い面で光を弱めるのに、180までは照り返しの位置に使っている）
        const float cameraPitch = cam->GetCameraPitch();
        const float pitchCos = cosf(cameraPitch);
        beamDirection = Vector3(
            sinf(yaw) * pitchCos,
            sinf(cameraPitch),
            cosf(yaw) * pitchCos);
        beamDirection.Normalize();
        const Vector3 beamEnd =
            beamOrigin + beamDirection * BounceReach;

        for (const Wall* wall : walls)
        {
            if (wall == nullptr)
            {
                continue;
            }

            float hitDistance = 0.0f;
            if (wall->IntersectsInteractionSegment(
                beamOrigin, beamEnd, hitDistance))
            {
                beamHitDistance = (std::min)(beamHitDistance, hitDistance);
            }
        }

        // 床はWallとは別に描いているため、下を向いたときは床の高さの平面との距離も計算している。
        if (beamDirection.y < -0.001f)
        {
            const float floorDistance =
                (MIN_Y_POSITION - beamOrigin.y) / beamDirection.y;
            if (floorDistance >= 0.0f)
            {
                beamHitDistance = (std::min)(beamHitDistance, floorDistance);
            }
        }
        closestSurfaceDistance = (std::min)(closestSurfaceDistance, beamHitDistance);
    }

    // 近い面までの距離が12〜54の間で、近いほど光を弱め、照らす距離も短くしている（急に変わらないよう、なめらかに近づけている）
    const float nearSurfaceTarget = visibleLight
        ? 1.0f - (std::clamp)(
            (closestSurfaceDistance - 12.0f) / 42.0f, 0.0f, 1.0f)
        : 0.0f;
    const float nearSurfaceResponse = 1.0f - std::pow(
        1.0f - 0.18f, deltaTime * 60.0f);
    m_FlashlightNearSurfaceBlend +=
        (nearSurfaceTarget - m_FlashlightNearSurfaceBlend) *
        nearSurfaceResponse;

    // 電池の弱り具合（60%を切ると0から増え、空で1）。弱るほど光が暗く・黄色く・短くなり、光の輪のむらがはっきりする
    const float batteryWeakness = (std::clamp)((60.0f - m_Flashlight.GetBattery()) / 60.0f, 0.0f, 1.0f);

    // シェーダーへ渡す懐中電灯の光（スポットライト）を作っている
    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = renderFlashlight ? TRUE : FALSE;
    // 近い面で弱める量は最大2割。以前は4割強だったが、壁・小物の懐中電灯の拡散光に上限を付けて（flashlightLighting.hlsli の
    // GetFlashlightDiffuseResponse）近くの壁が白く飛ばなくなったため、弱めすぎて「壁に近づくと何も見えない」状態になっていた
    const float proximityExposure =
        1.0f - m_FlashlightNearSurfaceBlend * 0.20f;
    light.Intensity = renderFlashlight
        ? 1.50f * lightOutput * proximityExposure *
            flashlightState.powerBlend *
            (1.0f - batteryWeakness * 0.30f)
        : 0.0f;
    light.Range = (275.0f - m_FlashlightNearSurfaceBlend * 48.0f) * (1.0f - batteryWeakness * 0.18f);
    // 手で持ったライトの小さな揺れを加えている。電池が少ないときは視点は動かさず、
    // 光だけに電気的・機械的な不安定さを足している。
    const float flashlightSway = renderFlashlight
        ? 0.0035f + batteryStress * 0.0065f
        : 0.0f;
    Vector3 flashlightDirection(
        sinf(m_AmbienceTimer * 1.37f) * flashlightSway,
        sinf(m_AmbienceTimer * 1.91f + 1.2f) * flashlightSway * 0.72f,
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
        cosf(DirectX::XMConvertToRadians(14.0f)),
        cosf(DirectX::XMConvertToRadians(32.0f)),
        1.8f,
        batteryWeakness
    );

    if (renderFlashlight)
    {
        // 電池が弱るほど光を暖かい色（弱った電球のような黄色）へ寄せ、残りが少ないことを色でも伝えている。
        light.Diffuse = Color(
            m_LightDiffuseR * (1.0f + batteryStress * 0.04f),
            m_LightDiffuseG * (1.0f - batteryStress * 0.06f - batteryWeakness * 0.10f),
            m_LightDiffuseB * (1.0f - batteryStress * 0.18f - batteryWeakness * 0.32f),
            1.0f);
        // 懐中電灯を点けている間は、光の外を暗く沈め、照らした所（と天井の照明の光だまり）だけが見えるようにしている
        light.Ambient = Color(0.070f, 0.072f, 0.072f, 1.0f);
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        light.Ambient = Color(
            0.045f, // 懐中電灯を消すと、照明の光だまりの外はほとんど闇（壁の輪郭がかすかに分かる程度）
            0.047f,
            0.050f,
            1.0f
        );
    }

    // 電力が戻ったら、環境光を少し明るくしている（それでも照明の間には暗がりが残る程度にしている）
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = Color(light.FlashlightEnabled ? 0.095f : 0.070f, light.FlashlightEnabled ? 0.097f : 0.072f,
            light.FlashlightEnabled ? 0.100f : 0.076f, 1.0f);
    }

    Renderer::SetLight(light);

    // 懐中電灯の照り返し：光が最初に当たった面から少し手前に、弱い点光源を置いている。
    // 本物の懐中電灯は、照らした壁や床で跳ね返った光が部屋全体をぼんやり明るくする（近い面ほど強い）。
    // 面から10だけ手前に置き、光の筋（horrorMist.hlsli。高さ-58より上の光源だけ）が出ないよう、高さは-59までにしている
    {
        float targetIntensity = 0.0f;
        if (renderFlashlight && beamHitDistance < BounceReach)
        {
            const float closeness = 1.0f - beamHitDistance / BounceReach;
            targetIntensity = light.Intensity * 0.42f * (0.25f + 0.75f * closeness * closeness);
            Vector3 bouncePosition = beamOrigin + beamDirection * (std::max)(beamHitDistance - 10.0f, 2.0f);
            bouncePosition.y = (std::min)(bouncePosition.y, -59.0f);
            const float follow = m_HasBouncePosition ? 1.0f - std::pow(1.0f - 0.25f, deltaTime * 60.0f) : 1.0f;
            m_BouncePosition += (bouncePosition - m_BouncePosition) * follow;
            m_HasBouncePosition = true;
            m_BounceRange = 75.0f + beamHitDistance * 0.45f;
            // 跳ね返った光は、壁や床の色を少し帯びて暖かくなる
            m_BounceColor = Vector3(light.Diffuse.x, light.Diffuse.y * 0.94f, light.Diffuse.z * 0.84f);
        }
        const float response = 1.0f - std::pow(1.0f - 0.20f, deltaTime * 60.0f);
        m_BounceIntensity += (targetIntensity - m_BounceIntensity) * response;
    }

    // 手に持った懐中電灯：レンズの光と、前の面で跳ね返って手を照らす光（面が近いほど強い。何もなくても床から少し返る）
    FirstPersonHands::FrameInput handInput;
    handInput.DeltaTime = deltaTime;
    handInput.LensGlow = renderFlashlight ? lightOutput * flashlightState.powerBlend : 0.0f;
    handInput.BatteryWeakness = batteryWeakness;
    handInput.BatteryStress = batteryStress;
    handInput.VoltageDrop = flashlightState.voltageDropStarted;
    handInput.NearSurface = m_FlashlightNearSurfaceBlend;
    if (renderFlashlight)
    {
        const float surfaceRatio = closestSurfaceDistance / 16.0f;
        const float bounce = light.Intensity * (0.04f + 0.30f / (1.0f + surfaceRatio * surfaceRatio));
        handInput.Bounce = Vector3(light.Diffuse.x, light.Diffuse.y, light.Diffuse.z) * bounce;
    }

	// カメラの位置と向きを更新している（歩くときの頭の揺れを含む）
    m_Movement.UpdateHeadBob(deltaTime);

    Vector3 eyePos = m_Position;
    eyePos.y += m_CameraHeightOffset;
    if (m_IsFPS)
    {
        // 止まっていても遅い揺れを残し、三脚ではなく呼吸している人の視点に感じられるようにしている。
        const float breath = sinf(m_AmbienceTimer * 1.15f) * 0.075f;
        eyePos.y += m_Movement.GetHeadBobOffset() + breath;
        eyePos += right * m_Movement.GetHeadBobSideOffset();
    }

    if (m_IsFPS)
    {
        cam->SetPosition(eyePos);

        float pitch = cam->GetCameraPitch();
        float cp = cosf(pitch);

        Vector3 lookDir(
            sinf(yaw) * cp,
            sinf(pitch),
            cosf(yaw) * cp
        );

        cam->SetTarget(eyePos + lookDir);
    }
    else
    {
        // 三人称（デバッグ用）：プレイヤーの後ろ上からのぞき込む視点
        float distance = 45.0f;
        float height = 28.0f;

        Vector3 camPos = m_Position;
        camPos -= forward * distance;
        camPos.y += height;

        cam->SetPosition(camPos);

        Vector3 target = m_Position;
        target.y += 18.0f;
        cam->SetTarget(target);
    }


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

    // 体の影：足もとの位置・向き・歩く手足の振りを、壁と床のシェーダーへ渡している（figureShadow.hlsli）。
    // 一歩は約12なので、24進むと手足の振りが1往復する。ロッカーの中と三人称では影を落とさない
    const Vector3 velocity = m_Movement.GetVelocity();
    const float walkSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    const float walkRate = (std::min)(walkSpeed / m_Movement.GetMaximumHorizontalSpeed(), 1.0f);
    m_BodyWalkPhase += walkSpeed * deltaTime * (DirectX::XM_2PI / 24.0f);
    m_BodyWalkAmount += (walkRate - m_BodyWalkAmount) * (1.0f - std::exp(-deltaTime * 8.0f));
    Renderer::SetPlayerBody(m_Position, forward, std::sin(m_BodyWalkPhase) * m_BodyWalkAmount, m_IsFPS && !m_IsHiding);

    // 3面は開発用プレビューなので、Releaseでも電池回復を使えるようにする。
    const bool stage3Preview = Core::Game::GetInstance()->GetCurrentSceneName() == SceneName::Stage3;
#ifdef _DEBUG
    constexpr bool allowBatteryKey = true;
#else
    const bool allowBatteryKey = stage3Preview;
#endif
    if ((allowBatteryKey && Input::GetKeyTrigger(VK_B)) ||
        (stage3Preview && Input::GetButtonTrigger(XINPUT_BACK)))
    {
        AddBattery(50.0f);
    }
}

// 一人称のときは懐中電灯を持った右手だけを、デバッグ用の三人称のときはメッシュを描いている。
void Player::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    if (m_BounceIntensity < 0.01f || !m_HasBouncePosition)
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
        Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_FLASHLIGHT, 0.94f);
    }
    Core::Game::GetInstance()->GetCamera()->SetCameraDirection(facing);
}

// 隠れている間の処理。扉のすき間から見える範囲だけ見回せるよう向きを制限し、
// 調べるボタンで外へ出ている。
void Player::UpdateHiding(Camera& camera, float deltaTime)
{
    m_HidingInputTimer = (std::max)(0.0f, m_HidingInputTimer - deltaTime);

    // 扉の正面からのずれを-π〜πに直し、見回せる角度の中に収めている
    constexpr float TwoPi = 6.28318530718f;
    float offset = std::fmod(camera.GetCameraDirection() - m_HideFacing, TwoPi);
    if (offset > TwoPi * 0.5f) offset -= TwoPi;
    if (offset < -TwoPi * 0.5f) offset += TwoPi;
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
    Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR, m_HidePosition, 1.35f, 0.55f);
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
            const float lift = (std::clamp)(fraction / 0.4f, 0.0f, 1.0f);
            height = settings.StepRise * (fullSteps + lift * lift * (3.0f - 2.0f * lift));
        }
    }

    // 1段（平らな所は同じ間隔）ごとに足音を鳴らしている。段を上るときは、普段の足音より少し低く重くしている
    const int stepIndex = static_cast<int>(m_Climb.Distance / settings.StepRun);
    if (stepIndex != m_Climb.LastStepIndex && !finished)
    {
        m_Climb.LastStepIndex = stepIndex;
        ++m_FootstepIndex;
        const bool onStairs = stairDistance > 0.0f && stairDistance < stairsLength;
        const float pitch = (m_FootstepIndex % 2 == 0 ? 0.90f : 0.95f) - (onStairs ? 0.06f : 0.0f);
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, m_Position, pitch, onStairs ? 1.15f : 0.95f);
        if (onStairs)
        {
            Input::SetVibration(2, 0.05f);
        }
    }

    // 段を踏むたびに、目が少し沈んで戻る揺れ（歩くときの頭の揺れの代わり）
    const float stepPhase = m_Climb.Distance / settings.StepRun * DirectX::XM_PI;
    const float bob = finished ? 0.0f : -std::abs(std::sin(stepPhase)) * 0.7f;

    Camera* camera = Core::Game::GetInstance()->GetCamera();
    // 歩く向きと決めた上下の向きへ、0.8秒かけて向けている（向きの差は、近い回り方で縮める）
    const float turn = (std::clamp)(m_Climb.Time / 0.8f, 0.0f, 1.0f);
    const float smoothTurn = turn * turn * (3.0f - 2.0f * turn);
    const float targetYaw = std::atan2(settings.Direction.x, settings.Direction.z);
    const float yawDifference = std::remainder(targetYaw - m_Climb.StartYaw, DirectX::XM_2PI);
    const float yaw = m_Climb.StartYaw + yawDifference * smoothTurn;
    const float pitch = m_Climb.StartPitch + (settings.TargetPitch - m_Climb.StartPitch) * smoothTurn;
    camera->SetCameraDirection(yaw);
    camera->SetCameraPitch(pitch);

    Vector3 eye = m_Position;
    eye.y += m_CameraHeightOffset - settings.EyeDrop + height + bob;
    camera->SetPosition(eye);
    const float cosPitch = std::cos(pitch);
    camera->SetTarget(eye + Vector3(std::sin(yaw) * cosPitch, std::sin(pitch), std::cos(yaw) * cosPitch));

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
