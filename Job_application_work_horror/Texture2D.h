// ============================================================================
// ファイルの役割: 画像を貼った四角形を2Dで描くクラス（アニメーションの分割の指定付き。今はどこからも使っていない）。
// 主な技術: Direct3D 11のテクスチャ、SRV、UVの行列による画像の切り出し
// ============================================================================

#pragma once
#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Texture.h"
#include "Material.h"

//-----------------------------------------------------------------------------
// Texture2Dクラス：画面に画像を貼った四角形を描き、画像を縦横に分割したコマを切り替えられる
//-----------------------------------------------------------------------------
class Texture2D : public Object
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

	// UV座標の情報（何コマ目を使うか、何分割か）
	float m_NumU = 1;
	float m_NumV = 1;
	float m_SplitX = 1;
	float m_SplitY = 1;
	int m_DivX = 1; // 横の分割数
	int m_DivY = 1; // 縦の分割数

public:
	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();

	// テクスチャを指定している
	void SetTexture(const char* imgname);

	// 位置を指定している
	void SetPosition(const float& x, const float& y, const float& z);
	void SetPosition(const DirectX::SimpleMath::Vector3& pos);

	// 角度を指定している（度で受け取り、ラジアンにしている）
	void SetRotation(const float& x, const float& y, const float& z);
	void SetRotation(const DirectX::SimpleMath::Vector3& rot);

	// 大きさを指定している
	void SetScale(const float& x, const float& y, const float& z);
	void SetScale(const DirectX::SimpleMath::Vector3& scl);
	// 画像の縦横の分割数を指定している
	void SetDivide(int divX, int divY);

	// 何コマ目を表示するかを指定している
	void SetFrame(int frame);
	// UV座標を直接指定している
	void SetUV(const float& nu, const float& nv, const float& sx, const float& sy);
};

