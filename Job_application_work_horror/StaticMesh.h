// ============================================================================
// ファイルの役割: OBJなどの静的モデルを読み込み、描画用データとして保持します。
// 主な技術: Assimp、GPUバッファ、マテリアル、インデックス描画
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
// ============================================================================

#pragma once

#include	<simplemath.h>
#include	<string>
#include	<vector>
#include	<memory>
#include	"Texture.h"
#include	"Mesh.h"
#include	"renderer.h"

class StaticMesh : public Mesh {
public:
	void Load(std::string filename, std::string texturedirectory="");

	const std::vector<MATERIAL>& GetMaterials() {
		return m_materials;
	}

	const std::vector<SUBSET>& GetSubsets() {
		return m_subsets;
	}

	const std::vector<std::string>& GetTextureNames() {
		return m_texturenames;
	}

	std::vector<std::unique_ptr<Texture>> GetTextures() {
		return std::move(m_textures);
	}

private:

	std::vector<MATERIAL> m_materials;	    // マテリアル情報
	std::vector<std::string> m_texturenames;			// テクスチャ名
	std::vector<SUBSET> m_subsets;						// サブセット情報
	std::vector<std::unique_ptr<Texture>>	m_textures;	// テクスチャ群
};