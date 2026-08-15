#include "BatteryItem.h"
#include "Game.h"
#include "Player.h"

using namespace DirectX::SimpleMath;

void BatteryItem::Init()
{
    StaticMesh staticmesh;

    // 仮でゴルフボールモデルを使用
    // 後で電池モデルに変更
    std::u8string modelFile = u8"assets/model/cylinder/cylinder.obj";
    std::string texDirectory = "assets/model/cylinder";

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

    m_Scale = Vector3(5.0f, 5.0f, 5.0f);
}

void BatteryItem::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    m_Rotation.y += 0.03f;
}

void BatteryItem::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected || player.GetBattery() >= 100.0f)
    {
        return;
    }

    player.AddBattery(m_RecoverValue);
    m_IsCollected = true;
}

void BatteryItem::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

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

void BatteryItem::Uninit()
{}
