#include "Player.h"
#include "Game.h"
#include "Input.h"
#include "Camera.h"
#include "Renderer.h"
#include "Wall.h"
#include "Door.h"
#include "CeilingLight.h"

using namespace DirectX::SimpleMath;

void Player::Init()
{
    StaticMesh staticmesh;

	//今はゴルフボールのモデルを読み込む
    std::u8string modelFile = u8"assets/model/golf_ball/golf_ball.obj";
    std::string texDirectory = "assets/model/golf_ball";

    std::string tmpStr(
        reinterpret_cast<const char*>(modelFile.c_str()),
        modelFile.size()
    );

    staticmesh.Load(tmpStr, texDirectory);

    m_MeshRenderer.Init(staticmesh);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_subsets = staticmesh.GetSubsets();
    m_Textures = staticmesh.GetTextures();

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

    m_Position = Vector3(0.0f, -80.0f, 0.0f);
    m_Scale = Vector3(1.0f, 1.0f, 1.0f);
    m_Velocity = Vector3::Zero;
    m_Stamina = MAX_STAMINA;
    m_StaminaRecoveryDelay = 0.0f;
    m_SprintExhausted = false;
    m_BatteryNoticeTimer = 0.0f;
    m_LowBatteryWarningLevel = 0;
    m_WasFlashlightVoltageDrop = false;
    m_FlashlightNearSurfaceBlend = 0.0f;
}

