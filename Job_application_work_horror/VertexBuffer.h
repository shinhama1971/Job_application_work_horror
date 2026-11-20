// ============================================================================
// ファイルの役割: 好きな頂点の型を使えるDirect3D 11の頂点バッファを、ComPtrで持っている。
// 主な技術: Direct3D 11 Buffer、テンプレート、RAII（ComPtrで自動的に解放）、頂点の大きさ（ストライド）
// ============================================================================

#pragma once
#include	<vector>
#include	<cstddef>
#include	<limits>
#include	<wrl/client.h>
#include	"Renderer.h"
#include	"Utility.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// VertexBufferクラス：頂点の型Tの配列を入れるバッファ1つ分（CPUから書き換えられる）
//-----------------------------------------------------------------------------
template <typename T> class VertexBuffer{

	ComPtr<ID3D11Buffer> m_VertexBuffer;
	std::size_t m_VertexCapacity = 0;

public:
	// 頂点の配列から、GPUの頂点バッファを作っている（後からModifyで書き換えられるようDYNAMICにしている）
	void Create(const std::vector<T>& vertices)
	{
		// デバイスを取得している
		ID3D11Device* device = nullptr;
		device = Renderer::GetDevice();
		assert(device); // デバイスがあることを確かめている

		// ByteWidthはUINT。件数を変換する前にバイト数が表現できるか確認する。
		if (vertices.empty() || vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(T))
		{
			Utility::ReportFatalError("頂点バッファの容量が不正です。");
		}

		// 頂点バッファを作っている
		const bool created = Renderer::CreateVertexBufferWrite(
			sizeof(T),						// 1頂点あたりのバイト数
			(unsigned int)vertices.size(),	// 頂点の数
			(void*)vertices.data(),			// 頂点データの先頭アドレス
			m_VertexBuffer.ReleaseAndGetAddressOf());				// 作ったバッファを受け取る場所

		// assertはReleaseで消えるため、作れなかったときははっきり知らせて終了している。
		if (!created)
		{
			Utility::ReportFatalError("頂点バッファを作成できませんでした。");
		}
		m_VertexCapacity = vertices.size();
	}

	// この頂点バッファを、次の描画で使うよう設定している
	void SetGPU()
	{
		// デバイスコンテキストを取得している
		ID3D11DeviceContext* devicecontext = nullptr;
		devicecontext = Renderer::GetDeviceContext();

		// 頂点バッファを設定している（1頂点の大きさはsizeof(T)）
		unsigned int stride = sizeof(T);
		unsigned  offset = 0;
		devicecontext->IASetVertexBuffers(0, 1, m_VertexBuffer.GetAddressOf(), &stride, &offset);
	}

	// 頂点バッファの中身を書き換えている（前の中身は捨てる）
	[[nodiscard]] bool Modify(const std::vector<T>& vertices)
	{
		if (!m_VertexBuffer || vertices.empty() || vertices.size() > m_VertexCapacity)
		{
			return false;
		}
		auto* context = Renderer::GetDeviceContext();
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(m_VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			return false;
		}
		memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(T));
		context->Unmap(m_VertexBuffer.Get(), 0);
		return true;
	}
};
