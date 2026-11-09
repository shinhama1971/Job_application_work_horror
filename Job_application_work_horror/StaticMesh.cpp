// ============================================================================
// ファイルの役割: OBJ・FBXなどの動かないモデルを読み込み、描画用のデータ（頂点・インデックス・マテリアル・テクスチャ）として持っている。
// 主な技術: Assimp、GPUのバッファへ渡す形への変換、マテリアル、インデックスを使った描画、
//           変換した結果をバイナリで保存して次から読む仕組み（Debug版のAssimpは遅いため、起動を速くしている）
// ============================================================================

#include	"StaticMesh.h"
#include	"AssimpPerse.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
	// 変換済みの形式の先頭に書く印と版。中身の並びを変えたら版を上げ、古い保存を読まないようにしている
	constexpr char ConvertedMagic[4] = { 'S', 'L', 'M', 'C' };
	constexpr std::uint32_t ConvertedVersion = 1;
	// 保存先（ゲームのフォルダからの相対パス。.gitignore で管理の対象から外している）
	const std::filesystem::path ConvertedDirectory = "cache/model";

	// 元のモデルの大きさと更新日時。これが保存したときと同じなら、変換済みの形式を使っている
	struct SourceStamp
	{
		std::uint64_t Size = 0;
		std::int64_t WriteTime = 0;
	};

	bool GetSourceStamp(const std::string& filename, SourceStamp& stamp)
	{
		std::error_code error;
		const std::uintmax_t size = std::filesystem::file_size(filename, error);
		if (error)
		{
			return false;
		}
		const auto writeTime = std::filesystem::last_write_time(filename, error);
		if (error)
		{
			return false;
		}
		stamp.Size = static_cast<std::uint64_t>(size);
		stamp.WriteTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
		return true;
	}

	// モデルのパスから保存先のファイル名を作っている（例: assets/model/props/forest_rock.fbx → assets_model_props_forest_rock.fbx.mesh）
	std::filesystem::path GetConvertedPath(const std::string& filename)
	{
		std::string name = std::filesystem::path(filename).lexically_normal().generic_string();
		std::replace_if(name.begin(), name.end(),
			[](char character) { return character == '/' || character == ':'; }, '_');
		return ConvertedDirectory / (name + ".mesh");
	}

	// 値・配列・文字列を、そのままのバイト列で読み書きしている（同じPC・同じビルドで読み戻すだけなので、並びの変換はしない）
	template <typename T>
	void WriteValue(std::ofstream& stream, const T& value)
	{
		stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
	}

	template <typename T>
	bool ReadValue(std::ifstream& stream, T& value)
	{
		return static_cast<bool>(stream.read(reinterpret_cast<char*>(&value), sizeof(T)));
	}

	template <typename T>
	void WriteArray(std::ofstream& stream, const std::vector<T>& values)
	{
		WriteValue(stream, static_cast<std::uint32_t>(values.size()));
		if (!values.empty())
		{
			stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
		}
	}

	template <typename T>
	bool ReadArray(std::ifstream& stream, std::vector<T>& values)
	{
		std::uint32_t count = 0;
		if (!ReadValue(stream, count))
		{
			return false;
		}
		values.resize(count);
		return count == 0 ||
			static_cast<bool>(stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T))));
	}

	void WriteString(std::ofstream& stream, const std::string& text)
	{
		WriteValue(stream, static_cast<std::uint32_t>(text.size()));
		stream.write(text.data(), static_cast<std::streamsize>(text.size()));
	}

	bool ReadString(std::ifstream& stream, std::string& text)
	{
		std::uint32_t length = 0;
		if (!ReadValue(stream, length))
		{
			return false;
		}
		text.resize(length);
		return length == 0 || static_cast<bool>(stream.read(text.data(), length));
	}
}

// 変換済みの形式があればそれを読み、無ければAssimpで読んで変換し、その結果を保存している
void StaticMesh::Load(std::string filename, std::string texturedirectory)
{
	if (LoadConverted(filename, texturedirectory))
	{
		std::cout << "[StaticMesh] Converted: " << filename << std::endl;
		return;
	}
	if (LoadWithAssimp(filename, texturedirectory))
	{
		SaveConverted(filename);
	}
}

