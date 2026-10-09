// ============================================================================
// ファイルの役割: モデルの頂点から求めた、ローカル座標の境界（箱と球）と、描画の判定に使うワールド座標の境界球を定義している。
// 主な技術: ローカル座標のAABB、境界球、拡大・回転・移動の変換
// ============================================================================

#pragma once

#include <SimpleMath.h>

// モデルの頂点から求めた、ローカル座標の境界（中心・各軸の半分の大きさ・境界球の半径・求められたか）
struct ModelBounds
{
    DirectX::SimpleMath::Vector3 Center =
        DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 Extents =
        DirectX::SimpleMath::Vector3::Zero;
    float SphereRadius = 0.0f;
    bool IsValid = false;
};

// ワールド座標に変換した境界球（カリングに使っている）
struct WorldBoundingSphere
{
    DirectX::SimpleMath::Vector3 Center =
        DirectX::SimpleMath::Vector3::Zero;
    float Radius = 0.0f;
};
