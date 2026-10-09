// ============================================================================
// ファイルの役割: Direct3D 11のインデックスバッファをComPtrで持ち、描くときにGPUへ設定している。
// 主な技術: Direct3D 11 Buffer、RAII（ComPtrで自動的に解放）、インデックスを使った描画
// ============================================================================

#pragma once

#include	<vector>
#include	<wrl/client.h>
#include	"Renderer.h"
#include	"utility.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// IndexBufferクラス：32ビットのインデックスを入れたバッファ1つ分
//----------------------------------------------------------------------------- 
class IndexBuffer {

	ComPtr<ID3D11Buffer> m_IndexBuffer;

public:
	// インデックスの配列から、GPUのインデックスバッファを作っている
	void Create(const std::vector<unsigned int>& indices)
	{
		// デバイスを取得している
		ID3D11Device* device = nullptr;
		device = Renderer::GetDevice();
		assert(device); // デバイスがあることを確かめている

		// インデックスバッファを作っている
		const bool created = Renderer::CreateIndexBuffer(
			(unsigned int)(indices.size()),				// インデックスの数
			(void*)indices.data(),						// インデックスデータの先頭アドレス
			m_IndexBuffer.ReleaseAndGetAddressOf());							// 作ったバッファを受け取る場所

		// assertはReleaseで消えるため、作れなかったときははっきり知らせて終了している。
		if (!created)
		{
			utility::ReportFatalError("インデックスバッファを作成できませんでした。");
		}
	}

	// このインデックスバッファを、次の描画で使うよう設定している
	void SetGPU()
	{
		// デバイスコンテキストを取得している
		ID3D11DeviceContext* devicecontext = nullptr;
		devicecontext = Renderer::GetDeviceContext();

		// インデックスバッファを設定している（32ビットの符号なし整数）
		devicecontext->IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	}
};