void Player::Update()
{
    if (!m_CanControl)
    {
        m_IsSprinting = false;
        return;
    }

    Camera* cam = Core::Game::GetInstance()->GetCamera();
    constexpr float deltaTime = 1.0f / 60.0f;
    m_AmbienceTimer += deltaTime;
    m_BatteryNoticeTimer = (std::max)(
        0.0f, m_BatteryNoticeTimer - deltaTime);

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

    const float moveLengthSquared = moveDir.LengthSquared();
    const bool isMoving = moveLengthSquared > 0.0001f;
    const bool wantsToSprint =
        Input::GetKeyPress(VK_SHIFT) ||
        Input::GetButtonPress(XINPUT_LEFT_THUMB);

    if (m_SprintExhausted && m_Stamina >= MAX_STAMINA * 0.30f)
    {
        m_SprintExhausted = false;
    }

    m_IsSprinting = isMoving && wantsToSprint &&
        !m_SprintExhausted && m_Stamina > 0.0f;
    if (m_IsSprinting)
    {
        m_Stamina = (std::max)(
            0.0f, m_Stamina - STAMINA_DRAIN_PER_FRAME);
        m_StaminaRecoveryDelay = 0.45f;
        if (m_Stamina <= 0.0f)
        {
            m_IsSprinting = false;
            m_SprintExhausted = true;
            m_StaminaRecoveryDelay = 1.0f;
            Input::SetVibration(5, 0.10f);
        }
    }
    else if (m_StaminaRecoveryDelay > 0.0f)
    {
        m_StaminaRecoveryDelay = (std::max)(
            0.0f, m_StaminaRecoveryDelay - deltaTime);
    }
    else
    {
        m_Stamina = (std::min)(
            MAX_STAMINA,
            m_Stamina + STAMINA_RECOVERY_PER_FRAME);
    }

    const float currentMoveSpeed = m_MoveSpeed *
        (m_IsSprinting ? SPRINT_SPEED_MULTIPLIER : 1.0f);

    if (isMoving)
    {
        if (moveLengthSquared > 1.0f)
        {
            moveDir.Normalize();
        }

        const Vector3 desiredVelocity = moveDir * currentMoveSpeed;
        const float acceleration = m_IsSprinting ? 0.24f : 0.30f;
        m_Velocity.x +=
            (desiredVelocity.x - m_Velocity.x) * acceleration;
        m_Velocity.z +=
            (desiredVelocity.z - m_Velocity.z) * acceleration;
    }
    else
    {
        // Ease to a stop instead of changing velocity in one frame. This
        // removes the small camera snap when a movement key is released.
        m_Velocity.x *= 0.72f;
        m_Velocity.z *= 0.72f;
        if (std::abs(m_Velocity.x) < 0.001f) m_Velocity.x = 0.0f;
        if (std::abs(m_Velocity.z) < 0.001f) m_Velocity.z = 0.0f;
    }

    // �d��
    m_Velocity.y -= 0.01f;

    m_Position += m_Velocity;

    // ���̗����h�~
    if (m_Position.y < -99.0f)
    {
        m_Position.y = -99.0f;
        m_Velocity.y = 0.0f;
    }

    const std::vector<Wall*> walls =
        Core::Game::GetInstance()->GetObjects<Wall>();
    const std::vector<Door*> doors =
        Core::Game::GetInstance()->GetObjects<Door>();

    // Resolve twice so a push from one wall is also checked against the
    // neighbouring wall at room corners.
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

    // R�L�[�ň�l��
#if defined(_DEBUG) && defined(ENABLE_CAMERA_MODE_SHORTCUTS)
    if (Input::GetKeyTrigger(VK_R))
    {
        m_IsFPS = true;
    }

    // J�L�[�ŎO�l��
    if (Input::GetKeyTrigger(VK_T))
    {
        m_IsFPS = false;
    }

    if (Input::GetButtonTrigger(XINPUT_RIGHT_THUMB))
    {
        m_IsFPS = !m_IsFPS;
    }
#endif

    // F�L�[�ŉ����d��ON/OFF
    if (Input::GetKeyTrigger(VK_F) ||
        Input::GetButtonTrigger(XINPUT_Y))
    {
        // �d�r�����鎞����ON/OFF�ł���
        if (m_Battery > 0.0f)
        {
            m_FlashLightOn = !m_FlashLightOn;
        }
    }

   
    if (m_FlashLightOn)
    {
        m_Battery -= 0.02f;

        if (m_Battery <= 0.0f)
        {
            m_Battery = 0.0f;
            m_FlashLightOn = false;
        }
    }

    const int warningLevel = m_Battery <= 10.0f
        ? 2
        : (m_Battery <= 20.0f ? 1 : 0);
    if (warningLevel > m_LowBatteryWarningLevel)
    {
        m_LowBatteryWarningLevel = warningLevel;
        Core::Game* game = Core::Game::GetInstance();
        game->GetPostProcess()->TriggerHorrorPulse(
            warningLevel == 2 ? 0.26f : 0.12f,
            warningLevel == 2 ? 0.34f : 0.22f);
        Input::SetVibration(
            warningLevel == 2 ? 8 : 4,
            warningLevel == 2 ? 0.18f : 0.09f);
    }
    else if (m_Battery > 25.0f)
    {
        m_LowBatteryWarningLevel = 0;
    }

    bool visibleLight = m_FlashLightOn;
    float lightOutput = visibleLight ? 1.0f : 0.0f;
    float batteryStress = 0.0f;
    bool voltageDrop = false;

    if (m_FlashLightOn && m_Battery <= 20.0f)
    {
        m_FlickerTimer++;
        batteryStress = (20.0f - m_Battery) / 20.0f;

        // Combine unrelated frequencies so low-battery flicker never becomes
        // a predictable square wave. The beam normally stays usable.
        const float flickerTime =
            static_cast<float>(m_FlickerTimer) / 60.0f;
        const float slowVoltage = sinf(
            flickerTime * 7.1f + sinf(flickerTime * 1.7f) * 1.8f);
        const float ballastNoise =
            sinf(flickerTime * 13.7f) * sinf(flickerTime * 4.3f);
        const float unstableOutput =
            0.84f + slowVoltage * 0.075f + ballastNoise * 0.055f;
        lightOutput =
            1.0f + (unstableOutput - 1.0f) * batteryStress;

        // Very short voltage drops become more frequent near empty, but do
        // not hold the player in complete darkness for regular intervals.
        const float dropCycle = 3.7f - batteryStress * 1.45f;
        const float dropPhase = fmodf(flickerTime, dropCycle);
        const float dropDuration = 0.025f + batteryStress * 0.060f;
        if (dropPhase < dropDuration)
        {
            voltageDrop = true;
            lightOutput *= 0.46f - batteryStress * 0.23f;
        }
    }
    else
    {
        m_FlickerTimer = 0;
    }

    if (voltageDrop && !m_WasFlashlightVoltageDrop)
    {
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            0.055f + batteryStress * 0.095f,
            0.14f + batteryStress * 0.08f);
        Input::SetVibration(
            2 + static_cast<int>(batteryStress * 4.0f),
            0.035f + batteryStress * 0.065f);
    }
    m_WasFlashlightVoltageDrop = voltageDrop;

    // Reduce flashlight exposure near walls and the floor. A constant beam
    // made nearby surfaces clip to white and hid the material detail.
    float closestSurfaceDistance = 70.0f;
    if (visibleLight)
    {
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

        // Ground is rendered separately from Wall, so include its horizontal
        // plane when the player aims down.
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

    const float nearSurfaceTarget = visibleLight
        ? 1.0f - (std::clamp)(
            (closestSurfaceDistance - 12.0f) / 42.0f, 0.0f, 1.0f)
        : 0.0f;
    m_FlashlightNearSurfaceBlend +=
        (nearSurfaceTarget - m_FlashlightNearSurfaceBlend) * 0.18f;

    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = visibleLight ? TRUE : FALSE;
    const float proximityExposure =
        1.0f - m_FlashlightNearSurfaceBlend * 0.42f;
    light.Intensity = visibleLight
        ? 1.35f * lightOutput * proximityExposure
        : 0.0f;
    light.Range = 260.0f - m_FlashlightNearSurfaceBlend * 46.0f;
    // A hand-held lamp is never perfectly rigid. Low battery adds a little
    // electrical/mechanical instability without moving the player's aim.
    const float flashlightSway = visibleLight
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
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(16.0f)),
        cosf(DirectX::XMConvertToRadians(32.0f)),
        1.35f,
        0.0f
    );

    if (visibleLight)
    {
        // A struggling battery shifts the lamp slightly toward warm yellow.
        light.Diffuse = Color(
            m_LightDiffuseR * (1.0f + batteryStress * 0.04f),
            m_LightDiffuseG * (1.0f - batteryStress * 0.06f),
            m_LightDiffuseB * (1.0f - batteryStress * 0.18f),
            1.0f);
        light.Ambient = Color(0.225f, 0.218f, 0.205f, 1.0f);
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        light.Ambient = Color(
            0.225f, // Readable darkness without flattening the flashlight contrast.
            0.218f,
            0.205f,
            1.0f
        );
    }

    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = Color(0.255f, 0.262f, 0.272f, 1.0f);
    }

    Renderer::SetLight(light);

    // Turn the visible ceiling fixtures into real lights for the room geometry.
    ENVIRONMENT_LIGHTS environmentLights{};
    const bool powerRestored = Core::Game::GetInstance()->IsPowerRestored();
    for (CeilingLight* fixture : Core::Game::GetInstance()->GetObjects<CeilingLight>())
    {
        if (environmentLights.Count >= MAX_ENVIRONMENT_LIGHTS)
        {
            break;
        }

        const float brightness = fixture->GetBrightness();
        if (brightness <= 0.01f)
        {
            continue;
        }

        const Vector3 fixturePosition = fixture->GetPosition();
        ENVIRONMENT_POINT_LIGHT& pointLight =
            environmentLights.Lights[environmentLights.Count++];

        // Move the light slightly below the glowing panel to illuminate the room.
        pointLight.PositionRange = Vector4(
            fixturePosition.x, fixturePosition.y - 3.0f, fixturePosition.z,
            powerRestored ? 165.0f : 115.0f);

        if (powerRestored)
        {
            if (fixture->IsFaulted())
            {
                pointLight.ColorIntensity = Vector4(
                    0.70f, 0.78f, 0.56f, brightness * 1.08f);
            }
            else
            {
                pointLight.ColorIntensity = Vector4(
                    0.84f, 0.91f, 1.0f, brightness * 1.32f);
            }
        }
        else
        {
            pointLight.ColorIntensity = Vector4(
                1.0f, 0.055f, 0.025f, brightness * 1.05f);
        }
    }
    Renderer::SetEnvironmentLights(environmentLights);
	// カメラの位置と向きを更新
    const float horizontalSpeed = std::sqrt(
        m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z);
    const bool visiblyMoving = horizontalSpeed > 0.025f;
    if (visiblyMoving)
    {
        const float speedRate = (std::min)(
            horizontalSpeed / (m_MoveSpeed * SPRINT_SPEED_MULTIPLIER),
            1.0f);
        m_HeadBobTimer +=
            (m_IsSprinting ? 0.22f : 0.14f) * (0.55f + speedRate * 0.45f);
        const float amplitude =
            (m_IsSprinting ? 0.48f : 0.30f) * speedRate;
        const float verticalTarget =
            std::abs(sinf(m_HeadBobTimer)) * amplitude - amplitude * 0.48f;
        const float sideTarget =
            sinf(m_HeadBobTimer * 0.5f) * amplitude * 0.34f;
        m_HeadBobOffset +=
            (verticalTarget - m_HeadBobOffset) * 0.30f;
        m_HeadBobSideOffset +=
            (sideTarget - m_HeadBobSideOffset) * 0.24f;
    }
    else
    {
        m_HeadBobOffset *= 0.84f;
        m_HeadBobSideOffset *= 0.84f;
    }

    Vector3 eyePos = m_Position;
    eyePos.y += m_CameraHeightOffset;
    if (m_IsFPS)
    {
        // The slow component remains while standing still and makes the
        // viewpoint feel attached to a breathing person rather than a tripod.
        const float breath = sinf(m_AmbienceTimer * 1.15f) * 0.075f;
        eyePos.y += m_HeadBobOffset + breath;
        eyePos += right * m_HeadBobSideOffset;
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

    /*
	// バッテリーを回復するためのデバッグ用のキー入力
    if (Input::GetKeyTrigger(VK_B))
    {
        AddBattery(50.0f);
    }*/
}

void Player::Draw(Camera* cam)
{
    cam->SetCamera();

	// FPSモードの時はプレイヤーのモデルを描画しない(仮作成）
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

void Player::Uninit()
{}

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
