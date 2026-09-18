// ============================================================================
// ファイルの役割: 同じモデルを複数Objectで使うとき、読込結果とGPU資源を共有します。
// 主な技術: パス正規化、unordered_map、shared_ptr、Direct3D 11リソース共有
// ============================================================================

#pragma once

#include "MeshRenderer.h"
#include "ModelBounds.h"
#include "Texture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ModelData
{
    MeshRenderer Renderer;
    std::vector<MATERIAL> Materials;
    std::vector<SUBSET> Subsets;
    std::vector<std::shared_ptr<Texture>> Textures;
    std::shared_ptr<Texture> AlbedoOverride;
    std::shared_ptr<const ModelBounds> LocalBounds;
};

struct ModelCacheStats
{
    std::size_t LoadedModels = 0;
    std::uint64_t CacheHits = 0;
    std::uint64_t CacheMisses = 0;
    std::uint64_t AssimpLoads = 0;
};

class ModelCache
{
public:
    static std::shared_ptr<ModelData> Load(
        const std::string& modelPath,
        const std::string& textureDirectory = "",
        const std::string& albedoOverridePath = "");
    static ModelCacheStats GetStats();
};
