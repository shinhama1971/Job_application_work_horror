// ============================================================================
// ファイルの役割: aiSceneから頂点・Index・Material・Textureを一時キャッシュへ展開します。
// 主な技術: Assimp、左手座標変換、三角形化、Texture所有権移譲
// ============================================================================

#include	<vector>
#include	<iostream>
#include	<unordered_map>
#include	<cassert>
#include	"Texture.h"
#include	"AssimpPerse.h"

// Assimp側のCRT構成と一致させ、Release版がデバッグランタイムへ依存するのを防ぎます。
#if defined(_DEBUG)
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

namespace AssimpPerse
{
	// g_*は1回のLoadだけに使う作業領域で、GetModelDataの先頭で初期化します。
	// Textureだけはunique_ptrなのでGetTexturesで呼び出し元へ所有権を移します。
	std::vector<std::vector<VERTEX>> g_vertices{};		// 頂点データ
	std::vector<std::vector<unsigned int>> g_indices{};	// インデックスデータ
	std::vector<SUBSET> g_subsets{};					// サブセット情報
	std::vector<MATERIAL> g_materials{};				// マテリアル
	std::vector<std::unique_ptr<Texture>> g_textures;	// ディフューズテクスチャ群

	// unique_ptrをコピーできないため、Texture群はまとめて呼び出し元へ移します。
	std::vector<std::unique_ptr<Texture>> GetTextures()
	{
		return std::move(g_textures);
	}

