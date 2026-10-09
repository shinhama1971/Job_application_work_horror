// ============================================================================
// ファイルの役割: OBJ・FBXなどの動かないモデルを読み込み、描画用のデータ（頂点・インデックス・マテリアル・テクスチャ）として持っている。
// 主な技術: Assimp、GPUのバッファへ渡す形への変換、マテリアル、インデックスを使った描画
// ============================================================================

#include	"StaticMesh.h"
#include	"AssimpPerse.h"

#include <algorithm>
#include <cfloat>

// Assimpで読み込んだ結果を、ゲームで使う形（VERTEX_3D・SUBSET・MATERIAL）に変換している
void StaticMesh::Load(std::string filename, std::string texturedirectory)
{
	std::vector<AssimpPerse::SUBSET> subsets{};					// サブセットの情報
	std::vector<std::vector<AssimpPerse::VERTEX>> vertices{};	// 頂点データ（メッシュ単位）
	std::vector<std::vector<unsigned int>> indices{};			// インデックスデータ（メッシュ単位）
	std::vector<AssimpPerse::MATERIAL> materials{};				// マテリアル
	std::vector<std::unique_ptr<Texture>> embededtextures{};	// 内蔵テクスチャ（今は使っていない）

	// Assimpを使ってモデルのデータを読み込んでいる
	AssimpPerse::GetModelData(filename, texturedirectory);

	subsets = AssimpPerse::GetSubsets();		// サブセットの情報を取得している
	vertices = AssimpPerse::GetVertices();		// 頂点データ（メッシュ単位）
	indices = AssimpPerse::GetIndices();		// インデックスデータ（メッシュ単位）
	materials = AssimpPerse::GetMaterials();	// マテリアルの情報を取得している

	m_textures = AssimpPerse::GetTextures();	// テクスチャを受け取っている（所有権もここへ移る）
	m_modelBounds = {};
	DirectX::SimpleMath::Vector3 boundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
	DirectX::SimpleMath::Vector3 boundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	// 頂点データを作り、ついでに全頂点を囲む箱（最小と最大）を求めている
	for (const auto& mv : vertices)
	{
		for (auto& v : mv)
		{
			VERTEX_3D vertex{};
			vertex.position = DirectX::SimpleMath::Vector3(v.pos.x, v.pos.y, v.pos.z);
			vertex.normal = DirectX::SimpleMath::Vector3(v.normal.x, v.normal.y, v.normal.z);
			vertex.uv = DirectX::SimpleMath::Vector2(v.texcoord.x, v.texcoord.y);
			vertex.color = DirectX::SimpleMath::Color(v.color.r, v.color.g, v.color.b, v.color.a);

			boundsMin.x = (std::min)(boundsMin.x, vertex.position.x);
			boundsMin.y = (std::min)(boundsMin.y, vertex.position.y);
			boundsMin.z = (std::min)(boundsMin.z, vertex.position.z);
			boundsMax.x = (std::max)(boundsMax.x, vertex.position.x);
			boundsMax.y = (std::max)(boundsMax.y, vertex.position.y);
			boundsMax.z = (std::max)(boundsMax.z, vertex.position.z);

			m_vertices.emplace_back(vertex);
		}
	}

	// 全頂点を囲む箱から、中心・半分の大きさ・境界球の半径を求めている
	if (!m_vertices.empty())
	{
		m_modelBounds.Center = (boundsMin + boundsMax) * 0.5f;
		m_modelBounds.Extents = (boundsMax - boundsMin) * 0.5f;
		m_modelBounds.SphereRadius = m_modelBounds.Extents.Length();
		m_modelBounds.IsValid = true;
	}

	// インデックスデータを1つの配列につなげている（サブセットの開始位置で区別している）
	for (const auto& mi : indices)
	{
		for (auto& index : mi)
		{
			m_indices.emplace_back(index);
		}
	}

	// サブセットのデータを作っている
	for (const auto& sub : subsets)
	{
		SUBSET subset{};
		subset.VertexBase = sub.VertexBase; // 頂点の開始位置
		subset.VertexNum = sub.VertexNum; // サブセットの中の頂点の数
		subset.IndexBase = sub.IndexBase;  // インデックスの開始位置
		subset.IndexNum = sub.IndexNum; // サブセットの中のインデックスの数
		subset.MtrlName = sub.mtrlname; // マテリアルの名前
		subset.MaterialIdx = sub.materialindex; // マテリアルの配列の番号
		m_subsets.emplace_back(subset);
	}

	// マテリアルのデータを作っている（テクスチャの名前があれば、テクスチャを使う設定にしている）
	for (const auto& m : materials)
	{
		MATERIAL material{};
		material.Ambient = DirectX::SimpleMath::Color(
			m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a);
		material.Diffuse = DirectX::SimpleMath::Color(
			m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a);

		material.Specular = DirectX::SimpleMath::Color(
			m.Specular.r, m.Specular.g, m.Specular.b, m.Specular.a);
		material.Emission = DirectX::SimpleMath::Color(
			m.Emission.r, m.Emission.g, m.Emission.b, m.Emission.a);
		material.Shininess = m.Shininess;

		if (m.texturename.empty())
		{
			material.TextureEnable = FALSE;
			m_texturenames.emplace_back("");
		}
		else
		{
			material.TextureEnable = TRUE;
			m_texturenames.emplace_back(m.texturename);
		}

		m_materials.emplace_back(material);
	}
}
