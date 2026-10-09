// ============================================================================
// ファイルの役割: Direct3D 11のデバイス、描画の状態、ライト、各種の定数バッファを管理している。
// 主な技術: Direct3D 11、スワップチェーン、深度・ブレンド・ラスタライザーの状態、デバッグレイヤー
// ============================================================================


#include "Renderer.h"
#include "Application.h"
#include "CaptureMode.h"
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <chrono>

#pragma comment(lib, "dxgi.lib")


using namespace DirectX::SimpleMath;

namespace
{
	// 高性能なGPUを探している。内蔵GPUと単体GPUの両方を持つノートPCでは、何も指定しないと
	// 消費電力の少ない内蔵GPUで描画されることがあり、ライトや画面効果で大きく重くなるためである。
	// 見つからない（古いWindowsなど）ときはnullptrを返し、前と同じく既定のGPUを使っている。
	Microsoft::WRL::ComPtr<IDXGIAdapter1> FindHighPerformanceAdapter()
	{
		Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			return nullptr;
		}
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		// 「高性能な順」にGPUを並べてもらい、先頭から順に調べている
		for (UINT index = 0;
			SUCCEEDED(factory->EnumAdapterByGpuPreference(
				index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
			++index)
		{
			DXGI_ADAPTER_DESC1 description{};
			// ソフトウェア描画（Microsoft Basic Render Driver）は除いている。
			if (SUCCEEDED(adapter->GetDesc1(&description)) &&
				(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
			{
				return adapter;
			}
		}
		return nullptr;
	}
}

// 描画に使うGPUが内蔵GPUかを、専用のビデオメモリの量で判定している
bool Renderer::IsHighPerformanceAdapterIntegrated()
{
	const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = FindHighPerformanceAdapter();
	DXGI_ADAPTER_DESC1 description{};
	if (adapter == nullptr || FAILED(adapter->GetDesc1(&description)))
	{
		return false;
	}
	// 内蔵GPUはメインメモリを共有するため、専用のビデオメモリは128MB程度しかない（単体GPUは数GB）。そこで512MB未満を内蔵GPUとみなしている。
	constexpr SIZE_T IntegratedVideoMemoryLimit = 512ull * 1024ull * 1024ull;
	return description.DedicatedVideoMemory < IntegratedVideoMemoryLimit;
}

// Direct3Dの機能レベル（作ったデバイスが対応している版）
D3D_FEATURE_LEVEL Renderer::m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;

// デバイス＝DirectXの各種の資源（バッファ・テクスチャ・シェーダーなど）を作る役。ComPtrで持っているので自動で解放される
Microsoft::WRL::ComPtr<ID3D11Device> Renderer::m_pDevice;
// デバイスコンテキスト＝描画の命令を出す役
Microsoft::WRL::ComPtr<ID3D11DeviceContext> Renderer::m_pDeviceContext;
// スワップチェーン＝描いている画面と表示している画面を入れ替える仕組み（ダブルバッファ）
Microsoft::WRL::ComPtr<IDXGISwapChain> Renderer::m_pSwapChain;
// レンダーターゲットビュー＝描画先（バックバッファ）を表すもの
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Renderer::m_pRenderTargetView;
// 深度ステンシルビュー＝深度バッファ（奥行きで前後関係を判定する）
Microsoft::WRL::ComPtr<ID3D11DepthStencilView> Renderer::m_pDepthStencilView;

// ワールド・ビュー・射影の行列の定数バッファ（b0〜b2）
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pWorldBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pViewBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pProjectionBuffer;

// ライト（b3）・デバッグ表示（b7）・部屋の角の暗がり（b11）・マテリアル（b4）の定数バッファと、今のライト
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pLightBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pDebugViewBuffer;
DEBUG_VIEW_BUFFER Renderer::m_DebugView{ 0, 1.0f, 0.0f, 0.0f };
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pRoomOcclusionBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pMaterialBuffer;
LIGHT Renderer::m_Light{};
bool Renderer::m_LightEnable = true;
// UVの行列の定数バッファ（b5）
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pTextureBuffer;
// 深度の書き込みあり・なしの状態
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateEnable;
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateDisable;

// ブレンドの状態（なし・半透明・加算・減算）
Microsoft::WRL::ComPtr<ID3D11BlendState>
	Renderer::m_pBlendState[MAX_BLENDSTATE];



//--------------------------------------------------------------------------------------
// 初期化処理：デバイス・スワップチェーン・描画の状態・定数バッファを作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::Init()
{
	HRESULT hr = S_OK;

	// デバイスとスワップチェーンの設定
	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferCount = 1; // バックバッファの数は1（表示中の画面と合わせてダブルバッファ）
	swapChainDesc.BufferDesc.Width = Application::GetWidth(); // バッファの幅を描画解像度に合わせている
	swapChainDesc.BufferDesc.Height = Application::GetHeight(); // バッファの高さを描画解像度に合わせている
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 1画素を赤・緑・青・透明度の各8ビットにしている
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60; // リフレッシュレート（60Hz）
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画先として使う
	swapChainDesc.OutputWindow = Application::GetWindow(); // 表示するウィンドウ
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプリング（アンチエイリアス）は使わない
	swapChainDesc.SampleDesc.Quality = 0; // 同上
	swapChainDesc.Windowed = TRUE; // ウィンドウモード（全画面に広げたボーダーレスのウィンドウで表示している）

    // Debug構成では、D3D11のデバッグレイヤーを有効にして、使い方の間違いを出力させている
    UINT deviceCreationFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // 高性能なGPUがあればそれを使っている。GPUを指定するときは、ドライバーの種類をUNKNOWNにする決まりになっている。
    const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = FindHighPerformanceAdapter();
    const auto createDevice = [&](IDXGIAdapter* targetAdapter, UINT flags)
    {
        // デバイスとスワップチェーンを同時に作っている
        return D3D11CreateDeviceAndSwapChain(
            targetAdapter,      // 使うGPU。nullptrなら既定のGPU
            targetAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            NULL,               // ソフトウェアのラスタライザーは使わないのでNULL
            flags,
            NULL,               // 機能レベルの配列。NULLなら既定の組み合わせを使う
            0,                  // 機能レベルの配列の要素数（NULLなら0）
            D3D11_SDK_VERSION,  // SDKの版（常に D3D11_SDK_VERSION を指定する）
            &swapChainDesc,     // スワップチェーンの設定
            m_pSwapChain.ReleaseAndGetAddressOf(),
            m_pDevice.ReleaseAndGetAddressOf(),
            &m_FeatureLevel,    // 作ったデバイスの機能レベルを受け取る
            m_pDeviceContext.ReleaseAndGetAddressOf());
    };

    hr = createDevice(adapter.Get(), deviceCreationFlags);
#if defined(DEBUG) || defined(_DEBUG)
    // グラフィックスのデバッグ機能（任意で入れる部品）が無いPCでも、ゲームを起動できるようにしている。
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        hr = createDevice(adapter.Get(), 0);
    }
#endif
    // 選んだGPUで作れなかったときは、既定のGPUでもう一度試している。
    if (FAILED(hr) && adapter != nullptr)
    {
        hr = createDevice(nullptr, deviceCreationFlags);
#if defined(DEBUG) || defined(_DEBUG)
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            hr = createDevice(nullptr, 0);
        }
#endif
    }
    if (FAILED(hr)) return hr;

	// 描画先・深度バッファを作っている
	hr = CreateRenderAndDepthResources();
	if (FAILED(hr)) return hr;

	// ビューポート（描く範囲）を描画解像度の全体にしている
	D3D11_VIEWPORT viewport{};
	viewport.Width = (FLOAT)Application::GetWidth();   // ビューポートの幅
	viewport.Height = (FLOAT)Application::GetHeight(); // ビューポートの高さ
	viewport.MinDepth = 0.0f;                          // 深度の範囲の最小値
	viewport.MaxDepth = 1.0f;                          // 深度の範囲の最大値
	viewport.TopLeftX = 0;                             // ビューポートの左上のX座標
	viewport.TopLeftY = 0;                             // ビューポートの左上のY座標
	m_pDeviceContext->RSSetViewports(1, &viewport);


	// ラスタライザーの状態（三角形の塗り方）
	D3D11_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID; // 面を塗りつぶしている
	// （線だけで確かめたいときは D3D11_FILL_WIREFRAME にする）
	rasterizerDesc.CullMode = D3D11_CULL_BACK; // 裏を向いた面は描かない
	// （表を向いた面を描かないときは D3D11_CULL_FRONT）
	// （両面とも描くときは D3D11_CULL_NONE。コードで作る形は表と裏の両方の面を持たせているので、背面カリングのままでよい）
	rasterizerDesc.DepthClipEnable = TRUE;
	rasterizerDesc.MultisampleEnable = FALSE;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs;
	hr = m_pDevice->CreateRasterizerState(&rasterizerDesc, rs.GetAddressOf());
	if (FAILED(hr)) return hr;
	m_pDeviceContext->RSSetState(rs.Get());
	// ブレンドの状態を作っている（まずは重ねない設定）
	D3D11_BLEND_DESC BlendDesc{};
	BlendDesc.AlphaToCoverageEnable = FALSE;                     // 透明度をカバレッジとして使わない
	BlendDesc.IndependentBlendEnable = TRUE;                     // 描画先ごとに別のブレンドの設定を使えるようにしている
	BlendDesc.RenderTarget[0].BlendEnable = FALSE;               // ブレンドしない（不透明な描画）
	BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;  // 描く色には、その色の透明度を掛ける
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // 下の色には、(1 - 透明度) を掛ける
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;      // 2つを足す
	BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;   // 透明度はそのまま使う
	BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; // 下の透明度は使わない
	BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; // 透明度も足す
	BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; // 赤・緑・青・透明度のすべてを書き込む
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[0].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// 半透明の合成（上の設定でブレンドを有効にしたもの）
	BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[1].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// 加算合成（下の色を減らさずに足す）
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[2].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// 減算合成（下の色から引く）
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[3].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	SetBlendState(BS_ALPHABLEND);

	// 深度ステンシルの状態：深度テストは「手前か同じなら描く」
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	hr = m_pDevice->CreateDepthStencilState(
		&depthStencilDesc, m_pDepthStateEnable.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// 深度の書き込みだけを止めた状態（半透明・加算合成の描画用）
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	hr = m_pDevice->CreateDepthStencilState(
		&depthStencilDesc, m_pDepthStateDisable.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateEnable.Get(), NULL);

	// サンプラーの状態：異方性フィルタリング（4倍）で、UVが0〜1を超えたら繰り返している
	D3D11_SAMPLER_DESC smpDesc{};
	smpDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	smpDesc.MaxAnisotropy = 4;
	smpDesc.MaxLOD = D3D11_FLOAT32_MAX;

	Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;
	hr = m_pDevice->CreateSamplerState(&smpDesc, samplerState.GetAddressOf());
	if (FAILED(hr)) return hr;

	m_pDeviceContext->PSSetSamplers(0, 1, samplerState.GetAddressOf());

	// 行列の定数バッファを作り、頂点シェーダーのb0（ワールド）・b1（ビュー）・b2（射影）に設定している
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = sizeof(Matrix);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = sizeof(float);

	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pWorldBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(0, 1, m_pWorldBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;

	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pViewBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(1, 1, m_pViewBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;

	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pProjectionBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(2, 1, m_pProjectionBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;


	// ライトの定数バッファを作り、頂点・ピクセルシェーダーのb3に設定している
	bufferDesc.ByteWidth = sizeof(LIGHT);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pLightBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(3, 1, m_pLightBuffer.GetAddressOf());
	m_pDeviceContext->PSSetConstantBuffers(3, 1, m_pLightBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;
	// ライトの初期値（Playerが毎フレーム上書きしている）
	LIGHT light{};
	light.Enable = TRUE;
	light.FlashlightEnabled = TRUE;
	light.Intensity = 1.6f;
	light.Range = 260.0f;
	light.Direction = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
	light.Diffuse = Color(1.4f, 1.25f, 1.0f, 1.0f);
	light.Ambient = Color(0.055f, 0.055f, 0.065f, 1.0f);
	light.SpotParams = Vector4(
		cosf(DirectX::XMConvertToRadians(16.0f)),
		cosf(DirectX::XMConvertToRadians(32.0f)), 1.35f, 0.0f);

	SetLight(light);

	// デバッグ表示と壁の古さの定数バッファを作り、ピクセルシェーダーのb7に設定している
	bufferDesc.ByteWidth = sizeof(DEBUG_VIEW_BUFFER);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pDebugViewBuffer.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	m_pDeviceContext->PSSetConstantBuffers(
		7, 1, m_pDebugViewBuffer.GetAddressOf());
	SetDebugViewMode(0);

	// 部屋の角の暗がり。最初は壁がない（暗がりなし）状態にしている。
	bufferDesc.ByteWidth = sizeof(ROOM_OCCLUSION_BUFFER);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pRoomOcclusionBuffer.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);

	// マテリアルの定数バッファを作り、頂点・ピクセルシェーダーのb4に設定している
	bufferDesc.ByteWidth = sizeof(MATERIAL);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pMaterialBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(4, 1, m_pMaterialBuffer.GetAddressOf());
	m_pDeviceContext->PSSetConstantBuffers(4, 1, m_pMaterialBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;

	// マテリアルの初期値（白）
	MATERIAL material{};
	material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = Color(1.0f, 1.0f, 1.0f, 1.0f);
	SetMaterial(material);

	// UVの行列の定数バッファを作り、頂点シェーダーのb5に設定している
	bufferDesc.ByteWidth = sizeof(Matrix);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pTextureBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(5, 1, m_pTextureBuffer.GetAddressOf());
	if (FAILED(hr))return hr;

	// UVの初期値（拡大も移動もしない）
	SetUV(0, 0, 1, 1);
	return S_OK;
}

//--------------------------------------------------------------------------------------
// 描画先（バックバッファ）と、深度バッファを作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateRenderAndDepthResources()
{
    // スワップチェーンのバックバッファから、描画先のビューを作っている
    Microsoft::WRL::ComPtr<ID3D11Texture2D> renderTarget;
    HRESULT hr = m_pSwapChain->GetBuffer(
        0,
        IID_PPV_ARGS(renderTarget.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = m_pDevice->CreateRenderTargetView(
        renderTarget.Get(),
        NULL,
		m_pRenderTargetView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

	// 深度ステンシルバッファを作っている
	// （深度バッファ＝Zバッファ。奥行きを判定して、前後関係を正しく描けるようにしている）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = Application::GetWidth();   // バッファの幅を描画解像度に合わせている
	textureDesc.Height = Application::GetHeight(); // バッファの高さを描画解像度に合わせている
	textureDesc.MipLevels = 1;                            // ミップマップは使わない
	textureDesc.ArraySize = 1;                            // 配列ではない1枚のテクスチャ
	textureDesc.Format = DXGI_FORMAT_D16_UNORM;           // 16ビットの深度バッファを使っている
	textureDesc.SampleDesc.Count = 1;                     // スワップチェーンと同じサンプルの設定
	textureDesc.SampleDesc.Quality = 0;                   // 同上
	textureDesc.Usage = D3D11_USAGE_DEFAULT;              // GPUだけで使う
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;     // 深度ステンシルバッファとして使う
	textureDesc.CPUAccessFlags = 0;                       // CPUからは読み書きしない
	textureDesc.MiscFlags = 0;                            // その他のフラグはなし
    hr = m_pDevice->CreateTexture2D(
        &textureDesc,
        NULL,
        depthStencil.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

	// 深度ステンシルビューを作っている
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = textureDesc.Format; // 深度ステンシルバッファと同じ形式
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャ用のビューにしている
	depthStencilViewDesc.Flags = 0; // 特別なフラグはなし
    hr = m_pDevice->CreateDepthStencilView(
        depthStencil.Get(),
        &depthStencilViewDesc,
		m_pDepthStencilView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// 終了処理：Debug構成では、解放し忘れたDirect3Dの資源が無いかを最後に報告させている
//--------------------------------------------------------------------------------------
void Renderer::Uninit()
{
    Microsoft::WRL::ComPtr<ID3D11Debug> debugInterface;
#if defined(DEBUG) || defined(_DEBUG)
    if (m_pDevice != nullptr)
    {
        m_pDevice->QueryInterface(
            IID_PPV_ARGS(debugInterface.ReleaseAndGetAddressOf()));
    }
#endif

    // 描画の状態をすべて外し、溜まっている命令をGPUへ送っている
    if (m_pDeviceContext != nullptr)
    {
        m_pDeviceContext->ClearState();
        m_pDeviceContext->Flush();
    }

	// 定数バッファ・描画の状態・描画先・スワップチェーンを解放している
	m_pLightBuffer.Reset();
	m_pDebugViewBuffer.Reset();
	m_pMaterialBuffer.Reset();
	m_pTextureBuffer.Reset();

	m_pWorldBuffer.Reset();
	m_pViewBuffer.Reset();
	m_pProjectionBuffer.Reset();

	m_pDepthStateEnable.Reset();
	m_pDepthStateDisable.Reset();
	for (int i = 0; i < MAX_BLENDSTATE; i++)
	{
		m_pBlendState[i].Reset();
	}
	m_pDepthStencilView.Reset();
	m_pRenderTargetView.Reset();
	m_pSwapChain.Reset();

    // 資源の解放はドライバー側で後回しにされることがあるため、デバイスコンテキストを壊す前にFlushしている。
    // デバッグの報告が、解放待ちのものをアプリが持ち続けている物と間違えないようにするためである。
    if (m_pDeviceContext != nullptr)
    {
        m_pDeviceContext->Flush();
    }
	m_pDeviceContext.Reset();
	m_pDevice.Reset();

#if defined(DEBUG) || defined(_DEBUG)
    if (debugInterface)
    {
        debugInterface->ReportLiveDeviceObjects(
            D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
    }
#endif
}

//--------------------------------------------------------------------------------------
// 描画開始：バックバッファを描画先にし、背景色と深度を消している
//--------------------------------------------------------------------------------------
void Renderer::DrawStart()
{
	// 画面を塗りつぶす色
	float clearColor[4] = { 0.003f, 0.005f, 0.008f, 1.0f }; // 背景色（わずかに青みのある黒）

	// 描画先と、使う深度バッファを指定している
	m_pDeviceContext->OMSetRenderTargets(
		1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
	// 描画先を背景色で塗りつぶしている
	m_pDeviceContext->ClearRenderTargetView(
		m_pRenderTargetView.Get(), clearColor);
	// 深度バッファを一番奥（1.0）で消している
	m_pDeviceContext->ClearDepthStencilView(
		m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

//--------------------------------------------------------------------------------------
//描画終了
// 描画終了：描いた画面を表示している
void Renderer::DrawEnd()
{
	// 自動撮影モード（--capture）のときだけ、描き終えた画面を動画と静止画に書き出している。
	if (Tools::CaptureMode::IsActive())
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		if (SUCCEEDED(m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		{
			Tools::CaptureMode::OnFrameRendered(m_pDeviceContext.Get(), backBuffer.Get());
		}
	}

	// ダブルバッファを入れ替えて画面を更新している（普段は垂直同期を待っている）。
	// 計測モード（--benchmark）は垂直同期を待たず、本来の処理時間を測れるようにしている。
	if (Tools::CaptureMode::IsBenchmark())
	{
		const auto presentStart = std::chrono::steady_clock::now();
		m_pSwapChain->Present(0, 0);
		Tools::CaptureMode::OnPresentTimed(std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - presentStart).count());
		return;
	}
	m_pSwapChain->Present(1, 0);
}

//--------------------------------------------------------------------------------------
// ウィンドウの大きさに合わせて、バックバッファと深度バッファを作り直している
//--------------------------------------------------------------------------------------
HRESULT Renderer::ResizeWindow(int width, int height)
{
	// スワップチェーンが無い場合は何もしない
	if (!m_pSwapChain)return S_FALSE;

	// デバイスコンテキストも今のバックバッファを参照しているため、
	// ResizeBuffersの前に設定を外して、すべての参照を手放している。
	m_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	m_pDeviceContext->Flush();

	// 今の描画先のビューを解放している
	m_pRenderTargetView.Reset();

	// 今の深度ステンシルビューを解放している
	m_pDepthStencilView.Reset();

	// バッファは描画解像度のままにしている。画面効果やHUDのテクスチャも同じ大きさで作っているため、
	// ウィンドウの大きさに合わせると大きさが食い違う。ウィンドウへの引き伸ばしは表示のときに行われる。
	(void)width;
	(void)height;
	HRESULT hr = m_pSwapChain->ResizeBuffers(
		0, Application::GetWidth(), Application::GetHeight(), DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) return hr;

	// 描画先・深度バッファを作り直している
	hr = CreateRenderAndDepthResources();
	if (FAILED(hr)) return hr;

	D3D11_VIEWPORT vi = {};
	vi.Width = static_cast<float>(Application::GetWidth());
	vi.Height = static_cast<float>(Application::GetHeight());
	vi.MinDepth = 0.0f;
	vi.MaxDepth = 1.0f;

	// ビューポートを設定し直している
	m_pDeviceContext->RSSetViewports(1, &vi);

	return S_OK;
}
// Rendererのデバイスまわりの処理はここまで（描画の状態の設定はRendererState.cpp、資源の作成はRendererResources.cpp）。