// Assimpで読み込んだ結果を、ゲームで使う形（VERTEX_3D・SUBSET・MATERIAL）に変換している
bool StaticMesh::LoadWithAssimp(const std::string& filename, const std::string& texturedirectory)
{
	std::vector<AssimpPerse::SUBSET> subsets{};					// サブセットの情報
	std::vector<std::vector<AssimpPerse::VERTEX>> vertices{};	// 頂点データ（メッシュ単位）
	std::vector<std::vector<unsigned int>> indices{};			// インデックスデータ（メッシュ単位）
	std::vector<AssimpPerse::MATERIAL> materials{};				// マテリアル

	// Assimpを使ってモデルのデータを読み込んでいる
	AssimpPerse::GetModelData(filename, texturedirectory);

	subsets = AssimpPerse::GetSubsets();		// サブセットの情報を取得している
	vertices = AssimpPerse::GetVertices();		// 頂点データ（メッシュ単位）
	indices = AssimpPerse::GetIndices();		// インデックスデータ（メッシュ単位）
	materials = AssimpPerse::GetMaterials();	// マテリアルの情報を取得している

	m_textures = AssimpPerse::GetTextures();	// テクスチャを受け取っている（所有権もここへ移る）

	// 頂点データを作っている
	for (const auto& mv : vertices)
	{
		for (auto& v : mv)
		{
			VERTEX_3D vertex{};
			vertex.position = DirectX::SimpleMath::Vector3(v.pos.x, v.pos.y, v.pos.z);
			vertex.normal = DirectX::SimpleMath::Vector3(v.normal.x, v.normal.y, v.normal.z);
			vertex.uv = DirectX::SimpleMath::Vector2(v.texcoord.x, v.texcoord.y);
			vertex.color = DirectX::SimpleMath::Color(v.color.r, v.color.g, v.color.b, v.color.a);
			m_vertices.emplace_back(vertex);
		}
	}
	UpdateModelBounds();

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

	// 変換済みの形式は、テクスチャをファイル名から読み直すので、名前の無いテクスチャ（埋め込み・パスを取れなかった物）があれば保存しない
	if (AssimpPerse::HasEmbeddedTextures())
	{
		return false;
	}
	for (std::size_t index = 0; index < m_textures.size() && index < m_texturenames.size(); ++index)
	{
		if (m_textures[index] != nullptr && m_texturenames[index].empty())
		{
			return false;
		}
	}
	return true;
}

// 全頂点を囲む箱（最小と最大）から、中心・半分の大きさ・境界球の半径を求めている
void StaticMesh::UpdateModelBounds()
{
	m_modelBounds = {};
	if (m_vertices.empty())
	{
		return;
	}
	DirectX::SimpleMath::Vector3 boundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
	DirectX::SimpleMath::Vector3 boundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const VERTEX_3D& vertex : m_vertices)
	{
		boundsMin = DirectX::SimpleMath::Vector3::Min(boundsMin, vertex.position);
		boundsMax = DirectX::SimpleMath::Vector3::Max(boundsMax, vertex.position);
	}
	m_modelBounds.Center = (boundsMin + boundsMax) * 0.5f;
	m_modelBounds.Extents = (boundsMax - boundsMin) * 0.5f;
	m_modelBounds.SphereRadius = m_modelBounds.Extents.Length();
	m_modelBounds.IsValid = true;
}

