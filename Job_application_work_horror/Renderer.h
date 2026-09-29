// ============================================================================
// ファイルの役割: Direct3D 11デバイス、描画状態、ライト、各種定数バッファを管理します。
// 主な技術: Direct3D 11、Swap Chain、深度・ブレンド・ラスタライザ状態、Debug Layer
// このヘッダーでは外部へ公開する型・状態・操作を宣言します。
// ============================================================================

#pragma once
#include	<d3d11.h>
#include	<DirectXMath.h>
#include	<SimpleMath.h>
#include	<io.h>
#include	<string>
#include	<vector>
#include	<d3dcompiler.h>
#include	<locale.h>
#include	<wrl/client.h>

//外部ライブラリ
#pragma comment(lib,"directxtk.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")


// ３Ｄ頂点データ
struct VERTEX_3D
{
	DirectX::SimpleMath::Vector3 position;
	DirectX::SimpleMath::Vector3 normal;
	DirectX::SimpleMath::Color color;
	DirectX::SimpleMath::Vector2 uv;
};

// ブレンドステート
enum EBlendState {
	BS_NONE = 0,							// 半透明合成無し
	BS_ALPHABLEND,							// 半透明合成
	BS_ADDITIVE,							// 加算合成
	BS_SUBTRACTION,							// 減算合成
	MAX_BLENDSTATE
};

// 懐中電灯（カメラ位置から出るスポットライト）と環境光。定数バッファb3でシェーダーへ渡します。
struct LIGHT
{
	BOOL Enable;
	BOOL FlashlightEnabled;		// FALSEなら懐中電灯の光を計算せず、環境光だけにします
	float Intensity;			// 懐中電灯の明るさ（電池残量やちらつきで毎フレーム変わります）
	float Range;				// 懐中電灯の光が届く距離
	DirectX::SimpleMath::Vector4 Direction;	// ビュー空間での光の向き（手ぶれで少し揺れます）
	DirectX::SimpleMath::Color Diffuse;		// 懐中電灯の光の色
	DirectX::SimpleMath::Color Ambient;		// 環境光（光が当たらない場所の最低限の明るさ）
	DirectX::SimpleMath::Vector4 SpotParams; // x=内側の円錐のcos、y=外側の円錐のcos、z=縁のぼかし指数、w=未使用
};

static_assert(sizeof(LIGHT) == 80, "LIGHT must match the HLSL constant-buffer layout");

// 点光源1個分。StructuredBufferでシェーダーへ渡すため、common.hlslのENVIRONMENT_POINT_LIGHTと同じ並びにします。
// PositionRange.w は影響半径で、この距離で明るさが0になるようにシェーダー側で減衰させます。
struct ENVIRONMENT_POINT_LIGHT
{
	DirectX::SimpleMath::Vector4 PositionRange;
	DirectX::SimpleMath::Vector4 ColorIntensity;
};

static_assert(sizeof(ENVIRONMENT_POINT_LIGHT) == 32,
	"Environment point light must match HLSL layout");

struct DEBUG_VIEW_BUFFER
{
    int Mode;
    float WallDampStrength;
    float Padding[2];
};

static_assert(sizeof(DEBUG_VIEW_BUFFER) == 16, "Debug view buffer must be 16 bytes");

//サブセット
struct SUBSET{
	std::string MtrlName;
	unsigned int IndexNum = 0;
	unsigned int VertexNum = 0;
	unsigned int IndexBase = 0;
	unsigned int VertexBase = 0;
	unsigned int MaterialIdx= 0;
};

// 材質。定数バッファb4でシェーダーへ渡します（Emissionは発光色、TextureEnableがFALSEならテクスチャを使いません）。
struct MATERIAL
{
	DirectX::SimpleMath::Color Ambient;
	DirectX::SimpleMath::Color Diffuse;
	DirectX::SimpleMath::Color Specular;
	DirectX::SimpleMath::Color Emission;
	float Shininess;
	BOOL TextureEnable;
	BOOL Dummy[2];


};
//-----------------------------------------------------------------------------
// Direct3D 11のデバイス・スワップチェイン・共通の描画状態と、全シェーダー共通の定数バッファ
// （行列b0〜b2、ライトb3、材質b4、UV b5、デバッグ表示b7）を管理する静的クラスです。
//-----------------------------------------------------------------------------
class Renderer
{
private:

	static D3D_FEATURE_LEVEL       m_FeatureLevel;

