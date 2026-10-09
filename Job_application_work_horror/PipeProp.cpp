// ============================================================================
// ファイルの役割: Pipe90.fbxを読み込み、本描画・影・反射の各描画で描いている。
// 主な技術: Assimp（ModelCache経由）、静的メッシュ、ディフューズテクスチャ、シャドウマップ
// ============================================================================

#include "PipeProp.h"

#include "Game.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

// モデルをキャッシュから読み込み、色のテクスチャ（Albedo）を貼るマテリアルを作っている
void PipeProp::Init()
{
    const std::string modelDirectory = "assets/model/pipe";
    m_ModelData = ModelCache::Load(
        modelDirectory + "/Pipe90.fbx",
        modelDirectory,
        modelDirectory + "/Pipe90_DefaultMaterial_AlbedoTransparency.jpg");

    SetModelBounds(m_ModelData->LocalBounds);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // FBXの中の高度なマテリアルは今のRendererでは扱えないので、Albedo（色）のテクスチャだけをはっきり指定して使っている。
    const bool albedoLoaded = m_ModelData->AlbedoOverride != nullptr;

    std::vector<MATERIAL> importedMaterials = m_ModelData->Materials;
    if (importedMaterials.empty())
    {
        importedMaterials.emplace_back();
    }

    for (MATERIAL& materialData : importedMaterials)
    {
        // Albedoの色をそのまま見せつつ、今のライトの拡散反射・鏡面反射を受けるようにしている。
        materialData.Ambient = Color(0.12f, 0.12f, 0.12f, 1.0f);
        materialData.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
        materialData.Specular = Color(0.18f, 0.18f, 0.18f, 1.0f);
        materialData.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
        materialData.Shininess = 24.0f;
        materialData.TextureEnable = albedoLoaded ? TRUE : FALSE;

        auto material = std::make_unique<Material>();
        material->Create(materialData);
        material->SetShader(&m_Shader);
        if (albedoLoaded)
        {
            material->SetTexture(m_ModelData->AlbedoOverride.get());
        }
        m_Materials.emplace_back(std::move(material));
    }
}

Matrix PipeProp::GetWorldMatrix() const
{
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    return scale * rotation * translation;
}

// サブセットごとに、対応するマテリアルを設定して描いている
void PipeProp::Draw(Camera* camera)
{
    camera->SetCamera();
    Matrix world = GetWorldMatrix();
    Renderer::SetWorldMatrix(&world);
    m_ModelData->Renderer.BeforeDraw();

    for (const SUBSET& subset : m_ModelData->Subsets)
    {
        const size_t materialIndex = static_cast<size_t>(subset.MaterialIdx);
        if (materialIndex >= m_Materials.size())
        {
            continue;
        }

        m_Materials[materialIndex]->SetGPU();
        m_ModelData->Renderer.DrawSubset(
            subset.IndexNum,
            subset.IndexBase,
            subset.VertexBase);
    }
}

// 影：ライトから見た深度だけを描くので、マテリアルは使わず全サブセットを描いている
void PipeProp::DrawShadow()
{
    Matrix world = GetWorldMatrix();
    Renderer::SetWorldMatrix(&world);
    m_ModelData->Renderer.BeforeDraw();
    Core::Game::GetInstance()->GetShadowMap()->SetShader();

    for (const SUBSET& subset : m_ModelData->Subsets)
    {
        m_ModelData->Renderer.DrawSubset(
            subset.IndexNum,
            subset.IndexBase,
            subset.VertexBase);
    }
}

// マテリアルを解放し、共有しているモデルの参照を手放している
void PipeProp::Uninit()
{
    m_Materials.clear();
    m_ModelData.reset();
}
