// ============================================================================
// ファイルの役割: Assimpで読んだaiSceneから、頂点・インデックス・マテリアル・テクスチャを一時的な作業領域へ展開している。
// 主な技術: Assimp、左手座標系への変換、三角形化、unique_ptrによるTextureの所有権の受け渡し
// ============================================================================

#include	<vector>
#include	<iostream>
#include	<unordered_map>
#include	<cassert>
#include	"Texture.h"
#include	"AssimpPerse.h"
#include	"utility.h"

// Assimp側のCRT構成（静的CRT）と一致させ、Release版がデバッグランタイムに依存しないようにしている。
#if defined(_DEBUG)
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

namespace AssimpPerse
{
	// g_*は1回の読み込みだけに使う作業領域で、GetModelDataの先頭で毎回空にしている。
	// Textureだけはunique_ptrなので、GetTexturesで呼び出し元へ所有権を移している（移した後は空になる）。
	std::vector<std::vector<VERTEX>> g_vertices{};		// 頂点データ（メッシュごとの配列）
	std::vector<std::vector<unsigned int>> g_indices{};	// インデックスデータ（メッシュごとの配列）
	std::vector<SUBSET> g_subsets{};					// サブセット情報（メッシュごとの描画範囲とマテリアル）
	std::vector<MATERIAL> g_materials{};				// マテリアル（色と使うテクスチャ名）
	std::vector<std::unique_ptr<Texture>> g_textures;	// ディフューズテクスチャ（マテリアルと同じ添字で並べている）

	// unique_ptrはコピーできないため、Texture群はまとめてmoveで呼び出し元へ渡している。
	std::vector<std::unique_ptr<Texture>> GetTextures()
	{
		return std::move(g_textures);
	}

