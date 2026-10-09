// ============================================================================
// ファイルの役割: シェーダー・GPUのバッファの作成と、バックバッファの操作を担当している。
// 主な技術: COM、定数バッファ、頂点・インデックスバッファ、シェーダーのコンパイル
// ============================================================================

#include "Renderer.h"
#include "Application.h"

#include <filesystem>

using namespace DirectX::SimpleMath;

// シェーダーを読み込んでいる。コンパイル済みの.csoがあればそれを使い、無ければ.hlslをコンパイルしている。
HRESULT Renderer::CompileShader(
	const char* szFileName,
	LPCSTR szEntryPoint,
	LPCSTR szShaderModel,
	std::vector<unsigned char>& shaderObject)
{
	shaderObject.clear();
	// 拡張子をcsoに置き換えたファイル名を作っている。
	// 固定長のバッファを使わないため、長いパスでもあふれない。
	std::filesystem::path csoPath(szFileName);
	csoPath.replace_extension(".cso");

	// csoファイルがあれば開いている
	FILE* fp;
	int ret = fopen_s(&fp, csoPath.string().c_str(), "rb");
	if (ret == 0)
	{
		// ファイルの大きさを取得している
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		if (size <= 0)
		{
			fclose(fp);
			return E_FAIL;
		}

		shaderObject.resize(static_cast<size_t>(size));
		const size_t readSize = fread(
			shaderObject.data(), 1, shaderObject.size(), fp);
		fclose(fp);

		if (readSize != shaderObject.size())
		{
			shaderObject.clear();
			return E_FAIL;
		}
	}
	// csoファイルが無ければ、hlslファイルをコンパイルしている
	else
	{
		HRESULT hr = S_OK;
		WCHAR	filename[512];
		size_t 	wLen = 0;
		int err = 0;

		// 文字コードを Shift-JIS → UTF-16 に変換している
		setlocale(LC_ALL, "japanese");  // ロケールを日本語にしている（Windowsの日本語のパスを正しく変換するため）
		err = mbstowcs_s(&wLen, filename, 512, szFileName, _TRUNCATE);

		// シェーダーのコンパイルの設定（古い書き方を許さない）
		DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
		dwShaderFlags |= D3DCOMPILE_DEBUG; // Debug構成では、シェーダーのデバッグ情報も含めている
#endif

		// コンパイルの結果と、エラーの情報を受け取る入れ物
		Microsoft::WRL::ComPtr<ID3DBlob> pErrorBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> pBlob;

		// HLSLファイルをコンパイルしている
		hr = D3DCompileFromFile(
			filename,							// ファイル名
			nullptr,							// マクロの定義はなし
			D3D_COMPILE_STANDARD_FILE_INCLUDE,	// #include を使えるようにしている
			szEntryPoint,						// 入口の関数の名前
			szShaderModel,						// シェーダーモデル
			dwShaderFlags,						// コンパイルの設定
			0,									// エフェクトの設定（使わない）
			pBlob.GetAddressOf(),				// 成功したときのコンパイル結果
			pErrorBlob.GetAddressOf());			// コンパイルエラーの出力

		// コンパイルに失敗したら、エラーの内容を表示している
		if (FAILED(hr))
		{
			if (pErrorBlob != nullptr) {
				MessageBoxA(NULL, static_cast<const char*>(pErrorBlob->GetBufferPointer()), "Error", MB_OK);
			}
			return E_FAIL;
		}

		// コンパイルに成功したら、バイナリを呼び出し元へ渡している
		shaderObject.resize(pBlob->GetBufferSize());
		memcpy(
			shaderObject.data(),
			pBlob->GetBufferPointer(),
			shaderObject.size());
	}

	return S_OK;
}

//--------------------------------------------------------------------------------------
// 頂点シェーダーと、入力レイアウト（頂点の並び方）を作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName)
{
	std::vector<unsigned char> shaderObject;

	// vs_5_0としてコンパイルしている（入口はmain関数）
	HRESULT hr = CompileShader(
		szFileName, "main", "vs_5_0", shaderObject);
	if (FAILED(hr)) return hr;

	// 頂点シェーダーを作っている
	hr = m_pDevice->CreateVertexShader(
		shaderObject.data(), shaderObject.size(), NULL, ppVertexShader);

	if (FAILED(hr))
	{
		return hr;
	}
	// 頂点の並び方（入力レイアウト）を、シェーダーの入力と照らし合わせて作っている
	hr = m_pDevice->CreateInputLayout(
		pLayout,
		numElements,
		shaderObject.data(),
		shaderObject.size(),
		ppVertexLayout);
	
	if (FAILED(hr))
	{
		return hr;
	}

	return S_OK;
}

//--------------------------------------------------------------------------------------
// ピクセルシェーダーを作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreatePixelShader(ID3D11PixelShader** ppPixelShader, const char* szFileName)
{
	std::vector<unsigned char> shaderObject;

	// ps_5_0としてコンパイルしている（入口はmain関数）
	HRESULT hr = CompileShader(
		szFileName, "main", "ps_5_0", shaderObject);
	if (FAILED(hr)) return hr;

	// ピクセルシェーダーを作っている
	hr = m_pDevice->CreatePixelShader(
		shaderObject.data(), shaderObject.size(), nullptr, ppPixelShader);
	if (FAILED(hr))
	{
		return hr;
	}
	return S_OK;
}

