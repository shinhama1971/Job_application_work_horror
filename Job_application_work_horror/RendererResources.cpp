// ============================================================================
// ファイルの役割: シェーダー・GPUのバッファの作成と、バックバッファの操作を担当している。
// 主な技術: COM、定数バッファ、頂点・インデックスバッファ、シェーダーのコンパイル
// ============================================================================

#include "Renderer.h"
#include "Application.h"

#include <algorithm>
#include <execution>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace DirectX::SimpleMath;

namespace
{
	// ビルド時の事前コンパイル（--compile-shaders）の間は、エラーを画面に出さずにためている（ビルドを止めないため）
	bool g_CollectShaderErrors = false;
	std::string g_ShaderErrors;

	// シェーダーのファイルごとの「自分と、#include しているファイル（入れ子も含む）のうち一番新しい更新時刻」を覚えている。
	// 同じ .hlsli を多くのシェーダーが読むので、1回調べたファイルは覚えておき、調べ直さない。
	// 事前コンパイルでは複数のスレッドから呼ぶので、覚えている表に鍵（mutex）を掛けている
	std::mutex g_SourceTimeMutex;
	std::unordered_map<std::wstring, std::filesystem::file_time_type> g_SourceTimes;

	// source と、そこから #include "名前" で読んでいるファイル（読む側のファイルと同じフォルダから探す。D3DCompileFromFile と同じ）の
	// うち、一番新しい更新時刻を返している。ファイルがなければ一番古い時刻を返す（配布用のフォルダには .hlsl を入れないため、
	// そのときは .cso をそのまま使える）。visiting は、今たどっている途中のファイル（互いに読み合うファイルで止まらないため）
	std::filesystem::file_time_type GetShaderSourceTime(
		const std::filesystem::path& source, std::vector<std::wstring>& visiting)
	{
		const std::wstring key = source.lexically_normal().wstring();
		{
			std::lock_guard<std::mutex> lock(g_SourceTimeMutex);
			const auto found = g_SourceTimes.find(key);
			if (found != g_SourceTimes.end())
			{
				return found->second;
			}
		}
		std::error_code error;
		std::filesystem::file_time_type newest = std::filesystem::last_write_time(source, error);
		if (error)
		{
			// （Windows の min マクロと区別するため、括弧で囲んでいる）
			return (std::filesystem::file_time_type::min)();
		}
		if (std::find(visiting.begin(), visiting.end(), key) != visiting.end())
		{
			return newest;
		}
		visiting.push_back(key);
		// 行の頭の #include "名前" を探し、読んでいるファイルの時刻も比べている（コメントの中の #include は、行の頭にないので数えない）
		std::ifstream file(source);
		std::string line;
		while (std::getline(file, line))
		{
			const size_t start = line.find_first_not_of(" \t");
			if (start == std::string::npos || line.compare(start, 8, "#include") != 0)
			{
				continue;
			}
			const size_t open = line.find('"', start + 8);
			const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
			if (close == std::string::npos)
			{
				continue;
			}
			const std::filesystem::path included = source.parent_path() / line.substr(open + 1, close - open - 1);
			newest = (std::max)(newest, GetShaderSourceTime(included, visiting));
		}
		visiting.pop_back();
		std::lock_guard<std::mutex> lock(g_SourceTimeMutex);
		g_SourceTimes.emplace(key, newest);
		return newest;
	}

	// .cso があり、そのシェーダーと、そこから読んでいるファイルのどれよりも新しければ使える。
	// 1つの .hlsli を変えたときは、それを読んでいるシェーダーだけが作り直しになる
	bool IsCompiledShaderUpToDate(const std::filesystem::path& csoPath, const std::filesystem::path& sourcePath)
	{
		std::error_code error;
		const auto compiledTime = std::filesystem::last_write_time(csoPath, error);
		if (error)
		{
			return false;
		}
		std::vector<std::wstring> visiting;
		return compiledTime >= GetShaderSourceTime(sourcePath, visiting);
	}

