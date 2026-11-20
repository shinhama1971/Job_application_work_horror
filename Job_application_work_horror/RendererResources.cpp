// ============================================================================
// ファイルの役割: シェーダー・GPUのバッファの作成と、バックバッファの操作を担当している。
// 主な技術: COM、定数バッファ、頂点・インデックスバッファ、シェーダーのコンパイル
// ============================================================================

#include "Renderer.h"
#include "Application.h"
#include "Utility.h"

#include <algorithm>
#include <execution>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <limits>
#include <cstdint>
#include <d3d11shader.h>
#include <string>
#include <unordered_map>

using namespace DirectX::SimpleMath;

namespace
{
	// 保存したシェーダーが、このゲームで使うシェーダーモデル（5.0）でコンパイルした物かを確かめる番号
	constexpr UINT ShaderModelMajor = 5;
	constexpr UINT ShaderModelMinor = 0;

	// シェーダーのファイルごとの「自分と、#include しているファイル（入れ子も含む）のうち一番新しい更新時刻」を覚えている。
	// 同じ .hlsli を多くのシェーダーが読むので、1回調べたファイルは覚えておき、調べ直さない。
	// 事前コンパイルでは複数のスレッドから呼ぶので、覚えている表に鍵（mutex）を掛けている
	std::mutex g_SourceTimeMutex;
	std::unordered_map<std::wstring, std::filesystem::file_time_type> g_SourceTimes;

