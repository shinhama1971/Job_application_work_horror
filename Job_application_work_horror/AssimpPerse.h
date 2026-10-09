// ============================================================================
// ファイルの役割: Assimpの読み込み結果を一時的な作業領域へ展開し、StaticMeshへ渡す関数を宣言している。
// 主な技術: Assimp、左手座標系への変換、三角形化、unique_ptrによるTextureの所有権の受け渡し
// GetTexturesだけは内部のunique_ptrをStaticMeshへ移すため、呼んだ後は作業領域のテクスチャが空になる。
// ============================================================================

#pragma once

// Assimp 5.2.5のヘッダーはstd::minを括弧なしで呼んでいるため、先にWindows.hが
// 読み込まれているとmin/maxマクロと衝突する。読み込む間だけマクロを一時的に外している。
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include	<assimp/Importer.hpp>
#include	<assimp/scene.h>
#include	<assimp/postprocess.h>
#include	<assimp/cimport.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace AssimpPerse
{
	// Assimpから読んだ1頂点分のデータ（どのメッシュ・マテリアルに属するかも持っている）
	struct VERTEX {
		std::string meshname;		// メッシュ名
		aiVector3D	pos;			// 位置
		aiVector3D	normal;			// 法線
		aiColor4D	color;			// 頂点カラー
		aiVector3D	texcoord;		// テクスチャ座標
		int			materialindex;	// マテリアル番号
		std::string mtrlname;		// マテリアル名
	};

	// 1つのメッシュを描く範囲（まとめた頂点・インデックスバッファの中のどこからどこまでか）
	struct SUBSET {
		std::string meshname;		// メッシュ名
		int materialindex;			// マテリアル番号
		unsigned int VertexBase;	// 頂点バッファの中の開始位置
		unsigned int VertexNum;		// 頂点数
		unsigned int IndexBase;		// インデックスバッファの中の開始位置
		unsigned int IndexNum;		// インデックス数
		unsigned int IndexNum2;		// 現在はどこからも使っていない
		std::string	 mtrlname;		// マテリアル名
	};

	// マテリアルの色とテクスチャ名
	struct MATERIAL {
		std::string mtrlname;		// マテリアル名
		aiColor4D	Ambient;		// アンビエント（環境光の色）
		aiColor4D	Diffuse;		// ディフューズ（拡散反射の色）
		aiColor4D	Specular;		// スペキュラ（鏡面反射の色）
		aiColor4D	Emission;		// エミッション（自己発光の色）
		float		Shininess;		// シャイネス（鏡面反射の鋭さ）
		float       Padding[3];		// 詰め物（現在はどこからも使っていない）
		std::string texturename;	// ディフューズテクスチャのファイル名
	};

	// モデルファイルを読み込み、作業領域へ展開している
	void GetModelData(std::string filename, std::string texturedirectory);
	// 以下は展開した結果を返している（GetTexturesだけは所有権ごと移している）
	std::vector<SUBSET> GetSubsets();
	std::vector<std::vector<VERTEX>> GetVertices();
	std::vector<std::vector<unsigned int>> GetIndices();
	std::vector<MATERIAL> GetMaterials();
	std::vector<std::unique_ptr<Texture>> GetTextures();
}