	// .hlsl を1つコンパイルし、成功したら隣に .cso として保存している。画面にも共有の変数にも触らないので、
	// 事前コンパイルで複数のスレッドから同時に呼んでも安全。失敗したときは、コンパイラーのエラーの文を errorText に入れる
	HRESULT CompileShaderFile(
		const std::filesystem::path& sourcePath,
		LPCSTR entryPoint,
		LPCSTR shaderModel,
		std::vector<unsigned char>& shaderObject,
		std::string& errorText)
	{
		// シェーダーのコンパイルの設定（古い書き方を許さない）
		DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
		shaderFlags |= D3DCOMPILE_DEBUG; // Debug構成では、シェーダーのデバッグ情報も含めている
#endif
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		const HRESULT hr = D3DCompileFromFile(
			sourcePath.wstring().c_str(),		// ファイル名
			nullptr,							// マクロの定義はなし
			D3D_COMPILE_STANDARD_FILE_INCLUDE,	// #include を使えるようにしている
			entryPoint,							// 入口の関数の名前
			shaderModel,						// シェーダーモデル
			shaderFlags,						// コンパイルの設定
			0,									// エフェクトの設定（使わない）
			blob.GetAddressOf(),				// 成功したときのコンパイル結果
			errorBlob.GetAddressOf());			// コンパイルエラーの出力
		if (FAILED(hr))
		{
			if (errorBlob != nullptr)
			{
				errorText = static_cast<const char*>(errorBlob->GetBufferPointer());
			}
			return E_FAIL;
		}

		// コンパイルに成功したら、バイナリを呼び出し元へ渡している
		shaderObject.resize(blob->GetBufferSize());
		memcpy(shaderObject.data(), blob->GetBufferPointer(), shaderObject.size());

		// 次の起動ではコンパイルせずに読めるよう、.csoとして保存している（書けなくても、そのまま続ける）
		std::filesystem::path csoPath = sourcePath;
		csoPath.replace_extension(".cso");
		std::ofstream compiledFile(csoPath, std::ios::binary | std::ios::trunc);
		if (compiledFile)
		{
			compiledFile.write(reinterpret_cast<const char*>(shaderObject.data()),
				static_cast<std::streamsize>(shaderObject.size()));
		}
		return S_OK;
	}
}

// シェーダーを読み込んでいる。新しいコンパイル済みの.csoがあればそれを使い、無いか古ければ.hlslをコンパイルして、.csoとして保存している。
// 重いシェーダーのコンパイルは1つ数秒かかるため、2回目からの起動（とビルド時に作った.cso）では、ゲームを始めるまでの待ち時間がほとんどなくなる。
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

	// 新しいcsoファイルがあれば開いている
	FILE* fp = nullptr;
	const int ret = IsCompiledShaderUpToDate(csoPath, std::filesystem::path(szFileName))
		? fopen_s(&fp, csoPath.string().c_str(), "rb")
		: -1;
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
	// csoファイルが無いか古ければ、hlslファイルをコンパイルしている
	else
	{
		WCHAR	filename[512];
		size_t 	wLen = 0;

		// 文字コードを Shift-JIS → UTF-16 に変換している
		setlocale(LC_ALL, "japanese");  // ロケールを日本語にしている（Windowsの日本語のパスを正しく変換するため）
		mbstowcs_s(&wLen, filename, 512, szFileName, _TRUNCATE);

		// HLSLファイルをコンパイルし、.cso として保存している（CompileShaderFile）
		std::string errorText;
		if (FAILED(CompileShaderFile(filename, szEntryPoint, szShaderModel, shaderObject, errorText)))
		{
			// コンパイルに失敗したら、エラーの内容を表示している（ビルド時の事前コンパイルの間は、ためて最後にまとめて出す）
			if (!errorText.empty())
			{
				if (g_CollectShaderErrors)
				{
					g_ShaderErrors += errorText + "\n";
				}
				else
				{
					MessageBoxA(NULL, errorText.c_str(), "Error", MB_OK);
				}
			}
			return E_FAIL;
		}
	}

	return S_OK;
}