	// source と、そこから #include "名前" で読んでいるファイル（読む側のファイルと同じフォルダから探す。D3DCompileFromFile と同じ）の
	// うち、一番新しい更新時刻を返す。読み取れない依存先があればコンパイラに原因を報告させる。
	// visiting は循環参照を止めるため、今たどっているファイルだけを保持する。
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
			return (std::filesystem::file_time_type::max)();
		}
		if (std::find(visiting.begin(), visiting.end(), key) != visiting.end())
		{
			return newest;
		}
		visiting.push_back(key);
		// 行の頭の #include "名前" を探し、読んでいるファイルの時刻も比べている（コメントの中の #include は、行の頭にないので数えない）
		std::ifstream file(source);
		if (!file)
		{
			visiting.pop_back();
			return (std::filesystem::file_time_type::max)();
		}
		std::string line;
		while (std::getline(file, line))
		{
			const size_t start = line.find_first_not_of(" \t");
			constexpr std::string_view IncludeDirective = "#include";
			if (start == std::string::npos || line.compare(start, IncludeDirective.size(), IncludeDirective) != 0)
			{
				continue;
			}
			const size_t open = line.find('"', start + IncludeDirective.size());
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
		if (newest != (std::filesystem::file_time_type::max)())
		{
			g_SourceTimes.emplace(key, newest);
		}
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
		// 配布版はルートのソースを同梱しない。開発版では依存先も検証する。
		const bool hasSource = std::filesystem::exists(sourcePath, error);
		if (error || !hasSource)
		{
			return !error;
		}
		const auto sourceTime = GetShaderSourceTime(sourcePath, visiting);
		return sourceTime != (std::filesystem::file_time_type::max)() && compiledTime >= sourceTime;
	}

	// 空や破損したコンテナーと、別のステージ向けのCSOを新しいキャッシュとして扱わない。
	bool ReadShaderCache(const std::filesystem::path& path, std::string_view shaderModel,
		std::vector<unsigned char>& bytes)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return false;
		}
		const std::streamoff size = file.tellg();
		if (size <= 0 || static_cast<std::uintmax_t>(size) > bytes.max_size() ||
			size > (std::numeric_limits<std::streamsize>::max)())
		{
			return false;
		}
		bytes.resize(static_cast<std::size_t>(size));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		D3D11_SHADER_DESC description{};
		if (FAILED(D3DReflect(bytes.data(), bytes.size(), IID_PPV_ARGS(reflection.GetAddressOf()))) ||
			FAILED(reflection->GetDesc(&description)))
		{
			return false;
		}
		const auto expectedType = shaderModel == "vs_5_0" ? D3D11_SHVER_VERTEX_SHADER :
			shaderModel == "cs_5_0" ? D3D11_SHVER_COMPUTE_SHADER : D3D11_SHVER_PIXEL_SHADER;
		return D3D11_SHVER_GET_TYPE(description.Version) == static_cast<UINT>(expectedType) &&
			D3D11_SHVER_GET_MAJOR(description.Version) == ShaderModelMajor &&
			D3D11_SHVER_GET_MINOR(description.Version) == ShaderModelMinor;
	}

	template<class Create>
	HRESULT CreateShaderWithCacheRetry(const char* fileName, const char* model, const Create& create)
	{
		std::vector<unsigned char> bytes;
		bool usedCache = false;
		HRESULT hr = Renderer::CompileShader(fileName, "main", model, bytes, &usedCache);
		if (FAILED(hr))
		{
			return hr;
		}
		hr = create(bytes);
		std::error_code sourceError;
		if (FAILED(hr) && usedCache && std::filesystem::exists(std::filesystem::path(fileName), sourceError))
		{
			// リフレクションに出ない命令の不具合は、実際のGPU作成で失敗したときに再コンパイルする。
			hr = Renderer::CompileShader(fileName, "main", model, bytes, nullptr, true);
			if (SUCCEEDED(hr))
			{
				hr = create(bytes);
			}
		}
		return hr;
	}

	// .hlsl を1つコンパイルし、成功したら隣に .cso として保存している。画面にも共有の変数にも触らないので、
	// 事前コンパイルで複数のスレッドから同時に呼んでも安全。失敗したときは、コンパイラーのエラーの文を errorText に入れる
	HRESULT CompileShaderFile(
		const std::filesystem::path& sourcePath,
		LPCSTR entryPoint,
		LPCSTR shaderModel,
		std::vector<unsigned char>& shaderObject,
		std::string& errorText, bool requireCacheWrite = false)
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

		// 途中まで書いたCSOを別プロセスが読まないよう、閉じて確認したファイルだけを公開する。
		auto csoPath = sourcePath;
		csoPath.replace_extension(".cso");
		if (Utility::WriteTextAtomically(csoPath, {reinterpret_cast<const char*>(shaderObject.data()), shaderObject.size()})
			!= Utility::AtomicWriteResult::Success)
		{
			if (requireCacheWrite)
			{
				errorText = "シェーダーキャッシュを保存できません: " + csoPath.string();
				return E_FAIL;
			}
			// 実行時はメモリ上のコンパイル結果で続行できる。配布用の事前コンパイルだけは保存を必須とする。
			OutputDebugStringW((L"シェーダーキャッシュを保存できません: " + csoPath.wstring() + L"\n").c_str());
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
	std::vector<unsigned char>& shaderObject, bool* loadedFromCache, bool forceSource)
{
	shaderObject.clear();
	if (loadedFromCache != nullptr)
	{
		*loadedFromCache = false;
	}
	const std::filesystem::path sourcePath(szFileName);
	auto cachePath = sourcePath;
	cachePath.replace_extension(".cso");
	if (!forceSource && IsCompiledShaderUpToDate(cachePath, sourcePath) &&
		ReadShaderCache(cachePath, szShaderModel, shaderObject))
	{
		if (loadedFromCache != nullptr)
		{
			*loadedFromCache = true;
		}
		return S_OK;
	}

	shaderObject.clear();
	std::string errorText;
	const HRESULT hr = CompileShaderFile(sourcePath, szEntryPoint, szShaderModel, shaderObject, errorText);
	if (FAILED(hr) && !errorText.empty())
	{
		MessageBoxA(nullptr, errorText.c_str(), "Shader error", MB_OK | MB_ICONERROR);
	}
	return hr;
}

