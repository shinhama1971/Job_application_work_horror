#include "Door.h"

#include "Game.h"
#include "Player.h"

using namespace DirectX::SimpleMath;

void Door::Init()
{
    StaticMesh staticMesh;

    const std::u8string modelFile = u8"assets/model/golf_ball/golf_ball.obj";
    const std::string textureDirectory = "assets/model/golf_ball";
    const std::string modelPath(
        reinterpret_cast<const char*>(modelFile.c_str()),
        modelFile.size()
    );

    staticMesh.Load(modelPath, textureDirectory);
    m_MeshRenderer.Init(staticMesh);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_subsets = staticMesh.GetSubsets();
    m_Textures = staticMesh.GetTextures();

    for (const MATERIAL& material : staticMesh.GetMaterials())
    {
        auto instance = std::make_unique<Material>();
        instance->Create(material);
        instance->SetShader(&m_Shader);
        m_Materials.push_back(std::move(instance));
    }

    const size_t count = (std::min)(m_Materials.size(), m_Textures.size());
    for (size_t i = 0; i < count; ++i)
    {
        m_Materials[i]->SetTexture(m_Textures[i].get());
    }

    m_Scale = Vector3(10.0f, 30.0f, 5.0f);
}

void Door::Update()
{
    if (!m_IsOpening)
    {
        return;
    }

    Vector3 direction = m_OpenPosition - m_Position;
    if (direction.Length() <= m_OpenSpeed)
    {
        m_Position = m_OpenPosition;
        m_IsOpen = true;
        m_IsOpening = false;
        return;
    }

    direction.Normalize();
    m_Position += direction * m_OpenSpeed;
}

const char* Door::GetInteractionPrompt() const
{
    return Core::Game::GetInstance()->GetItemCount() < 3
        ? "Requires 3 fuses"
        : "Open door";
}

void Door::Interact(Player& player)
{
    (void)player;

    if (!m_IsOpen && !m_IsOpening &&
        Core::Game::GetInstance()->GetItemCount() >= 3)
    {
        m_IsOpening = true;
    }
}

void Door::Draw(Camera* camera)
{
    camera->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    Matrix world = scale * rotation * translation;

    Renderer::SetWorldMatrix(&world);
    m_MeshRenderer.BeforeDraw();

    for (const SUBSET& subset : m_subsets)
    {
        const size_t materialIndex = subset.MaterialIdx;
        if (materialIndex >= m_Materials.size())
        {
            continue;
        }

        m_Materials[materialIndex]->SetGPU();

        if (materialIndex < m_Textures.size() && m_Textures[materialIndex] != nullptr)
        {
            m_Textures[materialIndex]->SetGPU();
        }

        m_MeshRenderer.DrawSubset(
            subset.IndexNum,
            subset.IndexBase,
            subset.VertexBase
        );
    }
}

void Door::Uninit()
{}