// フォルダの中のシェーダーを、名前の最後（VS・PS・CS）か頭（PS_）からシェーダーモデルを決めて、まとめてコンパイルしている。
// common.hlsl のように、どれにも当てはまらないファイルは include 用なので飛ばしている。
// .cso が自分と読んでいるファイルより新しいシェーダーは作り直さず、作り直すシェーダーだけを、CPU のコアに分けて同時にコンパイルしている。
// （1つの .hlsli を変えたときは、それを読んでいるシェーダーだけが、並んで作り直しになる）
bool Renderer::PrecompileShaders(const std::filesystem::path& folder, std::string& errors)
{
	g_CollectShaderErrors = true;
	g_ShaderErrors.clear();
	bool succeeded = true;
	int shaderCount = 0;
	// 作り直すシェーダー1つ分（ファイル・シェーダーモデル）と、その結果
	struct CompileJob
	{
		std::filesystem::path Source;
		const char* ShaderModel = nullptr;
		bool Succeeded = false;
		std::string ErrorText;
	};
	std::vector<CompileJob> jobs;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(folder, error))
	{
		if (entry.path().extension() != ".hlsl")
		{
			continue;
		}
		const std::string name = entry.path().stem().string();
		const auto endsWith = [&name](const char* suffix)
		{
			const size_t length = strlen(suffix);
			return name.size() > length && name.compare(name.size() - length, length, suffix) == 0;
		};
		const char* shaderModel = nullptr;
		if (endsWith("VS")) { shaderModel = "vs_5_0"; }
		else if (endsWith("PS") || name.rfind("PS_", 0) == 0) { shaderModel = "ps_5_0"; }
		else if (endsWith("CS")) { shaderModel = "cs_5_0"; }
		if (shaderModel == nullptr)
		{
			continue;
		}
		++shaderCount;
		std::filesystem::path csoPath = entry.path();
		csoPath.replace_extension(".cso");
		if (!IsCompiledShaderUpToDate(csoPath, entry.path()))
		{
			jobs.push_back(CompileJob{ entry.path(), shaderModel });
		}
	}

	// 作り直すシェーダーを、標準ライブラリの並列実行（スレッドプール）で同時にコンパイルしている。
	// 1つずつの処理（CompileShaderFile）は共有の変数に触らないので、結果は1つ分ずつの入れ物に入れ、後でまとめている
	std::for_each(std::execution::par, jobs.begin(), jobs.end(), [](CompileJob& job)
	{
		std::vector<unsigned char> shaderObject;
		job.Succeeded = SUCCEEDED(CompileShaderFile(job.Source, "main", job.ShaderModel, shaderObject, job.ErrorText));
	});
	for (const CompileJob& job : jobs)
	{
		if (!job.Succeeded)
		{
			g_ShaderErrors += job.ErrorText + "\n";
			g_ShaderErrors += "コンパイルに失敗: " + job.Source.filename().string() + "\n";
			succeeded = false;
		}
	}

	if (error)
	{
		g_ShaderErrors += "シェーダーのフォルダを開けません: " + folder.string() + "\n";
		succeeded = false;
	}
	else if (shaderCount == 0 && succeeded)
	{
		g_ShaderErrors += "コンパイルするシェーダーが見つかりません: " + folder.string() + "\n";
		succeeded = false;
	}

	errors = g_ShaderErrors;
	g_CollectShaderErrors = false;
	g_ShaderErrors.clear();
	return succeeded;
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
	// 頂点番号から形を作る草には、頂点バッファの並び方は不要。ほかのモデルはこれまでどおり作る。
	if (numElements == 0)
	{
		*ppVertexLayout = nullptr;
		return S_OK;
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
