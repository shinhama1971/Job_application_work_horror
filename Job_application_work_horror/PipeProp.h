// ============================================================================
// ファイルの役割: 外部のFBXの配管モデルを配置する、背景用のObject。
// 主な技術: Assimp（ModelCache経由）、静的メッシュ、ディフューズテクスチャ、シャドウマップ
// ============================================================================

#pragma once

#include "Object.h"
#include "Material.h"
#include "ModelCache.h"

#include <memory>
#include <vector>

// 天井や壁を走る配管。ModelCacheで同じモデルを共有し、影を落とし、水面にも映している。
class PipeProp final : public Object
{
private:
    // 共有しているモデルのデータと、このObjectのマテリアル
    std::shared_ptr<ModelData> m_ModelData;
    std::vector<std::unique_ptr<Material>> m_Materials;

    // 拡大・回転・移動のワールド行列を作っている
    DirectX::SimpleMath::Matrix GetWorldMatrix() const;

public:
    void Init() override;
    // 動かないので、毎フレームの処理はない
    void Update() override {}
    void Draw(Camera* camera) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return true; }
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;
};