	// マテリアルの色とテクスチャを集めている。MaterialIndexと同じ添字で引けるよう、テクスチャの配列をマテリアル数に合わせている。
	void GetMaterialData(const aiScene* pScene, std::string texturedirectory)
	{
		// テクスチャのないマテリアルも添字がずれないよう、先に全要素を確保している。
		g_textures.resize(pScene->mNumMaterials);

		// マテリアルごとに、色とディフューズテクスチャを同じ添字の場所へ集めている。
		for (unsigned int m = 0; m < pScene->mNumMaterials; m++)
		{
			aiMaterial* material = pScene->mMaterials[m];

			// マテリアル名を取得し、確認用にコンソールへ出している
			std::string mtrlname = std::string(material->GetName().C_Str());
			std::cout << mtrlname << std::endl;

			// マテリアルの各色と光沢の強さ
			aiColor4D ambient;
			aiColor4D diffuse;
			aiColor4D specular;
			aiColor4D emission;
			float shininess;

			// アンビエント（環境光の色）。無ければ黒にしている
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &ambient)) {
			}
			else {
				ambient = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// ディフューズ（拡散反射の色）。無ければ白にして、テクスチャの色がそのまま出るようにしている
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse)) {
			}
			else {
				diffuse = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
			}

			// スペキュラ（鏡面反射の色）。無ければ黒にしている
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular)) {
			}
			else {
				specular = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// エミッション（自己発光の色）。無ければ黒にしている
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &emission)) {
			}
			else {
				emission = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// シャイネス（鏡面反射の鋭さ）。無ければ0にしている
			if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess)) {
			}
			else {
				shininess = 0.0f;
			}

			// このマテリアルに付いているディフューズテクスチャの数だけ繰り返している
			std::vector<std::string> texpaths{};

			for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_DIFFUSE); t++)
			{
				aiString path{};

				// t番目のテクスチャのパスを取得している
				if (AI_SUCCESS == material->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, t), path))
				{
					// テクスチャのパスを文字列にし、確認用にコンソールへ出している
					std::string texpath = std::string(path.C_Str());
					std::cout << texpath << std::endl;

					// パスに「:」が含まれていれば絶対パス（作った人のPCのパス）なので、ファイル名だけを取り出している
					if (texpath.find(':') != std::string::npos) {
						// スラッシュまたはバックスラッシュが最後に現れる位置を探している
						size_t pos = texpath.find_last_of("/\\");
						if (pos != std::string::npos)
						{
							// 最後の区切り文字の次から後ろ（ファイル名）だけを残している
							texpath = texpath.substr(pos + 1);
						}
					}
					texpaths.push_back(texpath);

					// FBXなどに埋め込まれた内蔵テクスチャかどうかを調べている
					if (auto tex = pScene->GetEmbeddedTexture(path.C_Str())) {

						std::unique_ptr<Texture> texture = std::make_unique<Texture>();

						// 内蔵テクスチャは、ファイルではなくメモリ上の画像データから読み込んでいる
						bool sts = texture->LoadFromMemory(
							(unsigned char*)tex->pcData,			// 画像データの先頭アドレス
							tex->mWidth);			// 圧縮画像の場合、mWidthに画像データのバイト数が入っている
						if (sts) {
							g_textures[m] = std::move(texture);
						}
						std::cout << "Embedded" << std::endl;

					}
					else {
						// 外部のテクスチャファイルは、モデルと同じフォルダ（texturedirectory）から読み込んでいる
						std::unique_ptr<Texture> texture;
						texture = std::make_unique<Texture>();

						std::string texname = texturedirectory + "/" + texpath;

						bool sts = texture->Load(texname);
						if (sts) {
							g_textures[m] = std::move(texture);
						}

						std::cout << "other Embedded" << std::endl;
					}
				}
				// テクスチャのパスを取得できなかった場合
				else
				{
					// 空のTextureを入れて、添字の対応だけは保っている
					std::unique_ptr<Texture> texture;
					texture = std::make_unique<Texture>();
					g_textures[m] = std::move(texture);
				}
			}

			// マテリアル情報を保存している（最初のディフューズテクスチャの名前も一緒に記録している）
			AssimpPerse::MATERIAL mtrl{};
			mtrl.mtrlname = mtrlname;
			mtrl.Ambient = ambient;
			mtrl.Diffuse = diffuse;
			mtrl.Specular = specular;
			mtrl.Emission = emission;
			mtrl.Shininess = shininess;
			if (texpaths.size() == 0)
			{
				mtrl.texturename = "";
			}
			else
			{
				mtrl.texturename = texpaths[0];
			}
			g_materials.push_back(mtrl);
		}
	}

	// モデルファイルを読み込み、頂点・インデックス・サブセット・マテリアルを作業領域へ展開している。
	// texturedirectoryは、モデルが参照するテクスチャを探すフォルダ。
	void GetModelData(std::string filename, std::string texturedirectory)
	{
		// 前回の読み込み結果が残らないよう、作業領域を空にしている
		g_vertices.clear();		// 頂点データ
		g_indices.clear();		// インデックスデータ
		g_subsets.clear();		// サブセット情報
		g_materials.clear();	// マテリアル
		g_textures.clear(); 	// ディフューズテクスチャ

		// 読み込み用のImporter（読み込んだaiSceneの寿命もImporterが持っている）
		Assimp::Importer importer;

		// ファイルを読み込み、シーン情報を作っている
		const aiScene* pScene = importer.ReadFile(
			filename.c_str(),
			aiProcess_ConvertToLeftHanded |	// DirectXに合わせて左手座標系に変換している
			aiProcess_Triangulate);			// 四角形以上の面を三角形に分割している

        // assertはReleaseで消えるため、読み込めないときはファイル名と理由を表示して終了している。
		if (pScene == nullptr)
		{
            utility::ReportFatalError(
                "モデルを読み込めませんでした。\n" + filename + "\n" +
                importer.GetErrorString());
		}

		// マテリアル情報を集めている（頂点がマテリアル名を参照するため、先に読んでいる）
		GetMaterialData(pScene, texturedirectory);

		// aiMeshはマテリアルを1つだけ持つため、メッシュ単位で頂点配列を作っている。
		g_vertices.resize(pScene->mNumMeshes);

		for (unsigned int m = 0; m < pScene->mNumMeshes; m++)
		{
			aiMesh* mesh = pScene->mMeshes[m];

			// メッシュ名を取得している
			std::string meshname = std::string(mesh->mName.C_Str());

			// 頂点の数だけ繰り返している
			for (unsigned int vidx = 0; vidx < mesh->mNumVertices; vidx++)
			{
				// 1頂点分のデータを作っている
				VERTEX	v{};
				v.meshname = meshname;		// どのメッシュの頂点かを覚えている

				// 位置
				v.pos = mesh->mVertices[vidx];

				// この頂点が属するメッシュのマテリアル番号を覚え、
				// その番号からマテリアル名も入れている
				v.materialindex = mesh->mMaterialIndex;

				v.mtrlname = g_materials[mesh->mMaterialIndex].mtrlname;

				// 法線があれば使い、無ければ0にしている
				if (mesh->HasNormals()) {
					v.normal = mesh->mNormals[vidx];
				}
				else
				{
					v.normal = aiVector3D(0.0f, 0.0f, 0.0f);
				}

				// 頂点カラー（0番目の組）があれば使い、無ければ白にしている
				if (mesh->HasVertexColors(0)) {
					v.color = mesh->mColors[0][vidx];
				}
				else
				{
					v.color = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
				}

				// テクスチャ座標（0番目の組）があれば使い、無ければ0にしている
				if (mesh->HasTextureCoords(0)) {
					v.texcoord = mesh->mTextureCoords[0][vidx];
				}
				else
				{
					v.texcoord = aiVector3D(0.0f, 0.0f, 0.0f);
				}

				// メッシュの頂点配列へ追加している
				g_vertices[m].push_back(v);
			}
		}

		// 三角形化済みの面を、メッシュ単位のインデックス配列へつなげている。
		g_indices.resize(pScene->mNumMeshes);
		for (unsigned int m = 0; m < pScene->mNumMeshes; m++)
		{
			aiMesh* mesh = pScene->mMeshes[m];

			// メッシュ名を取得している
			std::string meshname = std::string(mesh->mName.C_Str());

			// 面の数だけ繰り返している
			for (unsigned int fidx = 0; fidx < mesh->mNumFaces; fidx++)
			{
				aiFace face = mesh->mFaces[fidx];

				assert(face.mNumIndices == 3);	// aiProcess_Triangulateで三角形にしているため、頂点は必ず3つ

				// 面の3頂点のインデックスを追加している
				for (unsigned int i = 0; i < face.mNumIndices; i++)
				{
					g_indices[m].push_back(face.mIndices[i]);
				}
			}
		}

		// サブセット情報を作っている（メッシュごとの頂点数・インデックス数・マテリアル）
		g_subsets.resize(pScene->mNumMeshes);
		for (unsigned int m = 0; m < g_subsets.size(); m++)
		{
			g_subsets[m].IndexNum = (unsigned int)g_indices[m].size();
			g_subsets[m].VertexNum = (unsigned int)g_vertices[m].size();
			g_subsets[m].VertexBase = 0;
			g_subsets[m].IndexBase = 0;
			g_subsets[m].meshname = g_vertices[m][0].meshname;
			g_subsets[m].mtrlname = g_vertices[m][0].mtrlname;
			g_subsets[m].materialindex = g_vertices[m][0].materialindex;
		}

		// 全メッシュを1つの頂点・インデックスバッファにまとめるため、各サブセットの開始位置を計算している
		for (int m = 0; m < g_subsets.size(); m++)
		{
			// 頂点バッファの開始位置＝それより前のメッシュの頂点数の合計
			g_subsets[m].VertexBase = 0;
			for (int i = m - 1; i >= 0; i--) {
				g_subsets[m].VertexBase += g_subsets[i].VertexNum;
			}

			// インデックスバッファの開始位置＝それより前のメッシュのインデックス数の合計
			g_subsets[m].IndexBase = 0;
			for (int i = m - 1; i >= 0; i--) {
				g_subsets[m].IndexBase += g_subsets[i].IndexNum;
			}
		}
	}

	// サブセット情報を返している（コピーを返すので、作業領域はそのまま残る）
	std::vector<SUBSET> GetSubsets()
	{
		return g_subsets;
	}

	// 頂点データ（メッシュ単位）を返している
	std::vector<std::vector<VERTEX>> GetVertices()
	{
		return g_vertices; // 頂点データ（メッシュ単位）
	}

	// インデックスデータ（メッシュ単位）を返している
	std::vector<std::vector<unsigned int>> GetIndices()
	{
		return g_indices; // インデックスデータ（メッシュ単位）
	}

	// マテリアルを返している
	std::vector<MATERIAL> GetMaterials()
	{
		return g_materials; // マテリアル
	}
}
