// ============================================================================
// ファイルの役割: Direct3D 11のインデックスバッファをRAIIで保持し、描画時に設定します。
// 主な技術: Direct3D 11 Buffer、テンプレート、RAII、インデックス描画
// ============================================================================

#pragma once

#include	<vector>
#include	<wrl/client.h>
#include	"Renderer.h"
#include	"utility.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//IndexBufferクラス
//----------------------------------------------------------------------------- 
class IndexBuffer {

	ComPtr<ID3D11Buffer> m_IndexBuffer;

public:
	void Create(const std::vector<unsigned int>& indices)
	{
		// デバイス取得
		ID3D11Device* device = nullptr;
		device = Renderer::GetDevice();
		assert(device); //deviceが存在することを確認

		// インデックスバッファ作成
		const bool created = Renderer::CreateIndexBuffer(
			(unsigned int)(indices.size()),				// インデックス数
			(void*)indices.data(),						// インデックスデータ先頭アドレス
			m_IndexBuffer.ReleaseAndGetAddressOf());							// インデックスバッファ

		// assertはReleaseで消えるため、作成失敗は明示的に通知して終了します。
		if (!created)
		{
			utility::ReportFatalError("インデックスバッファを作成できませんでした。");
		}
	}

	void SetGPU()
	{
		// デバイスコンテキスト取得
		ID3D11DeviceContext* devicecontext = nullptr;
		devicecontext = Renderer::GetDeviceContext();

		// インデックスバッファをセット
		devicecontext->IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	}
};
