// ============================================================================
// ファイルの役割: Direct3D 11デバイス、描画状態、ライト、各種定数バッファを管理します。
// 主な技術: Direct3D 11、Swap Chain、深度・ブレンド・ラスタライザ状態、Debug Layer
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
	// 高性能なGPUを探します。内蔵GPUと単体GPUの両方を持つノートPCでは、何も指定しないと
	// 消費電力の少ない内蔵GPUで描画されることがあり、ライトや画面効果で大きく重くなるためです。
	// 見つからない（古いWindowsなど）ときはnullptrを返し、従来どおり既定のGPUを使います。
	Microsoft::WRL::ComPtr<IDXGIAdapter1> FindHighPerformanceAdapter()
	{
		Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			return nullptr;
		}
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		for (UINT index = 0;
			SUCCEEDED(factory->EnumAdapterByGpuPreference(
				index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
			++index)
		{
			DXGI_ADAPTER_DESC1 description{};
			// ソフトウェア描画（Microsoft Basic Render Driver）は除きます。
			if (SUCCEEDED(adapter->GetDesc1(&description)) &&
				(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
			{
				return adapter;
			}
		}
		return nullptr;
	}
}

bool Renderer::IsHighPerformanceAdapterIntegrated()
{
	const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = FindHighPerformanceAdapter();
	DXGI_ADAPTER_DESC1 description{};
	if (adapter == nullptr || FAILED(adapter->GetDesc1(&description)))
	{
		return false;
	}
	// 内蔵GPUはメインメモリを共有するため、専用のビデオメモリは128MB程度しかありません（単体GPUは数GB）。
	constexpr SIZE_T IntegratedVideoMemoryLimit = 512ull * 1024ull * 1024ull;
	return description.DedicatedVideoMemory < IntegratedVideoMemoryLimit;
}

//Direct3Dのバージョン
D3D_FEATURE_LEVEL Renderer::m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;

// デバイス＝DirectXの各種機能を作る ※ID3D11で始まるポインタ型の変数は、解放する必要がある
Microsoft::WRL::ComPtr<ID3D11Device> Renderer::m_pDevice;
// コンテキスト＝描画関連を司る機能
Microsoft::WRL::ComPtr<ID3D11DeviceContext> Renderer::m_pDeviceContext;
// スワップチェイン＝ダブルバッファ機能
Microsoft::WRL::ComPtr<IDXGISwapChain> Renderer::m_pSwapChain;
// レンダーターゲット＝描画先を表す機能
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Renderer::m_pRenderTargetView;
// デプスバッファ
Microsoft::WRL::ComPtr<ID3D11DepthStencilView> Renderer::m_pDepthStencilView;

Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pWorldBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pViewBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pProjectionBuffer;

Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pLightBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pDebugViewBuffer;
DEBUG_VIEW_BUFFER Renderer::m_DebugView{ 0, 1.0f, 0.0f, 0.0f };
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pRoomOcclusionBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pMaterialBuffer;
LIGHT Renderer::m_Light{};
bool Renderer::m_LightEnable = true;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pTextureBuffer;
// デプスステンシルステート
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateEnable;
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateDisable;

Microsoft::WRL::ComPtr<ID3D11BlendState>
	Renderer::m_pBlendState[MAX_BLENDSTATE];



//--------------------------------------------------------------------------------------
//初期化処理
//--------------------------------------------------------------------------------------
HRESULT Renderer::Init()
{
	HRESULT hr = S_OK;

	// デバイス、スワップチェーン作成
	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferCount = 1; // バックバッファの数を1に設定（ダブルバッファリング）
	swapChainDesc.BufferDesc.Width = Application::GetWidth(); // バッファの幅をウィンドウサイズに合わせる
	swapChainDesc.BufferDesc.Height = Application::GetHeight(); // バッファの高さをウィンドウサイズに合わせる
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // バッファのピクセルフォーマットを設定
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60; // リフレッシュレートを設定（Hz）
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // バッファの使用用途を設定
	swapChainDesc.OutputWindow = Application::GetWindow(); // スワップチェーンのターゲットウィンドウを設定
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプリングの設定（アンチエイリアスのサンプル数とクオリティ）
	swapChainDesc.SampleDesc.Quality = 0; //同上
	swapChainDesc.Windowed = TRUE; // ウィンドウモード（フルスクリーンではなく、ウィンドウモードで実行）

    UINT deviceCreationFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // 高性能なGPUがあればそれを使います。GPUを指定するときはドライバータイプをUNKNOWNにする決まりです。
    const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = FindHighPerformanceAdapter();
    const auto createDevice = [&](IDXGIAdapter* targetAdapter, UINT flags)
    {
        // デバイスとスワップチェインを同時に作成する関数の呼び出し
        return D3D11CreateDeviceAndSwapChain(
            targetAdapter,      // 使うGPU。nullptrなら既定のGPU
            targetAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            NULL,               // ソフトウェアラスタライザを指定しないのでNULL
            flags,
            NULL,               // 機能レベルの配列。NULLならデフォルトの機能レベルセットが使われる
            0,                  // 機能レベルの配列の要素数(NULLなら0でOK)
            D3D11_SDK_VERSION,  // SDKのバージョン 常に「D3D11_SDK_VERSION」を指定
            &swapChainDesc,     // スワップチェーンの設定構造体へのポインタ
            m_pSwapChain.ReleaseAndGetAddressOf(),
            m_pDevice.ReleaseAndGetAddressOf(),
            &m_FeatureLevel,    // 作成されたデバイスの機能レベルを受け取る変数へのポインタ
            m_pDeviceContext.ReleaseAndGetAddressOf());
    };

    hr = createDevice(adapter.Get(), deviceCreationFlags);
#if defined(DEBUG) || defined(_DEBUG)
    // 任意のグラフィックスデバッグ機能が使えないPCでもゲームを起動できるようにします。
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        hr = createDevice(adapter.Get(), 0);
    }
#endif
    // 選んだGPUで作れなかったときは、既定のGPUでもう一度試します。
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

	// レンダーターゲットビュー・デプスステンシルバッファ・デプスステンシルビュー作成
	hr = CreateRenderAndDepthResources();
	if (FAILED(hr)) return hr;

	// ビューポート設定
	D3D11_VIEWPORT viewport{};
	viewport.Width = (FLOAT)Application::GetWidth();   // ビューポートの幅
	viewport.Height = (FLOAT)Application::GetHeight(); // ビューポートの高さ
	viewport.MinDepth = 0.0f;                          // 深度範囲の最小値
	viewport.MaxDepth = 1.0f;                          // 深度範囲の最大値
	viewport.TopLeftX = 0;                             // ビューポートの左上隅のX座標
	viewport.TopLeftY = 0;                             // ビューポートの左上隅のY座標）
	m_pDeviceContext->RSSetViewports(1, &viewport);


	// ラスタライザステート設定
	D3D11_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID; //ソリッド
	//rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME; //ワイヤーフレーム
	rasterizerDesc.CullMode = D3D11_CULL_BACK; //ポリゴン裏をカリング
	//rasterizerDesc.CullMode = D3D11_CULL_FRONT; //ポリゴン表をカリング
	//rasterizerDesc.CullMode = D3D11_CULL_NONE; //カリングしない(裏も表も表示される)
	rasterizerDesc.DepthClipEnable = TRUE;
	rasterizerDesc.MultisampleEnable = FALSE;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs;
	hr = m_pDevice->CreateRasterizerState(&rasterizerDesc, rs.GetAddressOf());
	if (FAILED(hr)) return hr;
	m_pDeviceContext->RSSetState(rs.Get());
	// ブレンド ステート生成
	D3D11_BLEND_DESC BlendDesc{};
	BlendDesc.AlphaToCoverageEnable = FALSE;                     // アルファ・トゥ・カバレッジを無効化（透明度をカバレッジとして利用しない）
	BlendDesc.IndependentBlendEnable = TRUE;                     // 各レンダーターゲットに対して個別のブレンド設定を有効化
	BlendDesc.RenderTarget[0].BlendEnable = FALSE;               // ブレンドを無効に設定（不透明な描画）
	BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;  // ソース（描画するピクセル）のアルファ値を使用
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // デスティネーション（既存のピクセル）の逆アルファ値を使用
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;      // ソースとデスティネーションを加算する操作
	BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;   // ソースのアルファ値をそのまま使用
	BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; // デスティネーションのアルファ値を無視
	BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; // アルファ値に対して加算操作を行う
	BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; // レンダーターゲットのカラーチャンネル書き込みマスク
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[0].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// ブレンド ステート生成 (アルファ ブレンド用)
	BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[1].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// ブレンド ステート生成 (加算合成用)
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[2].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// ブレンド ステート生成 (減算合成用)
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
	hr = m_pDevice->CreateBlendState(
		&BlendDesc, m_pBlendState[3].ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	SetBlendState(BS_ALPHABLEND);

	// デプスステンシルステート設定
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	hr = m_pDevice->CreateDepthStencilState(
		&depthStencilDesc, m_pDepthStateEnable.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	hr = m_pDevice->CreateDepthStencilState(
		&depthStencilDesc, m_pDepthStateDisable.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateEnable.Get(), NULL);

	// サンプラーステート設定
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

	// 定数バッファ生成
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


	bufferDesc.ByteWidth = sizeof(LIGHT);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pLightBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(3, 1, m_pLightBuffer.GetAddressOf());
	m_pDeviceContext->PSSetConstantBuffers(3, 1, m_pLightBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;
	//ライト初期化　
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

	bufferDesc.ByteWidth = sizeof(DEBUG_VIEW_BUFFER);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pDebugViewBuffer.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	m_pDeviceContext->PSSetConstantBuffers(
		7, 1, m_pDebugViewBuffer.GetAddressOf());
	SetDebugViewMode(0);

	// 部屋の角の暗がり。最初は壁がない（暗がりなし）状態にしておきます。
	bufferDesc.ByteWidth = sizeof(ROOM_OCCLUSION_BUFFER);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pRoomOcclusionBuffer.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);

	bufferDesc.ByteWidth = sizeof(MATERIAL);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pMaterialBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(4, 1, m_pMaterialBuffer.GetAddressOf());
	m_pDeviceContext->PSSetConstantBuffers(4, 1, m_pMaterialBuffer.GetAddressOf());
	if (FAILED(hr)) return hr;

	//マテリアル初期化
	MATERIAL material{};
	material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = Color(1.0f, 1.0f, 1.0f, 1.0f);
	SetMaterial(material);

	bufferDesc.ByteWidth = sizeof(Matrix);
	hr = m_pDevice->CreateBuffer(
		&bufferDesc, NULL, m_pTextureBuffer.ReleaseAndGetAddressOf());
	m_pDeviceContext->VSSetConstantBuffers(5, 1, m_pTextureBuffer.GetAddressOf());
	if (FAILED(hr))return hr;

	//UV初期化
	SetUV(0, 0, 1, 1);
	return S_OK;
}

//--------------------------------------------------------------------------------------
// レンダーターゲットビュー・デプスステンシルバッファ・デプスステンシルビュー作成
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateRenderAndDepthResources()
{
    // レンダーターゲットビュー作成
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

	// デプスステンシルバッファ作成
	// ※（デプスバッファ = 深度バッファ = Zバッファ）→奥行を判定して前後関係を正しく描画できる
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = Application::GetWidth();   // バッファの幅をスワップチェーンに合わせる
	textureDesc.Height = Application::GetHeight(); // バッファの高さをスワップチェーンに合わせる
	textureDesc.MipLevels = 1;                            // ミップレベルは1（ミップマップは使用しない）
	textureDesc.ArraySize = 1;                            // テクスチャの配列サイズ（通常1）
	textureDesc.Format = DXGI_FORMAT_D16_UNORM;           // フォーマットは16ビットの深度バッファを使用
	textureDesc.SampleDesc.Count = 1;                     // スワップチェーンと同じサンプル設定
	textureDesc.SampleDesc.Quality = 0;                   // 同上
	textureDesc.Usage = D3D11_USAGE_DEFAULT;              // 使用方法はデフォルト（GPUで使用）
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;     // 深度ステンシルバッファとして使用
	textureDesc.CPUAccessFlags = 0;                       // CPUからのアクセスは不要
	textureDesc.MiscFlags = 0;                            // その他のフラグは設定なし
    hr = m_pDevice->CreateTexture2D(
        &textureDesc,
        NULL,
        depthStencil.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

	// デプスステンシルビュー作成
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = textureDesc.Format; // デプスステンシルバッファのフォーマットを設定
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; // ビューの次元を2Dテクスチャとして設定（2Dテクスチャ用のデプスステンシルビュー）
	depthStencilViewDesc.Flags = 0; // 特別なフラグは設定しない（デフォルトの動作）
    hr = m_pDevice->CreateDepthStencilView(
        depthStencil.Get(),
        &depthStencilViewDesc,
		m_pDepthStencilView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    return S_OK;
}

//--------------------------------------------------------------------------------------
//終了処理
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

    if (m_pDeviceContext != nullptr)
    {
        m_pDeviceContext->ClearState();
        m_pDeviceContext->Flush();
    }

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

    // リソース解放はドライバー側で遅延処理される場合があるため、コンテキスト破棄前にFlushします。
    // デバッグレポートが解放待ちをアプリ所有の生存オブジェクトと誤認しないためです。
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
//描画開始
//--------------------------------------------------------------------------------------
void Renderer::DrawStart()
{
	// 画面塗りつぶし色
	float clearColor[4] = { 0.003f, 0.005f, 0.008f, 1.0f }; // 背景色（わずかに青みのある黒）

	// 描画先のキャンバスと使用する深度バッファを指定する
	m_pDeviceContext->OMSetRenderTargets(
		1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
	// 描画先キャンバスを塗りつぶす
	m_pDeviceContext->ClearRenderTargetView(
		m_pRenderTargetView.Get(), clearColor);
	// 深度バッファをリセットする
	m_pDeviceContext->ClearDepthStencilView(
		m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

//--------------------------------------------------------------------------------------
//描画終了
//--------------------------------------------------------------------------------------
void Renderer::DrawEnd()
{
	// 自動撮影モード（--capture）のときだけ、描き終えた画面を動画と静止画に書き出します。
	if (Tools::CaptureMode::IsActive())
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		if (SUCCEEDED(m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		{
			Tools::CaptureMode::OnFrameRendered(m_pDeviceContext.Get(), backBuffer.Get());
		}
	}

	// ダブルバッファの切り替えを行い画面を更新する
	// 計測モード（--benchmark）は垂直同期を待たず、本来の処理時間を測れるようにします。
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
// ウィンドウの大きさに合わせて、バックバッファと深度バッファを作り直します
//--------------------------------------------------------------------------------------
HRESULT Renderer::ResizeWindow(int width, int height)
{
	// スワップチェインが存在しない場合は処理しない
	if (!m_pSwapChain)return S_FALSE;

	// デバイスコンテキストも現在のバックバッファを参照するため、
	// ResizeBuffers前にバインドを解除して全参照を解放します。
	m_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	m_pDeviceContext->Flush();

	// 既存のレンダーターゲットビューを解放
	m_pRenderTargetView.Reset();

	// 既存のデプスステンシルビューを解放
	m_pDepthStencilView.Reset();

	// バッファは描画解像度のままにします。画面効果やHUDのテクスチャも同じ大きさで作っているため、
	// ウィンドウの大きさに合わせると大きさが食い違います。ウィンドウへの引き伸ばしは表示のときに行われます。
	(void)width;
	(void)height;
	HRESULT hr = m_pSwapChain->ResizeBuffers(
		0, Application::GetWidth(), Application::GetHeight(), DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) return hr;

	// レンダーターゲットビュー・デプスステンシルバッファ・デプスステンシルビュー作成
	hr = CreateRenderAndDepthResources();
	if (FAILED(hr)) return hr;

	D3D11_VIEWPORT vi = {};
	vi.Width = static_cast<float>(Application::GetWidth());
	vi.Height = static_cast<float>(Application::GetHeight());
	vi.MinDepth = 0.0f;
	vi.MaxDepth = 1.0f;

	// ビューポートを設定
	m_pDeviceContext->RSSetViewports(1, &vi);

	return S_OK;
}
// Rendererのデバイス管理処理はここまでです。