// 変換済みの形式を読んでいる。並び：印・版・構造体の大きさ・元のモデルの大きさと更新日時・頂点・インデックス・サブセット・マテリアル・テクスチャの名前
bool StaticMesh::LoadConverted(const std::string& filename, const std::string& texturedirectory)
{
	SourceStamp source;
	if (!GetSourceStamp(filename, source))
	{
		return false;
	}
	std::ifstream stream(GetConvertedPath(filename), std::ios::binary);
	if (!stream)
	{
		return false;
	}

	char magic[4] = {};
	std::uint32_t version = 0;
	std::uint32_t vertexSize = 0;
	std::uint32_t materialSize = 0;
	SourceStamp saved;
	if (!ReadValue(stream, magic) || !std::equal(magic, magic + 4, ConvertedMagic) ||
		!ReadValue(stream, version) || version != ConvertedVersion ||
		!ReadValue(stream, vertexSize) || vertexSize != sizeof(VERTEX_3D) ||
		!ReadValue(stream, materialSize) || materialSize != sizeof(MATERIAL) ||
		!ReadValue(stream, saved.Size) || saved.Size != source.Size ||
		!ReadValue(stream, saved.WriteTime) || saved.WriteTime != source.WriteTime)
	{
		return false;
	}

	std::vector<VERTEX_3D> vertices;
	std::vector<unsigned int> indices;
	std::vector<SUBSET> subsets;
	std::vector<MATERIAL> materials;
	std::vector<std::string> texturenames;
	std::uint32_t subsetCount = 0;
	if (!ReadArray(stream, vertices) || !ReadArray(stream, indices) || !ReadValue(stream, subsetCount))
	{
		return false;
	}
	subsets.resize(subsetCount);
	for (SUBSET& subset : subsets)
	{
		if (!ReadString(stream, subset.MtrlName) || !ReadValue(stream, subset.IndexNum) || !ReadValue(stream, subset.VertexNum) ||
			!ReadValue(stream, subset.IndexBase) || !ReadValue(stream, subset.VertexBase) || !ReadValue(stream, subset.MaterialIdx))
		{
			return false;
		}
	}
	if (!ReadArray(stream, materials))
	{
		return false;
	}
	texturenames.resize(materials.size());
	for (std::string& texturename : texturenames)
	{
		if (!ReadString(stream, texturename))
		{
			return false;
		}
	}

	// 全部読めてから入れ替えている（途中で失敗したら、Assimpで読み直す）
	m_vertices = std::move(vertices);
	m_indices = std::move(indices);
	m_subsets = std::move(subsets);
	m_materials = std::move(materials);
	m_texturenames = std::move(texturenames);
	UpdateModelBounds();

	// テクスチャは、Assimpで読んだときと同じく、テクスチャのフォルダから読んでいる（読めなければ空のまま）
	m_textures.clear();
	m_textures.resize(m_materials.size());
	for (std::size_t index = 0; index < m_texturenames.size(); ++index)
	{
		if (m_texturenames[index].empty())
		{
			continue;
		}
		auto texture = std::make_unique<Texture>();
		if (texture->Load(texturedirectory + "/" + m_texturenames[index]))
		{
			m_textures[index] = std::move(texture);
		}
	}
	return true;
}

// 変換した結果を保存している。書きかけのファイルを読まないよう、別の名前に書き終えてから置き換えている
void StaticMesh::SaveConverted(const std::string& filename) const
{
	SourceStamp source;
	if (!GetSourceStamp(filename, source))
	{
		return;
	}
	const std::filesystem::path path = GetConvertedPath(filename);
	std::filesystem::path temporaryPath = path;
	temporaryPath += ".tmp";
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	{
		std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			return;
		}
		stream.write(ConvertedMagic, sizeof(ConvertedMagic));
		WriteValue(stream, ConvertedVersion);
		WriteValue(stream, static_cast<std::uint32_t>(sizeof(VERTEX_3D)));
		WriteValue(stream, static_cast<std::uint32_t>(sizeof(MATERIAL)));
		WriteValue(stream, source.Size);
		WriteValue(stream, source.WriteTime);
		WriteArray(stream, m_vertices);
		WriteArray(stream, m_indices);
		WriteValue(stream, static_cast<std::uint32_t>(m_subsets.size()));
		for (const SUBSET& subset : m_subsets)
		{
			WriteString(stream, subset.MtrlName);
			WriteValue(stream, subset.IndexNum);
			WriteValue(stream, subset.VertexNum);
			WriteValue(stream, subset.IndexBase);
			WriteValue(stream, subset.VertexBase);
			WriteValue(stream, subset.MaterialIdx);
		}
		WriteArray(stream, m_materials);
		for (std::size_t index = 0; index < m_materials.size(); ++index)
		{
			WriteString(stream, index < m_texturenames.size() ? m_texturenames[index] : std::string());
		}
		if (!stream)
		{
			return;
		}
	}
	std::filesystem::rename(temporaryPath, path, error);
	if (error)
	{
		std::filesystem::remove(temporaryPath, error);
	}
}
