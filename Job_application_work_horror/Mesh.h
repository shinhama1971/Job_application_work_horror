// ============================================================================
// ファイルの役割: 汎用メッシュの頂点・インデックス・材質情報を保持します。
// ============================================================================

#pragma once
#include	<vector>
#include	"renderer.h"

class Mesh {
protected:
	std::vector<VERTEX_3D>		m_vertices;		// 頂点座標群
	std::vector<unsigned int>	m_indices;		// インデックスデータ群
public:
	// 頂点データ取得
	const std::vector<VERTEX_3D>& GetVertices() {
		return m_vertices;
	}

	// インデックスデータ取得
	const std::vector<unsigned int>& GetIndices() {
		return m_indices;
	}
};

