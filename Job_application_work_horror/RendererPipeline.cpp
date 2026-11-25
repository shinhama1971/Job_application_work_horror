// ============================================================================
// ファイルの役割: 描画の準備のうち、ラスタライザー・サンプラー・深度の状態、ブレンドの状態、定数バッファ、描画先と深度のテクスチャを作っている。
// （Renderer.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "Renderer.h"
#include "Application.h"
#include "CaptureMode.h"
#include <wrl/client.h>
#include <cstdlib>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>

using namespace DirectX::SimpleMath;

namespace
{
	// --- いつも使うサンプラー（異方性フィルタリングの倍率と、ピクセルシェーダーの番号 s0）
	constexpr UINT DefaultMaxAnisotropy = 4;
	constexpr UINT DefaultSamplerSlot = 0;
	// --- 定数バッファの番号（シェーダーの b0〜b7。common.hlsl と同じ）
	constexpr UINT WorldBufferSlot = SIGNAL_LOST_WORLD_BUFFER_SLOT;
	constexpr UINT ViewBufferSlot = SIGNAL_LOST_VIEW_BUFFER_SLOT;
	constexpr UINT ProjectionBufferSlot = SIGNAL_LOST_PROJECTION_BUFFER_SLOT;
	constexpr UINT LightBufferSlot = SIGNAL_LOST_LIGHT_BUFFER_SLOT;
	constexpr UINT MaterialBufferSlot = SIGNAL_LOST_MATERIAL_BUFFER_SLOT;
	constexpr UINT TextureBufferSlot = SIGNAL_LOST_TEXTURE_TRANSFORM_BUFFER_SLOT;
	constexpr UINT DebugViewBufferSlot = SIGNAL_LOST_DEBUG_VIEW_BUFFER_SLOT;
	// --- ライトの初期値（Player が最初のフレームから上書きするので、作った直後の一瞬だけ使う）
	constexpr float InitialLightIntensity = 1.6f;
	constexpr float InitialLightRange = 260.0f;
	const Color InitialLightDiffuse(1.4f, 1.25f, 1.0f, 1.0f);
	const Color InitialLightAmbient(0.055f, 0.055f, 0.065f, 1.0f);
	constexpr float InitialLightInnerConeDegrees = 16.0f;
	constexpr float InitialLightOuterConeDegrees = 32.0f;
	constexpr float InitialLightConeFalloff = 1.35f;
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
    SetDebugViewMode(ShaderDebugView::Final);

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
