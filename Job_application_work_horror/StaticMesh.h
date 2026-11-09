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

// Meshを継承し、モデルファイルから頂点とインデックスを作るクラス。
// Assimpで読んで変換した結果は cache/model/ に保存し、次からはそれを読んでいる（元のモデルの大きさか更新日時が変わったら読み直す）。
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
	// Assimpで読み込み、ゲームで使う形に変換している（変換した結果を保存してよいかを返している）
	bool LoadWithAssimp(const std::string& filename, const std::string& texturedirectory);
	// 変換済みの形式（cache/model/）を読み書きしている。読めないとき・元のモデルと合わないときは false
	bool LoadConverted(const std::string& filename, const std::string& texturedirectory);
	void SaveConverted(const std::string& filename) const;
	// 全頂点を囲む箱から、境界（中心・半分の大きさ・境界球の半径）を求めている
	void UpdateModelBounds();

	std::vector<MATERIAL> m_materials;	    // マテリアルの情報
	std::vector<std::string> m_texturenames;			// テクスチャの名前
	std::vector<SUBSET> m_subsets;						// サブセットの情報
	std::vector<std::unique_ptr<Texture>>	m_textures;	// テクスチャ
	// ローカル座標の境界（AABBと境界球）
	ModelBounds m_modelBounds;
};
