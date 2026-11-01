// ============================================================================
// ファイルの役割: Mayaで作った小物のモデル（tools/maya/build_props.py）を読み込み、各クラスの頂点とインデックスの配列へ足している。
// 主な技術: Assimp（StaticMesh経由）、マテリアルの名前で部位を選ぶ、部位ごとのインデックスの範囲、
//           頂点カラー（環境遮蔽）と部位の色の掛け合わせ、拡大率を戻した位置と法線
// ・これまで箱や円柱をコードで組んでいたクラスは、組み立ての代わりにこれを呼ぶだけで、描き方（頂点バッファ・
//   マテリアルごとの範囲・影・深度プリパス）はそのまま使える。
// ・モデルは拡大した後の実際の大きさで作ってある。ワールド行列で拡大する前の空間に戻すため、位置を標準の拡大率で割り、
//   法線も同じ率で割っている（ワールド行列を掛けたときに、元の面の向きに戻るようにするため）。
// ・同じファイルは1回だけ読み込み、2回目からは読み込んだ結果を使い回している。
// ============================================================================

#pragma once

#include "Renderer.h"

#include <SimpleMath.h>

#include <string>
#include <vector>

namespace PropModel
{
    // 使う部位（FBXのマテリアルの名前）と、その色
    struct Part
    {
        const char* Material;
        DirectX::SimpleMath::Color Color;
    };

    // 足した部位のインデックスの範囲
    struct IndexRange
    {
        unsigned int Start = 0;
        unsigned int Count = 0;
    };

    // モデルの指定した部位を、指定した順に配列の後ろへ足し、部位ごとのインデックスの範囲を返している。
    // 頂点の色 = 部位の色 × モデルの頂点カラー（環境遮蔽）。nominalScale はそのクラスがワールド行列で掛ける拡大率。
    // orientation はモデルを先に回す行列（箱の一番長い向きへ、モデルの長い向きを合わせるときに使う。nominalScale は回した後の向きで書く）。
    std::vector<IndexRange> Append(
        const std::string& path,
        const std::vector<Part>& parts,
        const DirectX::SimpleMath::Vector3& nominalScale,
        std::vector<VERTEX_3D>& vertices,
        std::vector<unsigned int>& indices,
        const DirectX::SimpleMath::Matrix& orientation = DirectX::SimpleMath::Matrix::Identity);

    // 範囲を1つにまとめている（続けて足した部位を、同じマテリアルで1回で描くとき）
    IndexRange Merge(const IndexRange& first, const IndexRange& last);
}
