// ============================================================================
// ファイルの役割: 画像を貼った四角形（2Dスプライト）を描くためのクラス（今はどこからも使っていない）。
// 主な技術: 頂点・インデックスバッファ、テクスチャ、2D用の行列
// ============================================================================

#pragma once
#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Texture.h"
#include "Material.h"


// 画像を貼った四角形を描くクラス。HUDはHudクラスで描いているため、今は使っていない。
class SpriteRenderer: public Object
{
private:
	// 頂点データ
	std::vector<VERTEX_3D> m_Vertices;

	// インデックスデータ
	std::vector<unsigned int> m_Indices;


	// 描画するための情報（メッシュに関わる情報）
	IndexBuffer m_IndexBuffer; // インデックスバッファ
	VertexBuffer<VERTEX_3D> m_VertexBuffer; // 頂点バッファ
	// 描画するための情報（見た目に関わる部分）
	Texture m_Texture; // テクスチャ
	std::unique_ptr<Material> m_Material; // マテリアル

	// UV座標の情報（画像を何分割して、どこを使うか。今は描画に反映していない）
	float m_NumU = 1;
	float m_NumV = 1;
	float m_SplitX = 1;
	float m_SplitY = 1;
public:
	void Init();
	void Update();
	void Draw(Camera*cam);
	void Uninit();

	// テクスチャを指定している
	void SetTexture(const char* imgname);

	// 位置を指定する（宣言だけで、定義はない）
	void SetPosition(const float& x, const float& y, const float& z);
	void SetPosition(const DirectX::SimpleMath::Vector3& pos);

	// 角度を指定する（宣言だけで、定義はない）
	void SetRotation(const float& x, const float& y, const float& z);
	void SetRotation(const DirectX::SimpleMath::Vector3& rot);

	// 大きさを指定する（宣言だけで、定義はない）
	void SetScale(const float& x, const float& y, const float& z);
	void SetScale(const DirectX::SimpleMath::Vector3& scl);

	// UV座標を指定している
	void SetUV(const float& nu, const float& nv, const float& sx, const float& sy);
};

