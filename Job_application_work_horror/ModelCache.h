// ============================================================================
// ファイルの役割: 同じモデルを複数のObjectで使うとき、読み込んだ結果とGPUの資源を共有している。
// 主な技術: パスの正規化、unordered_map、shared_ptr、Direct3D 11の資源の共有
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

// 読み込んだモデル1つ分（GPUのバッファ、マテリアル、サブセット、テクスチャ、色の差し替え用テクスチャ、ローカル座標の境界）
struct ModelData
{
    MeshRenderer Renderer;
    std::vector<MATERIAL> Materials;
    std::vector<SUBSET> Subsets;
    std::vector<std::shared_ptr<Texture>> Textures;
    std::shared_ptr<Texture> AlbedoOverride;
    std::shared_ptr<const ModelBounds> LocalBounds;
};

// デバッグ画面に出す数（読み込んだモデルの数、使い回せた回数、使い回せなかった回数、Assimpで読み込んだ回数）
struct ModelCacheStats
{
    std::size_t LoadedModels = 0;
    std::uint64_t CacheHits = 0;
    std::uint64_t CacheMisses = 0;
    std::uint64_t AssimpLoads = 0;
};

// モデルの読み込み窓口。2回目以降は同じModelDataを返している
class ModelCache
{
public:
    // モデルを読み込んでいる（同じパス・テクスチャのフォルダ・差し替えテクスチャの組み合わせなら、前の結果を返している）
    static std::shared_ptr<ModelData> Load(
        const std::string& modelPath,
        const std::string& textureDirectory = "",
        const std::string& albedoOverridePath = "");
    // デバッグ画面に出す数を返している
    static ModelCacheStats GetStats();
    // D3Dデバイスを壊す前に呼び、キャッシュが持っているGPUの資源を解放している。
    static void Clear();
};