// フォルダの中のシェーダーを、名前の最後（VS・PS・CS）か頭（PS_）からシェーダーモデルを決めて、まとめてコンパイルしている。
// common.hlsl のように、どれにも当てはまらないファイルは include 用なので飛ばしている。
// .cso が自分と読んでいるファイルより新しいシェーダーは作り直さず、作り直すシェーダーだけを、CPU のコアに分けて同時にコンパイルしている。
// （1つの .hlsli を変えたときは、それを読んでいるシェーダーだけが、並んで作り直しになる）
bool Renderer::PrecompileShaders(const std::filesystem::path& folder, std::string& errors)
{
	errors.clear();
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
		std::vector<unsigned char> cached;
		if (!IsCompiledShaderUpToDate(csoPath, entry.path()) || !ReadShaderCache(csoPath, shaderModel, cached))
		{
			jobs.push_back(CompileJob{ entry.path(), shaderModel });
		}
	}

	// 作り直すシェーダーを、標準ライブラリの並列実行（スレッドプール）で同時にコンパイルしている。
	// 1つずつの処理（CompileShaderFile）は共有の変数に触らないので、結果は1つ分ずつの入れ物に入れ、後でまとめている
	std::for_each(std::execution::par, jobs.begin(), jobs.end(), [](CompileJob& job)
	{
		std::vector<unsigned char> shaderObject;
		job.Succeeded = SUCCEEDED(CompileShaderFile(job.Source, "main", job.ShaderModel, shaderObject, job.ErrorText, true));
	});
	for (const CompileJob& job : jobs)
	{
		if (!job.Succeeded)
		{
			errors += job.ErrorText + "\n";
			errors += "コンパイルに失敗: " + job.Source.filename().string() + "\n";
			succeeded = false;
		}
	}

	if (error)
	{
		errors += "シェーダーのフォルダを開けません: " + folder.string() + "\n";
		succeeded = false;
	}
	else if (shaderCount == 0 && succeeded)
	{
		errors += "コンパイルするシェーダーが見つかりません: " + folder.string() + "\n";
		succeeded = false;
	}

	return succeeded;
}

//--------------------------------------------------------------------------------------
// 頂点シェーダーと、入力レイアウト（頂点の並び方）を作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName)
{
	return CreateShaderWithCacheRetry(szFileName, "vs_5_0", [&](const std::vector<unsigned char>& bytes)
	{
		Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
		HRESULT hr = m_pDevice->CreateVertexShader(bytes.data(), bytes.size(), nullptr, shader.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		// 頂点番号から形を作る草は入力レイアウト不要。両方作れたときだけ呼び出し元へ渡す。
		Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
		if (numElements != 0)
		{
			hr = m_pDevice->CreateInputLayout(pLayout, numElements, bytes.data(), bytes.size(), layout.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}
		}
		*ppVertexShader = shader.Detach();
		*ppVertexLayout = layout.Detach();
		return S_OK;
	});
}

//--------------------------------------------------------------------------------------
// ピクセルシェーダーを作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreatePixelShader(ID3D11PixelShader** ppPixelShader, const char* szFileName)
{
	return CreateShaderWithCacheRetry(szFileName, "ps_5_0", [&](const std::vector<unsigned char>& bytes)
	{
		return m_pDevice->CreatePixelShader(bytes.data(), bytes.size(), nullptr, ppPixelShader);
	});
}

HRESULT Renderer::CreateComputeShader(ID3D11ComputeShader** shader, const char* fileName)
{
	return CreateShaderWithCacheRetry(fileName, "cs_5_0", [&](const std::vector<unsigned char>& bytes)
	{
		return m_pDevice->CreateComputeShader(bytes.data(), bytes.size(), nullptr, shader);
	});
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

void Renderer::CreateRequiredConstantBuffer(unsigned int bytesize, ID3D11Buffer** buffer, const char* purpose)
{
	if (!CreateConstantBuffer(bytesize, buffer))
	{
		Utility::ReportFatalError(std::string("定数バッファを作成できません: ") + purpose);
	}
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

// 本描画の間、描画先をHDRの描き先にしている。描画先を戻す処理（SetBackBufferRenderTarget）や、
// 描き終えた画面を写す処理（PostProcess::CaptureBackBuffer）は、そのままHDRの描き先を使う
void Renderer::BeginHdrScene()
{
	if (m_pSceneHdrRenderTargetView)
	{
		m_pRenderTargetView = m_pSceneHdrRenderTargetView;
	}
}

// 描画先をスワップチェーンのバックバッファに戻している
void Renderer::EndHdrScene()
{
	m_pRenderTargetView = m_pSwapChainRenderTargetView;
	SetBackBufferRenderTarget();
}