	// MaterialIndexと同じ添字で参照できるよう、Texture領域をMaterial数に合わせます。
	void GetMaterialData(const aiScene* pScene, std::string texturedirectory)
	{
		// TextureがないMaterialも添字を維持するため、先に全要素を確保します。
		g_textures.resize(pScene->mNumMaterials);

		// Materialごとに色とDiffuse Textureを同じ添字へ収集します。
		for (unsigned int m = 0; m < pScene->mNumMaterials; m++)
		{
			aiMaterial* material = pScene->mMaterials[m];

			// マテリアル名取得
			std::string mtrlname = std::string(material->GetName().C_Str());
			std::cout << mtrlname << std::endl;

			// マテリアル情報
			aiColor4D ambient;
			aiColor4D diffuse;
			aiColor4D specular;
			aiColor4D emission;
			float shininess;

			// アンビエント
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &ambient)) {
			}
			else {
				ambient = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// ディフューズ
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse)) {
			}
			else {
				diffuse = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
			}

			// スペキュラ
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular)) {
			}
			else {
				specular = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// エミッション
			if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &emission)) {
			}
			else {
				emission = aiColor4D(0.0f, 0.0f, 0.0f, 0.0f);
			}

			// シャイネス
			if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess)) {
			}
			else {
				shininess = 0.0f;
			}

			// このマテリアルに紐づいているディフューズテクスチャ数分ループ
			std::vector<std::string> texpaths{};

			for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_DIFFUSE); t++)
			{
				aiString path{};

				// t番目のテクスチャパス取得
				if (AI_SUCCESS == material->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, t), path))
				{
					// テクスチャパス取得
					std::string texpath = std::string(path.C_Str());
					std::cout << texpath << std::endl;

					// テクスチャパスに「:」が含まれていれば絶対パスなのでパスを加工する
					if (texpath.find(':') != std::string::npos) {
						// スラッシュまたはバックスラッシュが最後に現れる位置を探す
						size_t pos = texpath.find_last_of("/\\");
						if (pos != std::string::npos)
						{
							// 最後のスラッシュまたはバックスラッシュの次から文字列を返す
							texpath = texpath.substr(pos + 1);
						}
					}
					texpaths.push_back(texpath);

					// 内蔵テクスチャかどうかを判断する
					if (auto tex = pScene->GetEmbeddedTexture(path.C_Str())) {

						std::unique_ptr<Texture> texture = std::make_unique<Texture>();

						// 内蔵テクスチャの場合
						bool sts = texture->LoadFromMemory(
							(unsigned char*)tex->pcData,			// 先頭アドレス
							tex->mWidth);			// テクスチャサイズ（メモリにある場合幅がサイズ）	
						if (sts) {
							g_textures[m] = std::move(texture);
						}
						std::cout << "Embedded" << std::endl;

					}
					else {
						// 外部テクスチャファイルの場合
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
				// ディフューズテクスチャがなかった場合
				else
				{
					// 外部テクスチャファイルの場合
					std::unique_ptr<Texture> texture;
					texture = std::make_unique<Texture>();
					g_textures[m] = std::move(texture);
				}
			}

			// マテリアル情報を保存
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

	void GetModelData(std::string filename, std::string texturedirectory)
	{
		//データを一度クリア
		g_vertices.clear();		// 頂点データ
		g_indices.clear();		// インデックスデータ
		g_subsets.clear();		// サブセット情報
		g_materials.clear();	// マテリアル
		g_textures.clear(); 	// ディフューズテクスチャ群

		// シーン情報構築
		Assimp::Importer importer;

		// シーン情報を構築
		const aiScene* pScene = importer.ReadFile(
			filename.c_str(),
			aiProcess_ConvertToLeftHanded |	// 左手座標系に変換する
			aiProcess_Triangulate);			// 三角形化する

		if (pScene == nullptr)
		{
			std::cout << "load error" << filename.c_str() << importer.GetErrorString() << std::endl;
		}
		// Debugでは読み込み失敗を即座に止めます。Releaseは有効なパスが渡る前提です。
		assert(pScene != nullptr);

		// マテリアル情報取得
		GetMaterialData(pScene, texturedirectory);

		// aiMeshは一つのMaterialIndexを持つため、メッシュ単位で頂点配列を作ります。
		g_vertices.resize(pScene->mNumMeshes);

		for (unsigned int m = 0; m < pScene->mNumMeshes; m++)
		{
			aiMesh* mesh = pScene->mMeshes[m];

			// メッシュ名取得
			std::string meshname = std::string(mesh->mName.C_Str());

			//　頂点数分ループ
			for (unsigned int vidx = 0; vidx < mesh->mNumVertices; vidx++)
			{
				// 頂点データ
				VERTEX	v{};
				v.meshname = meshname;		// メッシュ名セット

				// 座標		
				v.pos = mesh->mVertices[vidx];

				// この頂点が使用しているマテリアルのインデックス番号（メッシュ内の）
				// を使用してマテリアル名をセット
				v.materialindex = mesh->mMaterialIndex;

				v.mtrlname = g_materials[mesh->mMaterialIndex].mtrlname;

				// 法線あり？
				if (mesh->HasNormals()) {
					v.normal = mesh->mNormals[vidx];
				}
				else
				{
					v.normal = aiVector3D(0.0f, 0.0f, 0.0f);
				}

				// 頂点カラー？（０番目）
				if (mesh->HasVertexColors(0)) {
					v.color = mesh->mColors[0][vidx];
				}
				else
				{
					v.color = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
				}

				// テクスチャあり？（０番目）
				if (mesh->HasTextureCoords(0)) {
					v.texcoord = mesh->mTextureCoords[0][vidx];
				}
				else
				{
					v.texcoord = aiVector3D(0.0f, 0.0f, 0.0f);
				}

				// 頂点データを追加
				g_vertices[m].push_back(v);
			}
		}

		// Triangulate済みのFaceをメッシュ単位のIndex配列へ連結します。
		g_indices.resize(pScene->mNumMeshes);
		for (unsigned int m = 0; m < pScene->mNumMeshes; m++)
		{
			aiMesh* mesh = pScene->mMeshes[m];

			// メッシュ名取得
			std::string meshname = std::string(mesh->mName.C_Str());

			// インデックス数分ループ
			for (unsigned int fidx = 0; fidx < mesh->mNumFaces; fidx++)
			{
				aiFace face = mesh->mFaces[fidx];

				assert(face.mNumIndices == 3);	// 三角形のみ対応

				// インデックスデータを追加
				for (unsigned int i = 0; i < face.mNumIndices; i++)
				{
					g_indices[m].push_back(face.mIndices[i]);
				}
			}
		}

		// サブセット情報を生成
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

		// サブセット情報を相対的なものにする	
		for (int m = 0; m < g_subsets.size(); m++)
		{
			// 頂点バッファのベースを計算
			g_subsets[m].VertexBase = 0;
			for (int i = m - 1; i >= 0; i--) {
				g_subsets[m].VertexBase += g_subsets[i].VertexNum;
			}

			// インデックスバッファのベースを計算
			g_subsets[m].IndexBase = 0;
			for (int i = m - 1; i >= 0; i--) {
				g_subsets[m].IndexBase += g_subsets[i].IndexNum;
			}
		}
	}

	// サブセット情報
	std::vector<SUBSET> GetSubsets()
	{
		return g_subsets;
	}

	std::vector<std::vector<VERTEX>> GetVertices()
	{
		return g_vertices; // 頂点データ（メッシュ単位）
	}

	std::vector<std::vector<unsigned int>> GetIndices()
	{
		return g_indices; // インデックスデータ（メッシュ単位）
	}

	std::vector<MATERIAL> GetMaterials()
	{
		return g_materials; // マテリアル
	}
}
