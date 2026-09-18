// ============================================================================
// ファイルの役割: モデルパスごとにAssimp読込結果を保持し、再利用します。
// 主な技術: パス正規化、unordered_map、shared_ptr、GPUバッファ共有
// ============================================================================

#include "ModelCache.h"

#include "StaticMesh.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>

namespace
{
    std::unordered_map<std::string, std::shared_ptr<ModelData>> g_ModelCache;
    std::uint64_t g_CacheHits = 0;
    std::uint64_t g_CacheMisses = 0;
    std::uint64_t g_AssimpLoads = 0;

    std::string NormalizePath(const std::string& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path normalizedPath(path);
        std::error_code pathError;
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(normalizedPath, pathError);
        if (!pathError)
        {
            normalizedPath = absolutePath;
        }

        std::string normalized =
            normalizedPath.lexically_normal().generic_string();
        std::transform(
            normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return normalized;
    }

    std::string MakeCacheKey(
        const std::string& modelPath,
        const std::string& textureDirectory,
        const std::string& albedoOverridePath)
    {
        return NormalizePath(modelPath) + "|" +
            NormalizePath(textureDirectory) + "|" +
            NormalizePath(albedoOverridePath);
    }
}

std::shared_ptr<ModelData> ModelCache::Load(
    const std::string& modelPath,
    const std::string& textureDirectory,
    const std::string& albedoOverridePath)
{
    const std::string cacheKey =
        MakeCacheKey(modelPath, textureDirectory, albedoOverridePath);
    const auto found = g_ModelCache.find(cacheKey);
    if (found != g_ModelCache.end())
    {
        ++g_CacheHits;
        std::cout << "[ModelCache] Hit: " << modelPath << std::endl;
        return found->second;
    }

    ++g_CacheMisses;
    ++g_AssimpLoads;
    std::cout << "[ModelCache] Miss: " << modelPath << std::endl;

    StaticMesh staticMesh;
    staticMesh.Load(modelPath, textureDirectory);

    auto modelData = std::make_shared<ModelData>();
    modelData->Renderer.Init(staticMesh);
    modelData->Materials = staticMesh.GetMaterials();
    modelData->Subsets = staticMesh.GetSubsets();
    modelData->LocalBounds =
        std::make_shared<ModelBounds>(staticMesh.GetModelBounds());

    // StaticMeshが受け取ったunique_ptrはここで一度だけshared_ptrへ移します。
    // 以後、同じモデルを使うObjectは同じTexture SRVを参照します。
    auto importedTextures = staticMesh.GetTextures();
    modelData->Textures.reserve(importedTextures.size());
    for (auto& texture : importedTextures)
    {
        modelData->Textures.emplace_back(std::move(texture));
    }

    if (!albedoOverridePath.empty())
    {
        auto albedo = std::make_shared<Texture>();
        if (albedo->Load(albedoOverridePath))
        {
            modelData->AlbedoOverride = std::move(albedo);
        }
    }

    g_ModelCache.emplace(cacheKey, modelData);
    return modelData;
}

ModelCacheStats ModelCache::GetStats()
{
    ModelCacheStats stats;
    stats.LoadedModels = g_ModelCache.size();
    stats.CacheHits = g_CacheHits;
    stats.CacheMisses = g_CacheMisses;
    stats.AssimpLoads = g_AssimpLoads;
    return stats;
}
