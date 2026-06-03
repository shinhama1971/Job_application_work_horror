#pragma once

#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include"Object.h"
#include"Material.h"
class Camera;

//-----------------------------------------------------------------------------
//TestPlaneクラス
//-----------------------------------------------------------------------------
class Ground :public Object 
{
	
	// 頂点データ
	std::vector<VERTEX_3D> m_Vertices;

	//インデックスデータ
	std::vector<unsigned int> m_Indices;

	// 描画の為の情報（メッシュに関わる情報）
	IndexBuffer	 m_IndexBuffer; // インデックスバッファ
	VertexBuffer<VERTEX_3D>	m_VertexBuffer; // 頂点バッファ
	//Texture m_Texture;
	std::unique_ptr<Material>m_Material;
	int m_SizeX = 0;//横サイズ
	int m_SizeZ = 0;//縦サイズ
	

public:
	

	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();
	void SetScale(float x, float y, float z) { m_Scale = DirectX::SimpleMath::Vector3(x, y, z); }
	void SetPosition(float x, float y, float z) { m_Position = DirectX::SimpleMath::Vector3(x, y, z); }
	void SetTexture(const char* filename);
	//頂点情報を取得
	std::vector<VERTEX_3D>GetVertices();
};