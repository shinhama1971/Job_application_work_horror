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
}

void Player::Update()
{
    if (!m_CanControl)
    {
        return;
    }

    Camera* cam = Core::Game::GetInstance()->GetCamera();

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
    const bool isSprinting = isMoving &&
        (Input::GetKeyPress(VK_SHIFT) ||
         Input::GetButtonPress(XINPUT_LEFT_THUMB));
    const float currentMoveSpeed = m_MoveSpeed *
        (isSprinting ? SPRINT_SPEED_MULTIPLIER : 1.0f);

    if (isMoving)
    {
        if (moveLengthSquared > 1.0f)
        {
            moveDir.Normalize();
        }

        m_Velocity.x = moveDir.x * currentMoveSpeed;
        m_Velocity.z = moveDir.z * currentMoveSpeed;
    }
    else
    {
        m_Velocity.x = 0.0f;
        m_Velocity.z = 0.0f;
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

	// バッテリー残量が20%以下の時に点滅するようにする
    bool visibleLight = m_FlashLightOn;

    if (m_FlashLightOn && m_Battery <= 20.0f)
    {
        m_FlickerTimer++;

		// 10フレームごとに点滅するようにする
        if ((m_FlickerTimer / 10) % 2 == 0)
        {
            visibleLight = true;
        }
        else
        {
            visibleLight = false;
        }
    }
    else
    {
        m_FlickerTimer = 0;
    }

    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = visibleLight ? TRUE : FALSE;
    light.Intensity = visibleLight ? 1.6f : 0.0f;
    light.Range = 260.0f;
    light.Direction = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(16.0f)),
        cosf(DirectX::XMConvertToRadians(32.0f)),
        1.35f,
        0.0f
    );

    if (visibleLight)
    {
        light.Diffuse = Color(m_LightDiffuseR, m_LightDiffuseG, m_LightDiffuseB, 1.0f);
        light.Ambient = Color(0.074f, 0.071f, 0.065f, 1.0f);
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        light.Ambient = Color(
            0.074f, // Readable baseline even without the flashlight.
            0.071f,
            0.065f,
            1.0f
        );
    }

    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = Color(0.105f, 0.098f, 0.082f, 1.0f);
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
            powerRestored ? 145.0f : 82.0f);

        if (powerRestored)
        {
            pointLight.ColorIntensity = Vector4(
                1.0f, 0.78f, 0.52f, brightness * 1.40f);
        }
        else
        {
            pointLight.ColorIntensity = Vector4(
                1.0f, 0.055f, 0.025f, brightness * 0.58f);
        }
    }
    Renderer::SetEnvironmentLights(environmentLights);
	// カメラの位置と向きを更新
    if (isMoving)
    {
        m_HeadBobTimer += isSprinting ? 0.22f : 0.14f;
        const float amplitude = isSprinting ? 0.55f : 0.35f;
        const float targetOffset = sinf(m_HeadBobTimer) * amplitude;
        m_HeadBobOffset += (targetOffset - m_HeadBobOffset) * 0.35f;
    }
    else
    {
        m_HeadBobOffset *= 0.82f;
    }

    Vector3 eyePos = m_Position;
    eyePos.y += m_CameraHeightOffset;
    if (m_IsFPS)
    {
        eyePos.y += m_HeadBobOffset;
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
