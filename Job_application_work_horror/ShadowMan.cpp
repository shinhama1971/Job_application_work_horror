#include "ShadowMan.h"
#include "Game.h"
#include"Player.h"
using namespace DirectX::SimpleMath;

void ShadowMan::Init()
{
    StaticMesh staticmesh;

    // 仮モデル。あとで人影モデルに変更
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

    // 人影っぽく縦長にする
    m_Scale = Vector3(8.0f, 35.0f, 8.0f);
}

void ShadowMan::Update()
{
    m_LifeTimer--;

    Player* player = Core::Game::GetInstance()->GetObjects<Player>()[0];

    Vector3 toShadow =
        m_Position - player->GetPosition();

    toShadow.Normalize();

    float dot =
        player->GetForward().Dot(toShadow);

   // if (dot > 0.8f)
    //{
     //   m_LifeTimer = 0;
    //}
}

void ShadowMan::Draw(Camera* cam)
{
    if (m_LifeTimer <= 0) return;

    cam->SetCamera();

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

void ShadowMan::Uninit()
{}