//--------------------------------------------------------------------------------------
// インデックスバッファを作っている（中身は後から書き換えない）
//--------------------------------------------------------------------------------------
bool Renderer::CreateIndexBuffer(
	unsigned int indexnum,						// インデックスの数
	void* indexdata,							// インデックスデータの先頭アドレス
	ID3D11Buffer** pIndexBuffer) {				// 作ったバッファを受け取る場所

	// インデックスバッファの設定
	D3D11_BUFFER_DESC bd;
	D3D11_SUBRESOURCE_DATA InitData;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;								// GPUだけで読み書きする
	bd.ByteWidth = sizeof(unsigned int) * indexnum;				// バッファの大きさ
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;						// インデックスバッファとして使う
	bd.CPUAccessFlags = 0;										// CPUからは書き換えない

	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = indexdata;

	HRESULT hr = m_pDevice->CreateBuffer(&bd, &InitData, pIndexBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(index buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------
// 頂点バッファを作っている（中身は後から書き換えない）
//--------------------------------------------------------------------------------------
bool Renderer::CreateVertexBuffer(
	unsigned int stride,				// 1頂点あたりのバイト数
	unsigned int vertexnum,				// 頂点の数
	void* vertexdata,					// 頂点データの先頭アドレス
	ID3D11Buffer** pVertexBuffer) {		// 作ったバッファを受け取る場所

	HRESULT hr;

	// 頂点バッファの設定
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;				// GPUだけで読み書きする
	bd.ByteWidth = stride * vertexnum;			// バッファの大きさ
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;	// 頂点バッファとして使う
	bd.CPUAccessFlags = 0;						// CPUからは書き換えない

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertexdata;				// バッファの最初の中身

	hr = m_pDevice->CreateBuffer(&bd, &InitData, pVertexBuffer);		// バッファを作っている
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(vertex buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------
// 頂点バッファを作っている（CPUからMapで書き換えられる）
//--------------------------------------------------------------------------------------
bool Renderer::CreateVertexBufferWrite(
	unsigned int stride,				// 1頂点あたりのバイト数
	unsigned int vertexnum,				// 頂点の数
	void* vertexdata,					// 頂点データの先頭アドレス
	ID3D11Buffer** pVertexBuffer) {		// 作ったバッファを受け取る場所

	HRESULT hr;

	// 頂点バッファの設定
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;							// CPUから毎フレーム書き換える
	bd.ByteWidth = stride * vertexnum;						// バッファの大きさ
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;				// 頂点バッファとして使う
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;				// CPUから書き込める

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertexdata;							// バッファの最初の中身

	hr = m_pDevice->CreateBuffer(&bd, &InitData, pVertexBuffer);		// バッファを作っている
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(vertex buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------
// 定数バッファを作っている（UpdateSubresourceで書き換える）
//--------------------------------------------------------------------------------------
bool Renderer::CreateConstantBuffer(
	unsigned int bytesize,					// 定数バッファの大きさ（16バイトの倍数）
	ID3D11Buffer** pConstantBuffer) {			// 作ったバッファを受け取る場所

	// 定数バッファの設定
	D3D11_BUFFER_DESC bd;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;								// GPUが読み、CPUからはUpdateSubresourceで書き換える
	bd.ByteWidth = bytesize;									// バッファの大きさ
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;					// 定数バッファとして使う
	bd.CPUAccessFlags = 0;										// CPUから直接は書き込まない

	HRESULT hr = m_pDevice->CreateBuffer(&bd, nullptr, pConstantBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(constant buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

// 描画先をバックバッファに戻し、ビューポートを描画解像度の全体にしている
void Renderer::SetBackBufferRenderTarget()
{
	m_pDeviceContext->OMSetRenderTargets(
		1,
		m_pRenderTargetView.GetAddressOf(),
		m_pDepthStencilView.Get()
	);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(Application::GetWidth());
    viewport.Height = static_cast<float>(Application::GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);
}

// バックバッファを指定の色で消している
void Renderer::ClearBackBuffer(float r, float g, float b, float a)
{
	float clearColor[4] = { r, g, b, a };

	m_pDeviceContext->ClearRenderTargetView(
		m_pRenderTargetView.Get(),
		clearColor
	);
}

// 深度バッファを一番奥（1.0）で消している
void Renderer::ClearDepth()
{
	m_pDeviceContext->ClearDepthStencilView(
		m_pDepthStencilView.Get(),
		D3D11_CLEAR_DEPTH,
		1.0f,
		0
	);
}

//--------------------------------------------------------------------------------------
// 定数バッファを作っている（CPUからMapで書き換えられる）
//--------------------------------------------------------------------------------------
bool Renderer::CreateConstantBufferWrite(
	unsigned int bytesize,					// 定数バッファの大きさ（16バイトの倍数）
	ID3D11Buffer** pConstantBuffer) {			// 作ったバッファを受け取る場所

	// 定数バッファの設定
	D3D11_BUFFER_DESC bd;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;							// CPUから毎フレーム書き換える
	bd.ByteWidth = bytesize;									// バッファの大きさ
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;					// 定数バッファとして使う
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;					// CPUから書き込める

	HRESULT hr = m_pDevice->CreateBuffer(&bd, nullptr, pConstantBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(constant buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}
