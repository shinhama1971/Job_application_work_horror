

// ============================================================================
// ファイルの役割: Direct3D 11のデバイス、描画の状態、ライト、各種の定数バッファを管理している。
// 主な技術: Direct3D 11、スワップチェーン、深度・ブレンド・ラスタライザーの状態、デバッグレイヤー
// ============================================================================


#include "Renderer.h"
#include "Application.h"
#include "CaptureMode.h"
#include <wrl/client.h>
#include <cstdlib>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "dxgi.lib")


using namespace DirectX::SimpleMath;

namespace
{
    // 画面を消すときの色（ほとんど黒の、わずかに青い夜の色）
    const Color ClearColor(0.003f, 0.005f, 0.008f, 1.0f);
}

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

	// --- スワップチェーン：フリップ方式で使うバックバッファの数（古いWindowsの BitBlt 方式では1枚）、望むリフレッシュレート
	constexpr UINT FlipBackBufferCount = 2;
	constexpr UINT BitBltBackBufferCount = 1;
	constexpr UINT PreferredRefreshRate = 60;
	// --- いつも使うサンプラー（異方性フィルタリングの倍率と、ピクセルシェーダーの番号 s0）
	constexpr UINT DefaultMaxAnisotropy = 4;
	constexpr UINT DefaultSamplerSlot = 0;
	// --- 定数バッファの番号（シェーダーの b0〜b7。common.hlsl と同じ）
	constexpr UINT WorldBufferSlot = 0;
	constexpr UINT ViewBufferSlot = 1;
	constexpr UINT ProjectionBufferSlot = 2;
	constexpr UINT LightBufferSlot = 3;
	constexpr UINT MaterialBufferSlot = 4;
	constexpr UINT TextureBufferSlot = 5;
	constexpr UINT DebugViewBufferSlot = 7;
	// --- ライトの初期値（Player が最初のフレームから上書きするので、作った直後の一瞬だけ使う）
	constexpr float InitialLightIntensity = 1.6f;
	constexpr float InitialLightRange = 260.0f;
	const Color InitialLightDiffuse(1.4f, 1.25f, 1.0f, 1.0f);
	const Color InitialLightAmbient(0.055f, 0.055f, 0.065f, 1.0f);
	constexpr float InitialLightInnerConeDegrees = 16.0f;
	constexpr float InitialLightOuterConeDegrees = 32.0f;
	constexpr float InitialLightConeFalloff = 1.35f;
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
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Renderer::m_pDepthShaderResourceView;
// スワップチェーンのバックバッファの描画先と、本描画のHDRの描き先（テクスチャ・描画先・読み取り）
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Renderer::m_pSwapChainRenderTargetView;
Microsoft::WRL::ComPtr<ID3D11Texture2D> Renderer::m_pSceneHdrTexture;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Renderer::m_pSceneHdrRenderTargetView;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Renderer::m_pSceneHdrShaderResourceView;

// ワールド・ビュー・射影の行列の定数バッファ（b0〜b2）
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pWorldBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pViewBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pProjectionBuffer;

