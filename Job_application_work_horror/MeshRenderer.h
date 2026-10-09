// ============================================================================
// ファイルの役割: メッシュの頂点・インデックスバッファを持ち、全体または一部（サブセット）を描いている。
// 主な技術: Direct3D 11の描画パイプライン、頂点・インデックスバッファ、DrawIndexed
// ============================================================================

#pragma once
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Mesh.h"

// Meshの内容をGPUのバッファに入れて描くクラス。モデルはマテリアルごとにサブセットに分けて描いている。
class MeshRenderer {
protected:
	VertexBuffer<VERTEX_3D>	m_VertexBuffer;		// 頂点バッファ
	IndexBuffer				m_IndexBuffer;		// インデックスバッファ
	int						m_IndexNum = 0;		// インデックスの数
public:
	// Meshの頂点とインデックスから、GPUのバッファを作っている
	virtual void Init(Mesh& mesh) 
	{
		m_VertexBuffer.Create(mesh.GetVertices());
		m_IndexBuffer.Create(mesh.GetIndices());
		m_IndexNum = static_cast<int>(mesh.GetIndices().size());
	}

	// 描く前の準備：三角形のリストとして描く設定にし、頂点・インデックスバッファを設定している
	virtual void BeforeDraw()
	{
		ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();

		// 三角形のリストとして描く
		devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		m_VertexBuffer.SetGPU();			// 頂点バッファを設定している
		m_IndexBuffer.SetGPU();				// インデックスバッファを設定している
	}

	// サブセット（モデルの一部）を描いている
	virtual void DrawSubset(unsigned int indexnum,unsigned int baseindex,unsigned int basevertexindex ) 
	{
		Renderer::GetDeviceContext()->DrawIndexed(
			indexnum,								// 描くインデックスの数（面の数×3）
			baseindex,								// インデックスバッファの中の開始位置
			basevertexindex);						// 頂点バッファの中の開始位置
	}

	// メッシュ全体を描いている
	virtual void Draw() 
	{
		BeforeDraw();								// 描く前の準備

		Renderer::GetDeviceContext()->DrawIndexed(
			m_IndexNum,								// 描くインデックスの数（面の数×3）
			0,										// インデックスバッファの最初から
			0);										// 頂点バッファの最初から
	}
};
