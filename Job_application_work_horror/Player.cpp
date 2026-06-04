#include "Player.h"
#include "Game.h"
#include "Input.h"
#include "Camera.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

void Player::Init()
{
    StaticMesh staticmesh;

    // 仮モデル。あとで人型や懐中電灯持ちモデルに変更
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
    Camera* cam = Game::GetInstance()->GetCamera();

    float yaw = cam->GetCameraDirection();

    Vector3 forward(sinf(yaw), 0.0f, cosf(yaw));
    Vector3 right(sinf(yaw + 1.5708f), 0.0f, cosf(yaw + 1.5708f));

    Vector3 moveDir = Vector3::Zero;

    if (Input::GetKeyPress(VK_W)) moveDir += forward;
    if (Input::GetKeyPress(VK_S)) moveDir -= forward;
    if (Input::GetKeyPress(VK_A)) moveDir -= right;
    if (Input::GetKeyPress(VK_D)) moveDir += right;

    if (moveDir.LengthSquared() > 0.0f)
    {
        moveDir.Normalize();
        m_Velocity.x = moveDir.x * m_MoveSpeed;
        m_Velocity.z = moveDir.z * m_MoveSpeed;
    }
    else
    {
        m_Velocity.x = 0.0f;
        m_Velocity.z = 0.0f;
    }

    // 重力
    m_Velocity.y -= 0.01f;

    m_Position += m_Velocity;

    // 仮の落下防止
    if (m_Position.y < -99.0f)
    {
        m_Position.y = -99.0f;
        m_Velocity.y = 0.0f;
    }

    // Rキーで一人称
    if (Input::GetKeyTrigger(VK_R))
    {
        m_IsFPS = true;
    }

    // Lキーで三人称
    if (Input::GetKeyTrigger(VK_J))
    {
        m_IsFPS = false;
    }


    // Fキーで懐中電灯ON/OFF
    if (Input::GetKeyTrigger(VK_F))
    {
        m_FlashLightOn = !m_FlashLightOn;
    }

    // カメラ処理
    Vector3 eyePos = m_Position;
    eyePos.y += 2.0f;


    // 懐中電灯ONなら電池を減らす
    if (m_FlashLightOn)
    {
        m_Battery -= 0.02f;

        if (m_Battery <= 0.0f)
        {
            m_Battery = 0.0f;
            m_FlashLightOn = false;
        }
    }

    LIGHT light{};

    if (m_FlashLightOn)
    {
        light.Enable = true;
        light.Direction = DirectX::SimpleMath::Vector4(0.5f, -1.0f, 0.8f, 0.0f);
        light.Direction.Normalize();
        light.Diffuse = DirectX::SimpleMath::Color(1.2f, 1.2f, 1.2f, 1.0f);
        light.Ambient = DirectX::SimpleMath::Color(0.15f, 0.15f, 0.15f, 1.0f);
    }
    else
    {
        light.Enable = true;
        light.Direction = DirectX::SimpleMath::Vector4(0.5f, -1.0f, 0.8f, 0.0f);
        light.Direction.Normalize();
        light.Diffuse = DirectX::SimpleMath::Color(0.2f, 0.2f, 0.2f, 1.0f);
        light.Ambient = DirectX::SimpleMath::Color(0.02f, 0.02f, 0.02f, 1.0f);
    }

    Renderer::SetLight(light);

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
        float distance = 8.0f;
        float height = 3.0f;

        Vector3 camPos = m_Position;
        camPos -= forward * distance;
        camPos.y += height;

        cam->SetPosition(camPos);

        Vector3 target = m_Position;
        target.y += 1.5f;
        cam->SetTarget(target);
    }
}

void Player::Draw(Camera* cam)
{
    cam->SetCamera();

    // 一人称のときは自分のモデルを描画しない
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
{
}
