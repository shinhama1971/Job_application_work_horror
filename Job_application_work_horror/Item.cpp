#include "Item.h"
#include "ShadowMan.h"

using namespace DirectX::SimpleMath;

void Item::Init()
{
    StaticMesh staticmesh;

    // 今はゴルフボールで代用
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

    // 確認用に大きくする
    m_Scale = Vector3(5.0f, 5.0f, 5.0f);
}

void Item::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    // 回転
    m_Rotation.y += 0.03f;
}

void Item::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }

    m_IsCollected = true;
    Core::Game::GetInstance()->AddItemCount();

    if (Core::Game::GetInstance()->GetItemCount() == 1)
    {
        const Vector3 shadowPosition(
            player.GetPosition().x,
            player.GetPosition().y,
            player.GetPosition().z - 80.0f
        );

        Core::Game::GetInstance()->RequestAddObject<ShadowMan>(
            [shadowPosition](ShadowMan& shadow)
            {
                shadow.SetPosition(
                    shadowPosition.x,
                    shadowPosition.y,
                    shadowPosition.z
                );
            }
        );
    }
}

void Item::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

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

void Item::Uninit()
{}