	static Microsoft::WRL::ComPtr<ID3D11Device> m_pDevice;
	static Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pDeviceContext;
	static Microsoft::WRL::ComPtr<IDXGISwapChain> m_pSwapChain;
	static Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;

	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pWorldBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pViewBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pProjectionBuffer;

	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pLightBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pDebugViewBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pMaterialBuffer;
	static LIGHT m_Light;
	static bool m_LightEnable;

	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pTextureBuffer;

	static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_pDepthStateEnable;
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_pDepthStateDisable;

	static Microsoft::WRL::ComPtr<ID3D11BlendState>
		m_pBlendState[MAX_BLENDSTATE];
	static Microsoft::WRL::ComPtr<ID3D11BlendState> m_pBlendStateATC;

	static HRESULT CreateRenderAndDepthResources();

public:

	static HRESULT Init();
	static void Uninit();
	// バックバッファと深度バッファをクリアし、本描画の描画先にします。
	static void DrawStart();
	// 描いた画面を表示します（垂直同期あり）。
	static void DrawEnd();

	// ウィンドウの大きさに合わせてバックバッファと深度バッファを作り直します。
	static HRESULT ResizeWindow(int width, int height);

	// falseでも深度テストは続け、深度の書き込みだけを止めます（半透明・加算合成の描画用）。
	static void SetDepthEnable(bool Enable);

	// アルファ・トゥ・カバレッジ用。現在はどこからも呼ばれておらず、専用のブレンドステートも作成していません。
	static void SetATCEnable(bool Enable);

	// HUDなどの2D描画用に、左上原点・ピクセル単位の行列を設定します。
	static void SetWorldViewProjection2D();
	// 行列はシェーダーの mul(v, M) に合わせて、転置してからGPUへ送ります。
	static void SetWorldMatrix(DirectX::SimpleMath::Matrix* WorldMatrix);
	static void SetViewMatrix(DirectX::SimpleMath::Matrix* ViewMatrix);
	static void SetProjectionMatrix(DirectX::SimpleMath::Matrix* ProjectionMatrix);

	static ID3D11Device* GetDevice( void ){ return m_pDevice.Get(); }
	static ID3D11DeviceContext* GetDeviceContext( void ){ return m_pDeviceContext.Get(); }

	// 同じ名前の.csoがあればそれを読み、なければ.hlslを実行時にコンパイルします。
	static HRESULT CompileShader(
		const char* szFileName,
		LPCSTR szEntryPoint,
		LPCSTR szShaderModel,
		std::vector<unsigned char>& shaderObject);
	static HRESULT CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName);
	static HRESULT CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);

	static bool CreateIndexBuffer(unsigned int indexnum, void* indexdata, ID3D11Buffer** pIndexBuffer);
	static bool CreateVertexBuffer(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);
	// CPUから毎フレーム書き換える頂点バッファ（DYNAMIC）を作ります。
	static bool CreateVertexBufferWrite(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);

	static bool CreateConstantBuffer(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);
	// CPUからMapで書き換える定数バッファ（DYNAMIC）を作ります。
	static bool CreateConstantBufferWrite(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);

	// 懐中電灯と環境光を設定します。Enableは SetLightEnable の状態が優先されます。
	static void SetLight(LIGHT Light);
	static void SetPointLight(LIGHT Light);
	static LIGHT GetLight() { return m_Light; }
	// デバッグ用のシェーダー表示（法線・光源タイルなど）を切り替えます。0で通常表示です。
	static void SetDebugViewMode(
		int mode, float wallDampStrength = 1.0f);
	static void SetLightEnable(bool Enable);
	static bool GetLightEnable();
	static void SetMaterial(MATERIAL Material);
	// テクスチャのUVを拡大・移動する行列を設定します（スプライトの切り出し用）。
	static void SetUV(float u, float v, float uw, float vh);

	static ID3D11RenderTargetView* GetBackBufferRTV()
	{
		return m_pRenderTargetView.Get();
	}

	static ID3D11DepthStencilView* GetDepthStencilView()
	{
		return m_pDepthStencilView.Get();
	}

	// オフスクリーン描画の後、描画先をバックバッファへ戻します。
	static void SetBackBufferRenderTarget();

	static void ClearBackBuffer(float r, float g, float b, float a);

	static void ClearDepth();

	// 合成方法を切り替えます（EBlendState: なし・半透明・加算・減算）。
	static void SetBlendState(int nBlendState)
	{
		if (nBlendState >= 0 && nBlendState < MAX_BLENDSTATE) {
			float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			m_pDeviceContext->OMSetBlendState(
				m_pBlendState[nBlendState].Get(), blendFactor, 0xffffffff);
		}
	}
	
};
