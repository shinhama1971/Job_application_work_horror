// ============================================================================
// ファイルの役割: OBJ・FBXなどの動かないモデルを読み込み、描画用のデータ（頂点・インデックス・マテリアル・テクスチャ）として持っている。
// 主な技術: Assimp、GPUのバッファへ渡す形への変換、マテリアル、インデックスを使った描画
// ============================================================================

#pragma once

#include	<simplemath.h>
#include	<string>
#include	<vector>
#include	<memory>
#include	"Texture.h"
#include	"Mesh.h"
#include	"ModelBounds.h"
#include	"renderer.h"

// Meshを継承し、モデルファイルから頂点とインデックスを作るクラス
class StaticMesh : public Mesh {
public:
	// モデルを読み込んでいる（texturedirectoryはテクスチャを探すフォルダ）
	void Load(std::string filename, std::string texturedirectory="");

	// マテリアル・サブセット・テクスチャの名前を返している
	const std::vector<MATERIAL>& GetMaterials() {
		return m_materials;
	}

	const std::vector<SUBSET>& GetSubsets() {
		return m_subsets;
	}

	const std::vector<std::string>& GetTextureNames() {
		return m_texturenames;
	}

	// テクスチャを所有権ごと返している（呼んだ後は空になる）
	std::vector<std::unique_ptr<Texture>> GetTextures() {
		return std::move(m_textures);
	}

	// モデルの頂点から求めた、ローカル座標の境界を返している
	const ModelBounds& GetModelBounds() const {
		return m_modelBounds;
	}

private:

	std::vector<MATERIAL> m_materials;	    // マテリアルの情報
	std::vector<std::string> m_texturenames;			// テクスチャの名前
	std::vector<SUBSET> m_subsets;						// サブセットの情報
	std::vector<std::unique_ptr<Texture>>	m_textures;	// テクスチャ
	// ローカル座標の境界（AABBと境界球）
	ModelBounds m_modelBounds;
};