// ライト（b3）・デバッグ表示（b7）・部屋の角の暗がり（b11）・マテリアル（b4）の定数バッファと、今のライト
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pLightBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pDebugViewBuffer;
DEBUG_VIEW_BUFFER Renderer::m_DebugView{ 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pRoomOcclusionBuffer;
ROOM_OCCLUSION_BUFFER Renderer::m_RoomOcclusion{};
unsigned int Renderer::m_RoomOcclusionVersion = 0;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pWaterCausticsBuffer;
WATER_CAUSTICS_BUFFER Renderer::m_WaterCaustics{};
bool Renderer::m_FigureOverride = false;
Vector3 Renderer::m_PuddleFigureOverride;
Vector3 Renderer::m_UnseenFigureOverride;
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pMaterialBuffer;
LIGHT Renderer::m_Light{};
// UVの行列の定数バッファ（b5）
Microsoft::WRL::ComPtr<ID3D11Buffer> Renderer::m_pTextureBuffer;
// 深度の書き込みあり・なしの状態
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateEnable;
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Renderer::m_pDepthStateDisable;

// ブレンドの状態（なし・半透明・加算・減算）
Microsoft::WRL::ComPtr<ID3D11BlendState>
	Renderer::m_pBlendState[MAX_BLENDSTATE];



//--------------------------------------------------------------------------------------
// 初期化処理：デバイス・スワップチェーン・描画の状態・定数バッファを、この順に作っている
//--------------------------------------------------------------------------------------
HRESULT Renderer::Init()
{
    HRESULT hr = CreateDeviceAndSwapChain();
    if (FAILED(hr)) return hr;

    // 描画先・深度バッファを作っている
    hr = CreateRenderAndDepthResources();
    if (FAILED(hr)) return hr;

    SetFullViewport();

    hr = CreatePipelineStates();
    if (FAILED(hr)) return hr;

    return CreateConstantBuffers();
}

//--------------------------------------------------------------------------------------
// デバイスとスワップチェーンを作り、ボーダーレス表示のまま Alt+Enter で全画面に切り替わらないようにしている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateDeviceAndSwapChain()
{
    // デバイスとスワップチェーンの設定
    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    // フリップ方式（FLIP_DISCARD）でバックバッファを2枚使っている。描いた画面を写さずに表示へ切り替えるため、
    // 垂直同期のときのフレームの間隔がそろう（BitBlt方式では、内蔵GPUの画面に単体GPUで描くノートPCで、30fps前後に落ちたり上下したりしていた）
    swapChainDesc.BufferCount = FlipBackBufferCount;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.BufferDesc.Width = Application::GetWidth(); // バッファの幅を描画解像度に合わせている
    swapChainDesc.BufferDesc.Height = Application::GetHeight(); // バッファの高さを描画解像度に合わせている
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 1画素を赤・緑・青・透明度の各8ビットにしている
    swapChainDesc.BufferDesc.RefreshRate.Numerator = PreferredRefreshRate; // リフレッシュレート
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画先として使う
    swapChainDesc.OutputWindow = Application::GetWindow(); // 表示するウィンドウ
    swapChainDesc.SampleDesc.Count = 1; // マルチサンプリング（アンチエイリアス）は使わない
    swapChainDesc.SampleDesc.Quality = 0; // 同上
    swapChainDesc.Windowed = TRUE; // ウィンドウモード（全画面に広げたボーダーレスのウィンドウで表示している）

    // Debug構成で、起動オプション --d3d-debug を付けたときだけ、D3D11のデバッグレイヤーを有効にして、使い方の間違いを出力させている。
    // デバッグレイヤーは描画の命令を1つずつ検査するため、CPU の時間が1フレーム1〜2.5ミリ秒増え、ときどき大きく重いフレームも出る
    // （120Hzの画面で120fpsを保てなくなる）。普段の Debug 版の起動では使わず、描画の間違いを調べるときだけ付けて起動する
    UINT deviceCreationFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    if (wcsstr(GetCommandLineW(), L"--d3d-debug") != nullptr)
    {
        deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
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

    HRESULT hr = createDevice(adapter.Get(), deviceCreationFlags);
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
    // フリップ方式を使えない古いWindows（8.1以前）では、これまでのBitBlt方式（バックバッファ1枚）で作り直している
    if (FAILED(hr))
    {
        swapChainDesc.BufferCount = BitBltBackBufferCount;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = createDevice(adapter.Get(), 0);
        if (FAILED(hr) && adapter != nullptr)
        {
            hr = createDevice(nullptr, 0);
        }
    }
    if (FAILED(hr)) return hr;

    // ボーダーレス表示を維持する。描画解像度は起動時に決め、最小化や復帰でバッファを作り直さない。
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    hr = m_pSwapChain->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return factory->MakeWindowAssociation(Application::GetWindow(), DXGI_MWA_NO_ALT_ENTER);
}

//--------------------------------------------------------------------------------------
// ビューポート（描く範囲）を描画解像度の全体にしている
//--------------------------------------------------------------------------------------
void Renderer::SetFullViewport()
{
    D3D11_VIEWPORT viewport{};
    viewport.Width = (FLOAT)Application::GetWidth();   // ビューポートの幅
    viewport.Height = (FLOAT)Application::GetHeight(); // ビューポートの高さ
    viewport.MinDepth = 0.0f;                          // 深度の範囲の最小値
    viewport.MaxDepth = 1.0f;                          // 深度の範囲の最大値
    viewport.TopLeftX = 0;                             // ビューポートの左上のX座標
    viewport.TopLeftY = 0;                             // ビューポートの左上のY座標
    m_pDeviceContext->RSSetViewports(1, &viewport);
}

//--------------------------------------------------------------------------------------
// 描画の状態（ラスタライザー・ブレンド・深度ステンシル・サンプラー）を作り、普段の設定にしている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreatePipelineStates()
{
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
    HRESULT hr = m_pDevice->CreateRasterizerState(&rasterizerDesc, rs.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_pDeviceContext->RSSetState(rs.Get());

    hr = CreateBlendStates();
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

    // サンプラーの状態：異方性フィルタリングで、UVが0〜1を超えたら繰り返している
    D3D11_SAMPLER_DESC smpDesc{};
    smpDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    smpDesc.MaxAnisotropy = DefaultMaxAnisotropy;
    smpDesc.MaxLOD = D3D11_FLOAT32_MAX;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;
    hr = m_pDevice->CreateSamplerState(&smpDesc, samplerState.GetAddressOf());
    if (FAILED(hr)) return hr;

    m_pDeviceContext->PSSetSamplers(DefaultSamplerSlot, 1, samplerState.GetAddressOf());
    return S_OK;
}

//--------------------------------------------------------------------------------------
// ブレンドの状態を作っている（重ねない・半透明・加算・減算。m_pBlendState の番号は BLEND_STATE と同じ）
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateBlendStates()
{
    // まずは重ねない設定
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
    HRESULT hr = m_pDevice->CreateBlendState(
        &BlendDesc, m_pBlendState[BS_NONE].ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    // 半透明の合成（上の設定でブレンドを有効にしたもの）
    BlendDesc.RenderTarget[0].BlendEnable = TRUE;
    hr = m_pDevice->CreateBlendState(
        &BlendDesc, m_pBlendState[BS_ALPHABLEND].ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    // 加算合成（下の色を減らさずに足す）
    BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    hr = m_pDevice->CreateBlendState(
        &BlendDesc, m_pBlendState[BS_ADDITIVE].ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    // 減算合成（下の色から引く）
    BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
    return m_pDevice->CreateBlendState(
        &BlendDesc, m_pBlendState[BS_SUBTRACTION].ReleaseAndGetAddressOf());
}

//--------------------------------------------------------------------------------------
// 定数バッファ（行列・ライト・デバッグ表示・部屋の形・水面の光・マテリアル・UV）を作ってシェーダーに設定し、初期値を入れている
//--------------------------------------------------------------------------------------
HRESULT Renderer::CreateConstantBuffers()
{
    // 行列の定数バッファを作り、頂点シェーダーのb0（ワールド）・b1（ビュー）・b2（射影）に設定している（ビューはピクセルシェーダーにも）
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(Matrix);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = sizeof(float);

    HRESULT hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pWorldBuffer.ReleaseAndGetAddressOf());
    m_pDeviceContext->VSSetConstantBuffers(WorldBufferSlot, 1, m_pWorldBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pViewBuffer.ReleaseAndGetAddressOf());
    m_pDeviceContext->VSSetConstantBuffers(ViewBufferSlot, 1, m_pViewBuffer.GetAddressOf());
    // ビュー行列はピクセルシェーダーのb1にも設定している（凹凸で傾けた法線・光の向きをビュー空間へ回すのに使う）
    m_pDeviceContext->PSSetConstantBuffers(ViewBufferSlot, 1, m_pViewBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pProjectionBuffer.ReleaseAndGetAddressOf());
    m_pDeviceContext->VSSetConstantBuffers(ProjectionBufferSlot, 1, m_pProjectionBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    // ライトの定数バッファを作り、頂点・ピクセルシェーダーのb3に設定している
    bufferDesc.ByteWidth = sizeof(LIGHT);
    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pLightBuffer.ReleaseAndGetAddressOf());
    m_pDeviceContext->VSSetConstantBuffers(LightBufferSlot, 1, m_pLightBuffer.GetAddressOf());
    m_pDeviceContext->PSSetConstantBuffers(LightBufferSlot, 1, m_pLightBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;
    // ライトの初期値（Playerが毎フレーム上書きしている）
    LIGHT light{};
    light.Enable = TRUE;
    light.FlashlightEnabled = TRUE;
    light.Intensity = InitialLightIntensity;
    light.Range = InitialLightRange;
    light.Direction = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    light.Diffuse = InitialLightDiffuse;
    light.Ambient = InitialLightAmbient;
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(InitialLightInnerConeDegrees)),
        cosf(DirectX::XMConvertToRadians(InitialLightOuterConeDegrees)), InitialLightConeFalloff, 0.0f);

    SetLight(light);

    // デバッグ表示と壁の古さの定数バッファを作り、ピクセルシェーダーのb7に設定している
    bufferDesc.ByteWidth = sizeof(DEBUG_VIEW_BUFFER);
    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pDebugViewBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;
    m_pDeviceContext->PSSetConstantBuffers(
        DebugViewBufferSlot, 1, m_pDebugViewBuffer.GetAddressOf());
    SetDebugViewMode(0);

    // 部屋の角の暗がり。最初は壁がない（暗がりなし）状態にしている。
    bufferDesc.ByteWidth = sizeof(ROOM_OCCLUSION_BUFFER);
    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pRoomOcclusionBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;
    SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);

    // 水面の光の揺らぎ。最初は浸水した範囲がない（効果なし）状態にしている。
    bufferDesc.ByteWidth = sizeof(WATER_CAUSTICS_BUFFER);
    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pWaterCausticsBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;
    SetWaterCaustics(Vector4(1.0f, 1.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f);

    // マテリアルの定数バッファを作り、頂点・ピクセルシェーダーのb4に設定している
    bufferDesc.ByteWidth = sizeof(MATERIAL);
    hr = m_pDevice->CreateBuffer(
        &bufferDesc, NULL, m_pMaterialBuffer.ReleaseAndGetAddressOf());
    m_pDeviceContext->VSSetConstantBuffers(MaterialBufferSlot, 1, m_pMaterialBuffer.GetAddressOf());
    m_pDeviceContext->PSSetConstantBuffers(MaterialBufferSlot, 1, m_pMaterialBuffer.GetAddressOf());
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
    m_pDeviceContext->VSSetConstantBuffers(TextureBufferSlot, 1, m_pTextureBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

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
		m_pSwapChainRenderTargetView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;
	m_pRenderTargetView = m_pSwapChainRenderTargetView;

	// 本描画のHDRの描き先。明るさを1で切らずに残し、ブルームとトーンマッピング（ACES）で明るい所をなだらかに丸める
	D3D11_TEXTURE2D_DESC hdrDesc{};
	hdrDesc.Width = Application::GetWidth();
	hdrDesc.Height = Application::GetHeight();
	hdrDesc.MipLevels = 1;
	hdrDesc.ArraySize = 1;
	hdrDesc.Format = SceneHdrFormat;
	hdrDesc.SampleDesc.Count = 1;
	hdrDesc.Usage = D3D11_USAGE_DEFAULT;
	hdrDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	hr = m_pDevice->CreateTexture2D(&hdrDesc, nullptr, m_pSceneHdrTexture.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	hr = m_pDevice->CreateRenderTargetView(
		m_pSceneHdrTexture.Get(), nullptr, m_pSceneHdrRenderTargetView.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;
	hr = m_pDevice->CreateShaderResourceView(
		m_pSceneHdrTexture.Get(), nullptr, m_pSceneHdrShaderResourceView.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return hr;

	// 深度ステンシルバッファを作っている
	// （深度バッファ＝Zバッファ。奥行きを判定して、前後関係を正しく描けるようにしている）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = Application::GetWidth();   // バッファの幅を描画解像度に合わせている
	textureDesc.Height = Application::GetHeight(); // バッファの高さを描画解像度に合わせている
	textureDesc.MipLevels = 1;                            // ミップマップは使わない
	textureDesc.ArraySize = 1;                            // 配列ではない1枚のテクスチャ
	textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;           // 32ビット。書き込みは深度（D32_FLOAT）、読み取りは浮動小数点（R32_FLOAT）として使うため型なしにしている
	textureDesc.SampleDesc.Count = 1;                     // スワップチェーンと同じサンプルの設定
	textureDesc.SampleDesc.Quality = 0;                   // 同上
	textureDesc.Usage = D3D11_USAGE_DEFAULT;              // GPUだけで使う
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE; // 深度バッファとして使い、タイルベースライティングのCompute Shaderからも読んでいる
	textureDesc.CPUAccessFlags = 0;                       // CPUからは読み書きしない
	textureDesc.MiscFlags = 0;                            // その他のフラグはなし
    hr = m_pDevice->CreateTexture2D(
        &textureDesc,
        NULL,
        depthStencil.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

	// 深度ステンシルビューを作っている
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT; // 深度として書き込むときの形式
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャ用のビューにしている
	depthStencilViewDesc.Flags = 0; // 特別なフラグはなし
    hr = m_pDevice->CreateDepthStencilView(
        depthStencil.Get(),
        &depthStencilViewDesc,
		m_pDepthStencilView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

	// 深度を読み取るビュー（タイルごとの一番奥の深度を求めるCompute Shaderが使う）
	D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDesc{};
	depthResourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	depthResourceDesc.Texture2D.MipLevels = 1;
    hr = m_pDevice->CreateShaderResourceView(
        depthStencil.Get(),
        &depthResourceDesc,
		m_pDepthShaderResourceView.ReleaseAndGetAddressOf());
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
	m_pRoomOcclusionBuffer.Reset();
	m_pWaterCausticsBuffer.Reset();
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
	m_pDepthShaderResourceView.Reset();
	m_pRenderTargetView.Reset();
	m_pSwapChainRenderTargetView.Reset();
	m_pSceneHdrShaderResourceView.Reset();
	m_pSceneHdrRenderTargetView.Reset();
	m_pSceneHdrTexture.Reset();
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
	float clearColor[4] = { ClearColor.R(), ClearColor.G(), ClearColor.B(), ClearColor.A() }; // 背景色（わずかに青みのある黒）

	// 本描画はHDRの描き先へ描く（画面効果の最後のトーンマッピングで、バックバッファへ移す）
	BeginHdrScene();
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
		else
		{
			Tools::CaptureMode::Abort("撮影・計測用の画面を取得できません。");
			return;
		}
	}

	// 計測モード（--benchmark）は垂直同期を待たず、本来の処理時間を測れるようにしている（--vsync を付けたときは待つ）。
	if (Tools::CaptureMode::IsBenchmark())
	{
		const auto presentStart = std::chrono::steady_clock::now();
		const HRESULT presentResult = m_pSwapChain->Present(Tools::CaptureMode::UsesBenchmarkVsync() ? 1 : 0, 0);
		if (FAILED(presentResult))
		{
			Tools::CaptureMode::Abort("計測中に画面を表示できません。");
			return;
		}
		Tools::CaptureMode::OnPresentTimed(std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - presentStart).count());
		return;
	}
	// 通常プレイの周期はApplicationで決める。Presentでは垂直同期を待たない。
	// 自動撮影の動画時間はCaptureModeが管理する。
	const HRESULT presentResult = m_pSwapChain->Present(Tools::CaptureMode::IsActive() ? 1 : 0, 0);
	if (Tools::CaptureMode::IsActive() && FAILED(presentResult))
	{
		Tools::CaptureMode::Abort("撮影中に画面を表示できません。");
		return;
	}

	// GPUのドライバーが再起動した（デバイスの消失）ときは、そのまま描き続けても画面が止まったままになる。
	// 原因の候補を一度だけ伝えて、ゲームを終わらせている。
	if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET)
	{
		static bool reported = false;
		if (!reported)
		{
			reported = true;
			MessageBoxW(nullptr,
				L"グラフィックドライバーが応答しなくなったため、ゲームを終了します。\n"
				L"ドライバーを更新するか、PCを再起動してからもう一度起動してください。",
				L"描画エラー", MB_OK | MB_ICONERROR);
			PostQuitMessage(EXIT_FAILURE);
		}
	}
}

// Rendererのデバイスまわりの処理はここまで（描画の状態の設定はRendererState.cpp、資源の作成はRendererResources.cpp）。
