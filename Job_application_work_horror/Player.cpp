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
    m_Position = Vector3(0.0f, -80.0f, 0.0f);
    m_Scale = Vector3(1.0f, 1.0f, 1.0f);
    m_Movement.Initialize();
    m_Flashlight.InitializeRuntimeNotifications();
    m_FlashlightNearSurfaceBlend = 0.0f;
    m_FootstepTimer = 0.0f;
    m_FootstepIndex = 0;
    m_SurfaceNoisePulse = 0.0f;
    m_WetSurfaceOverride = false;
}

// 入力・移動・当たり判定・カメラ・懐中電灯・電池の消費を、1フレーム分更新している。
// 操作できない間もカメラとライトの状態は崩さないようにし、演出から戻りやすくしている。
void Player::Update()
{
    // Scene::UpdateはObject::Updateより先に呼ばれるため、前フレームの値を
    // Sceneが読んだ後で、今回の足音の大きさを作り直している。
    m_SurfaceNoisePulse = 0.0f;
    if (!m_CanControl)
    {
        m_Movement.StopControl();
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

    if (Input::GetKeyPress(VK_W)) moveDir += forward;
    if (Input::GetKeyPress(VK_S)) moveDir -= forward;
    if (Input::GetKeyPress(VK_A)) moveDir -= right;
    if (Input::GetKeyPress(VK_D)) moveDir += right;

    const DirectX::XMFLOAT2 leftStick = Input::GetLeftAnalogStick();
    moveDir += right * leftStick.x;
    moveDir += forward * leftStick.y;

    if (m_IsHiding)
    {
        moveDir = Vector3::Zero;
    }
    // Shift（左スティックの押し込み）で走る。走れなくなった瞬間（スタミナ切れ）は振動で知らせている
    const bool wantsToSprint = !m_IsHiding && m_SprintAllowed &&
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
        // 足元が水の中なら水音、そうでなければ普通の足音を鳴らし、左右の足で音の高さを少し変えている
        bool wetStep = m_WetSurfaceOverride;
        for (Ground* ground : Core::Game::GetInstance()->GetObjects<Ground>())
        {
            wetStep = ground->TriggerFootstepRipple(
                m_Position, m_Movement.IsSprinting()) || wetStep;
        }
        const float alternatingPitch = (m_FootstepIndex % 2 == 0)
            ? (m_Movement.IsSprinting() ? 1.08f : 0.97f)
            : (m_Movement.IsSprinting() ? 1.14f : 1.03f);
        Core::Game::GetInstance()->PlayAudioCue(
            wetStep ? SOUND_CUE_WATER_STEP : SOUND_CUE_FOOTSTEP,
            wetStep ? alternatingPitch * 0.92f : alternatingPitch);
        // 足音の大きさ：水の中なら大きく、走ればさらに大きい（2面の影が反応する）
        m_SurfaceNoisePulse = wetStep
            ? (m_Movement.IsSprinting() ? 1.0f : 0.62f)
            : (m_Movement.IsSprinting() ? 0.32f : 0.0f);
        ++m_FootstepIndex;
        m_FootstepTimer = m_Movement.IsSprinting() ? 0.31f : 0.46f;
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
    if (!m_IsHiding && (Input::GetKeyTrigger(VK_F) ||
        Input::GetButtonTrigger(XINPUT_Y)))
    {
       
        if (m_Flashlight.Toggle())
        {
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
    if (visibleLight)
    {
        // 目の位置から視線の方向へ70の線を伸ばし、最も近い壁までの距離を求めている
        Vector3 beamOrigin = m_Position;
        beamOrigin.y += m_CameraHeightOffset;
        const float cameraPitch = cam->GetCameraPitch();
        const float pitchCos = cosf(cameraPitch);
        Vector3 beamDirection(
            sinf(yaw) * pitchCos,
            sinf(cameraPitch),
            cosf(yaw) * pitchCos);
        beamDirection.Normalize();
        const Vector3 beamEnd =
            beamOrigin + beamDirection * closestSurfaceDistance;

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
                closestSurfaceDistance = (std::min)(
                    closestSurfaceDistance, hitDistance);
            }
        }

        // 床はWallとは別に描いているため、下を向いたときは床の高さの平面との距離も計算している。
        if (beamDirection.y < -0.001f)
        {
            const float floorDistance =
                (MIN_Y_POSITION - beamOrigin.y) / beamDirection.y;
            if (floorDistance >= 0.0f)
            {
                closestSurfaceDistance = (std::min)(
                    closestSurfaceDistance, floorDistance);
            }
        }
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

    // シェーダーへ渡す懐中電灯の光（スポットライト）を作っている
    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = renderFlashlight ? TRUE : FALSE;
    const float proximityExposure =
        1.0f - m_FlashlightNearSurfaceBlend * 0.42f;
    light.Intensity = renderFlashlight
        ? 1.50f * lightOutput * proximityExposure *
            flashlightState.powerBlend
        : 0.0f;
    light.Range = 275.0f - m_FlashlightNearSurfaceBlend * 48.0f;
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
    // スポットライトの形：13度の内側は全部の明るさ、29度の外側で消え、その間は1.55乗でなめらかに暗くしている
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(13.0f)),
        cosf(DirectX::XMConvertToRadians(29.0f)),
        1.55f,
        0.0f
    );

    if (renderFlashlight)
    {
        // 電池が弱るほど光を少し暖かい色へ寄せ、残りが少ないことを色でも伝えている。
        light.Diffuse = Color(
            m_LightDiffuseR * (1.0f + batteryStress * 0.04f),
            m_LightDiffuseG * (1.0f - batteryStress * 0.06f),
            m_LightDiffuseB * (1.0f - batteryStress * 0.18f),
            1.0f);
        light.Ambient = Color(0.255f, 0.252f, 0.246f, 1.0f);
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        light.Ambient = Color(
            0.245f, // 懐中電灯を消しても、壁と進む方向を見分けられる最低限の明るさ
            0.248f,
            0.252f,
            1.0f
        );
    }

    // 電力が戻ったら、環境光を少し明るくしている
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = Color(0.285f, 0.292f, 0.304f, 1.0f);
    }

    Renderer::SetLight(light);

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


	// 電池を回復するデバッグ用のキー入力（Bキー。今はRelease版でも有効になっている）
    if (Input::GetKeyTrigger(VK_B))
    {
        AddBattery(50.0f);
    }
}

// 一人称のときは体を見せず、デバッグ用の三人称のときだけメッシュを描いている。
void Player::Draw(Camera* cam)
{
    cam->SetCamera();

    // 一人称では自分の体を描かない。三人称はデバッグ用の表示。
    if (m_IsFPS) return;

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

// 解放するものはない（メッシュとマテリアルはメンバーの破棄で解放される）
void Player::Uninit()
{}

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