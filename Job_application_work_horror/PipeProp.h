// ============================================================================
// ファイルの役割: 外部FBXの配管モデルを配置する背景用Objectです。
// 主な技術: Assimp、静的メッシュ、Diffuse Texture、Shadow Map
// ============================================================================

#pragma once

#include "Object.h"
#include "Material.h"
#include "ModelCache.h"

#include <memory>
#include <vector>

class PipeProp final : public Object
{
private:
    std::shared_ptr<ModelData> m_ModelData;
    std::vector<std::unique_ptr<Material>> m_Materials;

    DirectX::SimpleMath::Matrix GetWorldMatrix() const;

public:
    void Init() override;
    void Update() override {}
    void Draw(Camera* camera) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return true; }
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;
};
