// ============================================================================
// ファイルの役割: HLSLの読み込み、頂点の入力レイアウト、GPUへのシェーダーの設定を管理している。
// 主な技術: D3DCompileFromFile（またはコンパイル済みの.cso）、頂点・ピクセルシェーダー、入力レイアウト、エラーの表示
// ============================================================================

#include	"Shader.h"
#include	"Renderer.h"

#include <unordered_map>
#include "Utility.h"

namespace
{
	// 作ったシェーダーを、ファイル名の組み合わせごとに覚えておく入れ物（同じシェーダーを何度もコンパイルしないため）
	struct ShaderCacheEntry
	{
		ComPtr<ID3D11VertexShader> VertexShader;
		ComPtr<ID3D11PixelShader> PixelShader;
		ComPtr<ID3D11InputLayout> VertexLayout;
	};

	std::unordered_map<std::string, ShaderCacheEntry> g_ShaderCache;
}

//=======================================
// シェーダーを作っている
//=======================================
bool Shader::Create(std::string vs, std::string ps, bool procedural)
{
	// 頂点シェーダーとピクセルシェーダーのファイル名を改行でつないだものをキーにしている
	const std::string cacheKey = vs + '\n' + ps + (procedural ? "\nprocedural" : "\nmesh");
	const auto cachedShader = g_ShaderCache.find(cacheKey);
	if (cachedShader != g_ShaderCache.end())
	{
		m_pVertexShader = cachedShader->second.VertexShader;
		m_pPixelShader = cachedShader->second.PixelShader;
		m_pVertexLayout = cachedShader->second.VertexLayout;
		return true;
	}

	// 頂点データの並び方（VERTEX_3Dと同じ：位置・法線・色・テクスチャ座標）
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,		0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,		0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,			0,	D3D11_APPEND_ALIGNED_ELEMENT,   D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	unsigned int numElements = procedural ? 0u : ARRAYSIZE(layout);

	// 頂点シェーダーと、頂点レイアウトを一緒に作っている
	HRESULT hr = Renderer::CreateVertexShader(
		m_pVertexShader.ReleaseAndGetAddressOf(),		// 頂点シェーダー
		m_pVertexLayout.ReleaseAndGetAddressOf(),			// 頂点レイアウト
		layout,
		numElements,
		vs.c_str()
		);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateVertexShader error", "error", MB_OK);
		return false;
	}

	// 深度のみのパスではピクセルシェーダーを作らない。
	m_pPixelShader.Reset();
	if (!ps.empty())
	{
		hr = Renderer::CreatePixelShader(m_pPixelShader.GetAddressOf(), ps.c_str());
		if (FAILED(hr))
		{
			MessageBox(nullptr, "CreatePixelShader error", "error", MB_OK);
			return false;
		}
	}

	// 作ったシェーダーを覚えておいている
	g_ShaderCache.emplace(
		cacheKey,
		ShaderCacheEntry{
			m_pVertexShader,
			m_pPixelShader,
			m_pVertexLayout });

	return true;
}

//=======================================
// シェーダーをGPUへ設定している
//=======================================
void Shader::CreateRequired(const char* vs, const char* ps, bool procedural)
{
	if (!Create(vs, ps, procedural))
	{
		Utility::ReportFatalError(std::string("描画シェーダーを作成できません: ") + vs + " / " + ps);
	}
}

void Shader::SetGPU()
{
	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();

	devicecontext->VSSetShader(m_pVertexShader.Get(), nullptr, 0);		// 頂点シェーダーを設定している
	devicecontext->PSSetShader(m_pPixelShader.Get(), nullptr, 0);		// ピクセルシェーダーを設定している
	devicecontext->IASetInputLayout(m_pVertexLayout.Get());				// 頂点レイアウトを設定している
	
}

// 覚えているシェーダーを全部手放している
void Shader::ClearCache()
{
	g_ShaderCache.clear();
}

