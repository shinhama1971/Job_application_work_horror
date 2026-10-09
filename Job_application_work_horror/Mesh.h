// ============================================================================
// ファイルの役割: メッシュの頂点とインデックスの配列を持つ基本のクラス。
// 主な技術: インデックスを使った描画のためのデータ、継承して形を作るクラスの土台
// ============================================================================

#pragma once
#include	<vector>
#include	"renderer.h"

// 頂点とインデックスの配列を持ち、MeshRendererへ渡している（StaticMeshがモデルから中身を作っている）
class Mesh {
protected:
	std::vector<VERTEX_3D>		m_vertices;		// 頂点の配列
	std::vector<unsigned int>	m_indices;		// インデックスの配列
public:
	// 頂点データを返している
	const std::vector<VERTEX_3D>& GetVertices() {
		return m_vertices;
	}

	// インデックスデータを返している
	const std::vector<unsigned int>& GetIndices() {
		return m_indices;
	}
};

