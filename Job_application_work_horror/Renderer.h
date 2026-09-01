// ============================================================================
// ファイルの役割: Direct3D 11デバイス、描画状態、ライト、各種定数バッファを管理します。
// このヘッダーでは外部へ公開する型・状態・操作を宣言します。
// ============================================================================

#pragma once
#define _CRT_SECURE_NO_WARNINGS
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

// Direct3D解放の簡略化マクロ
#define SAFE_RELEASE(p) { if( NULL != p ) { p->Release(); p = NULL; } }

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

//平行光源
struct LIGHT
{
	BOOL Enable;
	BOOL FlashlightEnabled;
	float Intensity;
	float Range;
	DirectX::SimpleMath::Vector4 Direction;
	DirectX::SimpleMath::Color Diffuse;
	DirectX::SimpleMath::Color Ambient;
	DirectX::SimpleMath::Vector4 SpotParams; // inner cosine, outer cosine, edge exponent, unused
};

static_assert(sizeof(LIGHT) == 80, "LIGHT must match the HLSL constant-buffer layout");

static constexpr int MAX_ENVIRONMENT_LIGHTS = 8;

struct ENVIRONMENT_POINT_LIGHT
{
	DirectX::SimpleMath::Vector4 PositionRange;
	DirectX::SimpleMath::Vector4 ColorIntensity;
};

struct ENVIRONMENT_LIGHTS
{
	ENVIRONMENT_POINT_LIGHT Lights[MAX_ENVIRONMENT_LIGHTS];
	int Count;
	float Padding[3];
};

static_assert(sizeof(ENVIRONMENT_POINT_LIGHT) == 32,
	"Environment point light must match HLSL layout");
static_assert(sizeof(ENVIRONMENT_LIGHTS) == 272,
	"Environment light buffer must match HLSL layout");

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

//マテリアル
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
//Rendererクラス
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
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pEnvironmentLightBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pDebugViewBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pMaterialBuffer;
	static LIGHT m_Light;
	static ENVIRONMENT_LIGHTS m_EnvironmentLights;
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
	static void DrawStart();
	static void DrawEnd();

	static HRESULT ResizeWindow(int width, int height);
		
	static void SetDepthEnable(bool Enable);

	static void SetATCEnable(bool Enable);

	static void SetWorldViewProjection2D();
	static void SetWorldMatrix(DirectX::SimpleMath::Matrix* WorldMatrix);
	static void SetViewMatrix(DirectX::SimpleMath::Matrix* ViewMatrix);
	static void SetProjectionMatrix(DirectX::SimpleMath::Matrix* ProjectionMatrix);

	static ID3D11Device* GetDevice( void ){ return m_pDevice.Get(); }
	static ID3D11DeviceContext* GetDeviceContext( void ){ return m_pDeviceContext.Get(); }

	static HRESULT CompileShader(
		const char* szFileName,
		LPCSTR szEntryPoint,
		LPCSTR szShaderModel,
		std::vector<unsigned char>& shaderObject);
	static HRESULT CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName);
	static HRESULT CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);

	static bool CreateIndexBuffer(unsigned int indexnum, void* indexdata, ID3D11Buffer** pIndexBuffer);
	static bool CreateVertexBuffer(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);
	static bool CreateVertexBufferWrite(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);

	static bool CreateConstantBuffer(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);
	static bool CreateConstantBufferWrite(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);

	static void SetLight(LIGHT Light);
	static void SetEnvironmentLights(const ENVIRONMENT_LIGHTS& lights);
	static void SetDebugViewMode(
		int mode, float wallDampStrength = 1.0f);
	static void SetLightEnable(bool Enable);
	static bool GetLightEnable();
	static void SetMaterial(MATERIAL Material);
	static void SetUV(float u, float v, float uw, float vh);

	static ID3D11RenderTargetView* GetBackBufferRTV()
	{
		return m_pRenderTargetView.Get();
	}

	static ID3D11DepthStencilView* GetDepthStencilView()
	{
		return m_pDepthStencilView.Get();
	}

	static void SetBackBufferRenderTarget();

	static void ClearBackBuffer(float r, float g, float b, float a);

	static void ClearDepth();

	//=============================================================================
	// ブレンド ステート設定
	//=============================================================================
	static void SetBlendState(int nBlendState)
	{
		if (nBlendState >= 0 && nBlendState < MAX_BLENDSTATE) {
			float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			m_pDeviceContext->OMSetBlendState(
				m_pBlendState[nBlendState].Get(), blendFactor, 0xffffffff);
		}
	}
	
};
