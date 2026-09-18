// ============================================================================
// ファイルの役割: 外部FBXを既存のAssimp描画経路で確認するテスト用Objectです。
// 主な技術: Assimp、静的メッシュ、Diffuse Texture、Shadow Map
// ============================================================================

#pragma once

#include "Object.h"
#include "Material.h"
#include "ModelCache.h"

#include <memory>
#include <vector>

class TestPipe final : public Object
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
