// ============================================================================
// ファイルの役割: シェーダー、GPUバッファ、バックバッファ操作を管理します。
// ============================================================================

#include "Renderer.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

// シェーダーをファイル拡張子に合わせてコンパイルします。
HRESULT Renderer::CompileShader(
	const char* szFileName,
	LPCSTR szEntryPoint,
	LPCSTR szShaderModel,
	std::vector<unsigned char>& shaderObject)
{
	shaderObject.clear();
	//拡張子csoのファイル名を作成
	char csoFileName[256];
	const char* dot = strrchr(szFileName, '.');  // 最後の '.' を探す
	if (dot) {
		int basenameLen =(int)( dot - szFileName);
		strncpy(csoFileName, szFileName, basenameLen); // 拡張子がある場合は拡張子を除いたファイル名をコピー
		csoFileName[basenameLen] = '\0';   // 終端文字を追加
	}
	else {
		strcpy(csoFileName, szFileName);   // 拡張子がない場合はそのままコピー
	}
	strcat(csoFileName, ".cso");// ".cso" 拡張子を付加

	//csoファイルがあれば開く
	FILE* fp;
	int ret = fopen_s(&fp, csoFileName, "rb");
	if (ret == 0)
	{
		// ファイルサイズを取得
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
	//csoファイルがなければhlslファイルをコンパイルする
	else
	{
		HRESULT hr = S_OK;
		WCHAR	filename[512];
		size_t 	wLen = 0;
		int err = 0;

		// 文字コードを Shift-JIS → UTF-16 に変換
		setlocale(LC_ALL, "japanese");  // ロケールを設定（Windows特有）
		err = mbstowcs_s(&wLen, filename, 512, szFileName, _TRUNCATE);

		// シェーダーコンパイルオプションを設定
		DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
		dwShaderFlags |= D3DCOMPILE_DEBUG; // デバッグビルドの場合はデバッグ情報も含める
#endif

		// コンパイル結果およびエラー情報格納用のBlob
		ID3DBlob* pErrorBlob = nullptr;
		ID3DBlob* pBlob = nullptr;

		// HLSLファイルをコンパイル
		hr = D3DCompileFromFile(
			filename,							// ファイル名
			nullptr,							// マクロ定義なし 
			D3D_COMPILE_STANDARD_FILE_INCLUDE,	// #include 対応 
			szEntryPoint,						// エントリーポイント名
			szShaderModel,						// シェーダーモデル
			dwShaderFlags,						// コンパイルフラグ
			0,									// エフェクトフラグ
			&pBlob,								// 成功時のコンパイル結果
			&pErrorBlob);						// コンパイルエラー出力

		// コンパイル失敗時のエラーメッセージを表示
		if (FAILED(hr))
		{
			if (pErrorBlob != nullptr) {
				MessageBoxA(NULL, (char*)pErrorBlob->GetBufferPointer(), "Error", MB_OK);
			}
			
			SAFE_RELEASE(pErrorBlob);
			SAFE_RELEASE(pBlob);
			return E_FAIL;
		}

		// エラーブロブがあれば解放
		if (pErrorBlob) pErrorBlob->Release();

		// コンパイル成功時のバイナリデータコピーして呼び出し元に渡す
		
		shaderObject.resize(pBlob->GetBufferSize());
		memcpy(
			shaderObject.data(),
			pBlob->GetBufferPointer(),
			shaderObject.size());
		SAFE_RELEASE(pBlob);
	}

	return S_OK;
}

//--------------------------------------------------------------------------------------
// 頂点シェーダーオブジェクトを生成する
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName)
{
	std::vector<unsigned char> shaderObject;

	// ファイルの拡張子に合わせてコンパイル
	HRESULT hr = CompileShader(
		szFileName, "main", "vs_5_0", shaderObject);
	if (FAILED(hr)) return hr;

	// デバイスを使って頂点シェーダーを作成
	hr = m_pDevice->CreateVertexShader(
		shaderObject.data(), shaderObject.size(), NULL, ppVertexShader);

	if (FAILED(hr))
	{
		return hr;
	}
	// デバイスを使って頂点レイアウトを作成
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
// ピクセルシェーダーオブジェクトを生成する
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreatePixelShader(ID3D11PixelShader** ppPixelShader, const char* szFileName)
{
	std::vector<unsigned char> shaderObject;

	// ファイルの拡張子に合わせてコンパイル
	HRESULT hr = CompileShader(
		szFileName, "main", "ps_5_0", shaderObject);
	if (FAILED(hr)) return hr;

	// ピクセルシェーダーを生成
	hr = m_pDevice->CreatePixelShader(
		shaderObject.data(), shaderObject.size(), nullptr, ppPixelShader);
	if (FAILED(hr))
	{
		return hr;
	}
	return S_OK;
}

//--------------------------------------------------------------------------------------
//インデックスバッファを作成
//--------------------------------------------------------------------------------------
bool Renderer::CreateIndexBuffer(
	unsigned int indexnum,						// インデックス数
	void* indexdata,							// インデックスデータ格納メモリ先頭アドレス
	ID3D11Buffer** pIndexBuffer) {				// インデックスバッファ

	// インデックスバッファ生成
	D3D11_BUFFER_DESC bd;
	D3D11_SUBRESOURCE_DATA InitData;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;								// バッファ使用方
	bd.ByteWidth = sizeof(unsigned int) * indexnum;				// バッファの大き
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;						// インデックスバッファ
	bd.CPUAccessFlags = 0;										// CPUアクセス不要

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
//頂点バッファを作成
//--------------------------------------------------------------------------------------
bool Renderer::CreateVertexBuffer(
	unsigned int stride,				// １頂点当たりバイト数
	unsigned int vertexnum,				// 頂点数
	void* vertexdata,					// 頂点データ格納メモリ先頭アドレス
	ID3D11Buffer** pVertexBuffer) {		// 頂点バッファ

	HRESULT hr;

	// 頂点バッファ生成
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;				// バッファ使用方法
	bd.ByteWidth = stride * vertexnum;			// バッファの大きさ
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;	// 頂点バッファ
	bd.CPUAccessFlags = 0;						// CPUアクセス不要

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertexdata;				// バッファの初期値

	hr = m_pDevice->CreateBuffer(&bd, &InitData, pVertexBuffer);		// バッファ生成
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(vertex buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------
//頂点バッファを作成(CPU書き込み可能)
//--------------------------------------------------------------------------------------
bool Renderer::CreateVertexBufferWrite(
	unsigned int stride,				// １頂点当たりバイト数
	unsigned int vertexnum,				// 頂点数
	void* vertexdata,					// 頂点データ格納メモリ先頭アドレス
	ID3D11Buffer** pVertexBuffer) {		// 頂点バッファ

	HRESULT hr;

	// 頂点バッファ生成
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;							// バッファ使用方法
	bd.ByteWidth = stride * vertexnum;						// バッファの大きさ
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;				// 頂点バッファ
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;				// CPUアクセス可能

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertexdata;							// バッファの初期値

	hr = m_pDevice->CreateBuffer(&bd, &InitData, pVertexBuffer);		// バッファ生成
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(vertex buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------
//コンスタントバッファを作成
//--------------------------------------------------------------------------------------
bool Renderer::CreateConstantBuffer(
	unsigned int bytesize,					// コンスタントバッファサイズ
	ID3D11Buffer** pConstantBuffer) {			// コンスタントバッファ

	// コンスタントバッファ生成
	D3D11_BUFFER_DESC bd;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;								// バッファ使用方法
	bd.ByteWidth = bytesize;									// バッファの大き
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;					// コンスタントバッファ
	bd.CPUAccessFlags = 0;										// CPUアクセス不要

	HRESULT hr = m_pDevice->CreateBuffer(&bd, nullptr, pConstantBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(constant buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

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

void Renderer::ClearBackBuffer(float r, float g, float b, float a)
{
	float clearColor[4] = { r, g, b, a };

	m_pDeviceContext->ClearRenderTargetView(
		m_pRenderTargetView.Get(),
		clearColor
	);
}

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
//コンスタントバッファを作成(CPU書き込み可能)
//--------------------------------------------------------------------------------------
bool Renderer::CreateConstantBufferWrite(
	unsigned int bytesize,					// コンスタントバッファサイズ
	ID3D11Buffer** pConstantBuffer) {			// コンスタントバッファ

	// コンスタントバッファ生成
	D3D11_BUFFER_DESC bd;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;							// バッファ使用方法
	bd.ByteWidth = bytesize;									// バッファの大き
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;					// コンスタントバッファ
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;					// CPUアクセス可能

	HRESULT hr = m_pDevice->CreateBuffer(&bd, nullptr, pConstantBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(constant buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}
