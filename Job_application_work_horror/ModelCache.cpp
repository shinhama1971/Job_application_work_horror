// ============================================================================
// ファイルの役割: モデルのパスごとにAssimpで読み込んだ結果を持ち、使い回している。
// 主な技術: パスの正規化、unordered_map、shared_ptr、GPUのバッファの共有
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
    // キャッシュ本体（キー→モデル）と、デバッグ画面に出す数
    std::unordered_map<std::string, std::shared_ptr<ModelData>> g_ModelCache;
    std::uint64_t g_CacheHits = 0;
    std::uint64_t g_CacheMisses = 0;
    std::uint64_t g_AssimpLoads = 0;

    // 書き方が違っても同じファイルを同じキーにするため、絶対パスにして「.」「..」を整理し、小文字にそろえている
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

    // モデル・テクスチャのフォルダ・差し替えテクスチャの3つを「|」でつないだ文字列をキーにしている
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

// キャッシュにあればそれを返し、無ければAssimpで読み込んで、GPUのバッファ・マテリアル・テクスチャを作ってから登録している
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

    // StaticMeshが受け取ったunique_ptrは、ここで一度だけshared_ptrへ移している。
    // この後、同じモデルを使うObjectは同じテクスチャ（SRV）を参照している。
    auto importedTextures = staticMesh.GetTextures();
    modelData->Textures.reserve(importedTextures.size());
    for (auto& texture : importedTextures)
    {
        modelData->Textures.emplace_back(std::move(texture));
    }

    // 色の差し替え用テクスチャが指定されていれば読み込んでいる（モデルの色をテクスチャで置き換えるのに使っている）
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

// キャッシュを空にし、GPUの資源を解放している
void ModelCache::Clear()
{
    g_ModelCache.clear();
}

// デバッグ画面に出す数を返している
ModelCacheStats ModelCache::GetStats()
{
    ModelCacheStats stats;
    stats.LoadedModels = g_ModelCache.size();
    stats.CacheHits = g_CacheHits;
    stats.CacheMisses = g_CacheMisses;
    stats.AssimpLoads = g_AssimpLoads;
    return stats;
}
