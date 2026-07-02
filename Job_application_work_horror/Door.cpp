#include "Door.h"
#include "Game.h"
#include "Player.h"
#include "Input.h"

using namespace DirectX::SimpleMath;

void Door::Init()
{
    StaticMesh staticmesh;

    // 仮モデル
    // 後でドアモデルに変更
    std::u8string modelFile = u8"assets/model/golf_ball/golf_ball.obj";
    std::string texDirectory = "assets/model/golf_ball";

    std::string tmpStr(
        reinterpret_cast<const char*>(modelFile.c_str()),
        modelFile.size()
    );

    staticmesh.Load(tmpStr, texDirectory);

    m_MeshRenderer.Init(staticmesh);

    m_Shader.Create(
        "shader/litTextureVS.hlsl",
        "shader/litTexturePS.hlsl"
    );

    m_subsets = staticmesh.GetSubsets();
    m_Textures = staticmesh.GetTextures();

    std::vector<MATERIAL> materials = staticmesh.GetMaterials();

    for (int i = 0; i < materials.size(); i++)
    {
        std::unique_ptr<Material> m =
            std::make_unique<Material>();

        m->Create(materials[i]);
        m->SetShader(&m_Shader);

        m_Materials.push_back(std::move(m));
    }

    size_t count =
        (std::min)(m_Materials.size(), m_Textures.size());

    for (size_t i = 0; i < count; i++)
    {
        m_Materials[i]->SetTexture(m_Textures[i].get());
    }

    // 仮でドアっぽく縦長にする
    m_Scale = Vector3(10.0f, 30.0f, 5.0f);
}

void Door::Update()
{
    // 開いている途中
    if (m_IsOpening)
    {
        Vector3 dir = m_OpenPosition - m_Position;
        Vector3 open = m_OpenPosition - m_Position;//


        if (dir.Length() <= m_OpenSpeed)
        {
            m_Position = m_OpenPosition;
            m_IsOpen = true;
            m_IsOpening = false;

            return;
        }

        dir.Normalize();
        m_Position += dir * m_OpenSpeed;
        return;
    }

    if (m_IsOpen) return;

    std::vector<Player*> players =
        Core::Game::GetInstance()->GetObjects<Player>();

    if (players.empty()) return;

    Player* player = players[0];
   
    Vector3 diff = player->GetPosition() - m_Position;
    float distance = diff.Length();

    // ドアの近く
    if (distance <= m_OpenDistance)
    {
        // アイテム3個以上持っていたらドアを開けることができる
        if (Core::Game::GetInstance()->GetItemCount() >= 3)
        {
            if (Input::GetKeyTrigger(VK_E))
            {
                m_IsOpening = true;
            }
        }
    }
}

void Door::Draw(Camera* cam)
{
    //if (m_IsOpen) return;

    cam->SetCamera();

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );

    Matrix t = Matrix::CreateTranslation(m_Position);

    Matrix s = Matrix::CreateScale(
        m_Scale.x,
        m_Scale.y,
        m_Scale.z
    );

    Matrix worldmtx = s * r * t;

    Renderer::SetWorldMatrix(&worldmtx);

    m_MeshRenderer.BeforeDraw();

    for (int i = 0; i < m_subsets.size(); i++)
    {
        int matIdx = m_subsets[i].MaterialIdx;

        if (matIdx < 0 || matIdx >= m_Materials.size())
        {
            continue;
        }

        m_Materials[matIdx]->SetGPU();

        if (matIdx < m_Textures.size() &&
            m_Textures[matIdx] != nullptr)
        {
            m_Textures[matIdx]->SetGPU();
        }

        m_MeshRenderer.DrawSubset(
            m_subsets[i].IndexNum,
            m_subsets[i].IndexBase,
            m_subsets[i].VertexBase
        );
    }
}

void Door::Uninit()
{}