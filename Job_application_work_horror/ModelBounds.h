// ============================================================================
// ファイルの役割: モデル頂点から得たローカル境界と描画判定用の境界球を定義します。
// 主な技術: Local AABB、Bounding Sphere、SRT変換
// ============================================================================

#pragma once

#include <SimpleMath.h>

struct ModelBounds
{
    DirectX::SimpleMath::Vector3 Center =
        DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 Extents =
        DirectX::SimpleMath::Vector3::Zero;
    float SphereRadius = 0.0f;
    bool IsValid = false;
};

struct WorldBoundingSphere
{
    DirectX::SimpleMath::Vector3 Center =
        DirectX::SimpleMath::Vector3::Zero;
    float Radius = 0.0f;
};
