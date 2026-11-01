// ============================================================================
// ファイルの役割: Mayaで作った小物のモデルを読み込み、部位ごとに頂点とインデックスを各クラスの配列へ足している。
// 主な技術: Assimp（StaticMesh経由）、読み込んだ結果の使い回し、拡大率を戻した位置と法線
// ============================================================================

#include "PropModel.h"

#include "StaticMesh.h"

#include <memory>
#include <unordered_map>

using namespace DirectX::SimpleMath;

namespace
{
    // 読み込んだモデル1つ分（全部のメッシュの頂点・インデックスと、サブセット）
    struct LoadedModel
    {
        std::vector<VERTEX_3D> Vertices;
        std::vector<unsigned int> Indices;
        std::vector<SUBSET> Subsets;
    };

    const LoadedModel& LoadModel(const std::string& path)
    {
        static std::unordered_map<std::string, std::unique_ptr<LoadedModel>> cache;
        auto found = cache.find(path);
        if (found != cache.end())
        {
            return *found->second;
        }
        StaticMesh mesh;
        mesh.Load(path);
        auto model = std::make_unique<LoadedModel>();
        model->Vertices = mesh.GetVertices();
        model->Indices = mesh.GetIndices();
        model->Subsets = mesh.GetSubsets();
        return *cache.emplace(path, std::move(model)).first->second;
    }
}

namespace PropModel
{
    std::vector<IndexRange> Append(
        const std::string& path,
        const std::vector<Part>& parts,
        const Vector3& nominalScale,
        std::vector<VERTEX_3D>& vertices,
        std::vector<unsigned int>& indices)
    {
        const LoadedModel& model = LoadModel(path);
        std::vector<IndexRange> ranges;
        ranges.reserve(parts.size());
        for (const Part& part : parts)
        {
            IndexRange range;
            range.Start = static_cast<unsigned int>(indices.size());
            for (const SUBSET& subset : model.Subsets)
            {
                if (subset.MtrlName != part.Material)
                {
                    continue;
                }
                const unsigned int base = static_cast<unsigned int>(vertices.size());
                for (unsigned int index = 0; index < subset.VertexNum; ++index)
                {
                    VERTEX_3D vertex = model.Vertices[subset.VertexBase + index];
                    vertex.position = vertex.position / nominalScale;
                    vertex.normal = vertex.normal / nominalScale;
                    vertex.normal.Normalize();
                    // 頂点カラーは環境遮蔽（灰色）なので、部位の色に掛けている
                    vertex.color = Color(
                        part.Color.x * vertex.color.x,
                        part.Color.y * vertex.color.y,
                        part.Color.z * vertex.color.z,
                        part.Color.w);
                    vertices.push_back(vertex);
                }
                for (unsigned int index = 0; index < subset.IndexNum; ++index)
                {
                    indices.push_back(base + model.Indices[subset.IndexBase + index]);
                }
            }
            range.Count = static_cast<unsigned int>(indices.size()) - range.Start;
            ranges.push_back(range);
        }
        return ranges;
    }

    IndexRange Merge(const IndexRange& first, const IndexRange& last)
    {
        IndexRange merged;
        merged.Start = first.Start;
        merged.Count = last.Start + last.Count - first.Start;
        return merged;
    }
}
