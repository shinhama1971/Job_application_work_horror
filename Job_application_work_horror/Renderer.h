// ============================================================================
// ファイルの役割: Direct3D 11のデバイス、描画の状態、ライト、各種の定数バッファを管理している。
// 主な技術: Direct3D 11、スワップチェーン、深度・ブレンド・ラスタライザーの状態、デバッグレイヤー
// このヘッダーでは、頂点・ライト・マテリアルなどシェーダーと共有する型と、描画の共通の操作を宣言している。
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

// 外部ライブラリをリンクしている
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")


// 3Dの頂点データ（位置・法線・色・テクスチャ座標）。全シェーダーで同じ並びを使っている
struct VERTEX_3D
{
	DirectX::SimpleMath::Vector3 position;
	DirectX::SimpleMath::Vector3 normal;
	DirectX::SimpleMath::Color color;
	DirectX::SimpleMath::Vector2 uv;
};

// ブレンドステート（色の重ね方）
enum EBlendState {
	BS_NONE = 0,							// 半透明の合成なし
	BS_ALPHABLEND,							// 半透明の合成（アルファブレンド）
	BS_ADDITIVE,							// 加算合成
	BS_SUBTRACTION,							// 減算合成
	MAX_BLENDSTATE
};

// 懐中電灯（カメラの位置から出るスポットライト）と環境光。定数バッファb3でシェーダーへ渡している。
struct LIGHT
{
	// ライトの計算をするか
	BOOL Enable;
	BOOL FlashlightEnabled;		// FALSEなら懐中電灯の光を計算せず、環境光だけにしている
	float Intensity;			// 懐中電灯の明るさ（電池の残りやちらつきで毎フレーム変わる）
	float Range;				// 懐中電灯の光が届く距離
	DirectX::SimpleMath::Vector4 Direction;	// ビュー空間での光の向き（手ぶれで少し揺れる）
	DirectX::SimpleMath::Color Diffuse;		// 懐中電灯の光の色
	DirectX::SimpleMath::Color Ambient;		// 環境光（光が当たらない場所の最低限の明るさ）
	DirectX::SimpleMath::Vector4 SpotParams; // x=内側の円錐のcos、y=外側の円錐のcos、z=縁のぼかし指数、w=未使用
};

// HLSLの定数バッファと同じ大きさになっているかを、コンパイル時に確かめている
static_assert(sizeof(LIGHT) == 80, "LIGHT must match the HLSL constant-buffer layout");

// 点光源1個分。StructuredBufferでシェーダーへ渡すため、common.hlslのENVIRONMENT_POINT_LIGHTと同じ並びにしている。
// PositionRange.w は影響の半径で、この距離で明るさが0になるようにシェーダー側で弱めている。
struct ENVIRONMENT_POINT_LIGHT
{
	// xyz=位置、w=影響の半径
	DirectX::SimpleMath::Vector4 PositionRange;
	// xyz=色、w=強さ
	DirectX::SimpleMath::Vector4 ColorIntensity;
};

static_assert(sizeof(ENVIRONMENT_POINT_LIGHT) == 32,
	"Environment point light must match HLSL layout");

// デバッグ表示と壁の古さの定数バッファ（b7）
struct DEBUG_VIEW_BUFFER
{
    // デバッグ表示の番号（0は普段の画面）、2面の壁の湿り気の強さ
    int Mode;
    float WallDampStrength;
    // 壁の古さ（0〜1）。パネルの継ぎ目・穴・ひび・水の垂れた跡・床際の水位線・カビの濃さ。
    // 面ごとに雰囲気を変えるため、各SceneのInitで設定している（1面は1、2面は0で前と同じ見た目）。
    float WallWeathering;
    // 16バイトにそろえるための詰め物
    float Padding;
};

static_assert(sizeof(DEBUG_VIEW_BUFFER) == 16, "Debug view buffer must be 16 bytes");

// 部屋の角の暗がり（shader/roomOcclusion.hlsli の RoomOcclusionBuffer、b11）と同じ並びにしている。
struct ROOM_OCCLUSION_BUFFER
{
    // 壁の数の上限
    static constexpr unsigned int MaxBoxes = 32;
    // 壁を上から見た長方形。x,y = 中心のx・z、z,w = 幅と奥行きの半分。
    DirectX::SimpleMath::Vector4 Boxes[MaxBoxes];
    unsigned int BoxCount;  // 0なら暗がりを付けない
    // 床と天井の高さ、暗がりの強さ
    float FloorY;
    float CeilingY;
    float Strength;
};

static_assert(sizeof(ROOM_OCCLUSION_BUFFER) == 16 * 33, "Room occlusion buffer must match HLSL layout");

// サブセット（モデルの中で、同じマテリアルで描く部分）
struct SUBSET{
	// マテリアルの名前、インデックスの数、頂点の数、インデックスと頂点の開始位置、マテリアルの番号
	std::string MtrlName;
	unsigned int IndexNum = 0;
	unsigned int VertexNum = 0;
	unsigned int IndexBase = 0;
	unsigned int VertexBase = 0;
	unsigned int MaterialIdx= 0;
};

// マテリアル。定数バッファb4でシェーダーへ渡している（Emissionは自分で光る色、TextureEnableがFALSEならテクスチャを使わない）。
struct MATERIAL
{
	DirectX::SimpleMath::Color Ambient;
	DirectX::SimpleMath::Color Diffuse;
	DirectX::SimpleMath::Color Specular;
	DirectX::SimpleMath::Color Emission;
	float Shininess;
	BOOL TextureEnable;
	// 建物の壁（構造の壁）ならTRUE。壁の古さ（パネルの継ぎ目・ひびなど）はこの面だけに描いている。
	// 棚や配管などの小物に継ぎ目が出ないようにするための印。
	BOOL WeatheringSurface;
	// 16バイトにそろえるための詰め物
	BOOL Dummy;


};
//-----------------------------------------------------------------------------
// Direct3D 11のデバイス・スワップチェーン・共通の描画の状態と、全シェーダー共通の定数バッファ
// （行列b0〜b2、ライトb3、マテリアルb4、UV b5、デバッグ表示b7、部屋の角の暗がりb11）を管理する静的クラス。
//-----------------------------------------------------------------------------
class Renderer
{
private:

	// 使えたDirect3Dの機能レベル
	static D3D_FEATURE_LEVEL       m_FeatureLevel;

	// デバイス、デバイスコンテキスト、スワップチェーン、バックバッファの描画先、深度バッファ
	static Microsoft::WRL::ComPtr<ID3D11Device> m_pDevice;
	static Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pDeviceContext;
	static Microsoft::WRL::ComPtr<IDXGISwapChain> m_pSwapChain;
	static Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;
	static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pDepthShaderResourceView;

	// ワールド・ビュー・射影の行列の定数バッファ
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pWorldBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pViewBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pProjectionBuffer;

	// ライトとデバッグ表示の定数バッファ
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pLightBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pDebugViewBuffer;
	// デバッグ表示と壁の古さは同じ定数バッファにあるため、片方だけ変えるときのために中身を覚えている。
	static DEBUG_VIEW_BUFFER m_DebugView;
	// 覚えている中身を定数バッファへ送っている
	static void UploadDebugViewBuffer();
	// 部屋の角の暗がりとマテリアルの定数バッファ、今のライト、ライトの計算をするか
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pRoomOcclusionBuffer;
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pMaterialBuffer;
	static LIGHT m_Light;
	static bool m_LightEnable;

	// UVの行列の定数バッファ
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pTextureBuffer;

	// 深度の書き込みあり・なしの状態
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_pDepthStateEnable;
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_pDepthStateDisable;

	// ブレンドの状態（EBlendStateの数だけ）
	static Microsoft::WRL::ComPtr<ID3D11BlendState>
		m_pBlendState[MAX_BLENDSTATE];

	// バックバッファの描画先と深度バッファを作っている
	static HRESULT CreateRenderAndDepthResources();

public:

	// デバイスと共通の資源を作る／解放する
	static HRESULT Init();
	static void Uninit();
	// 描画に使う高性能なGPUが内蔵GPU（専用のビデオメモリをほとんど持たないもの）ならtrueを返している。
	// 描画解像度の「自動」を決めるため、Initより前（ウィンドウを作る前）に呼べるようにしている。
	static bool IsHighPerformanceAdapterIntegrated();
	// バックバッファと深度バッファを消し、本描画の描画先にしている。
	static void DrawStart();
	// 描いた画面を表示している（垂直同期あり。計測モードは垂直同期なし）。
	static void DrawEnd();

	// ウィンドウの大きさが変わったときに、バックバッファと深度バッファを作り直している。
	// バッファは描画解像度のままにし、ウィンドウへの引き伸ばしは表示（Present）に任せている。
	static HRESULT ResizeWindow(int width, int height);

	// falseでも深度テストは続け、深度の書き込みだけを止めている（半透明・加算合成の描画用）。
	static void SetDepthEnable(bool Enable);

	// HUDなどの2D描画用に、左上を原点とする画素単位の行列を設定している。
	static void SetWorldViewProjection2D();
	// 幅width・高さheightの座標系を、画面全体に対応させている（HUDのキャンバス用）。
	static void SetWorldViewProjection2D(float width, float height);
	// 行列はシェーダーの mul(v, M) に合わせて、転置してからGPUへ送っている。
	static void SetWorldMatrix(DirectX::SimpleMath::Matrix* WorldMatrix);
	static void SetViewMatrix(DirectX::SimpleMath::Matrix* ViewMatrix);
	static void SetProjectionMatrix(DirectX::SimpleMath::Matrix* ProjectionMatrix);

	// デバイスとデバイスコンテキストを返している
	static ID3D11Device* GetDevice( void ){ return m_pDevice.Get(); }
	static ID3D11DeviceContext* GetDeviceContext( void ){ return m_pDeviceContext.Get(); }

	// 同じ名前の.csoがあればそれを読み、なければ.hlslを実行時にコンパイルしている。
	static HRESULT CompileShader(
		const char* szFileName,
		LPCSTR szEntryPoint,
		LPCSTR szShaderModel,
		std::vector<unsigned char>& shaderObject);
	// 頂点シェーダー（と入力レイアウト）、ピクセルシェーダーを作っている
	static HRESULT CreateVertexShader(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppVertexLayout, D3D11_INPUT_ELEMENT_DESC* pLayout, unsigned int numElements, const char* szFileName);
	static HRESULT CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);

	// インデックスバッファと、書き換えない頂点バッファを作っている
	static bool CreateIndexBuffer(unsigned int indexnum, void* indexdata, ID3D11Buffer** pIndexBuffer);
	static bool CreateVertexBuffer(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);
	// CPUから毎フレーム書き換える頂点バッファ（DYNAMIC）を作っている。
	static bool CreateVertexBufferWrite(unsigned int stride, unsigned int vertexnum, void* vertexdata, ID3D11Buffer** pVertexBuffer);

	// 定数バッファを作っている（UpdateSubresourceで書き換える）
	static bool CreateConstantBuffer(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);
	// CPUからMapで書き換える定数バッファ（DYNAMIC）を作っている。
	static bool CreateConstantBufferWrite(unsigned int bytesize, ID3D11Buffer** pConstantBuffer);

	// 懐中電灯と環境光を設定している。Enableは SetLightEnable の状態を優先している。
	static void SetLight(LIGHT Light);
	static LIGHT GetLight() { return m_Light; }
	// デバッグ用のシェーダーの表示（法線・光源のタイルなど）を切り替えている。0で普段の表示。
	static void SetDebugViewMode(
		int mode, float wallDampStrength = 1.0f);
	// 壁の古さ（0〜1）を設定している。0なら前と同じ見た目のまま。
	static void SetWallWeathering(float weathering);
	// 部屋の角の暗がりに使う壁の形と、床・天井の高さを設定している。boxCount が0なら暗がりを付けない。
	static void SetRoomOcclusion(
		const DirectX::SimpleMath::Vector4* boxes, unsigned int boxCount,
		float floorY, float ceilingY, float strength);
	// ライトの計算をするかを切り替える・返している
	static void SetLightEnable(bool Enable);
	static bool GetLightEnable();
	// マテリアルを直接設定している
	static void SetMaterial(MATERIAL Material);
	// テクスチャのUVを拡大・移動する行列を設定している（スプライトの切り出し用）。
	static void SetUV(float u, float v, float uw, float vh);

	// バックバッファの描画先と深度バッファを返している
	static ID3D11RenderTargetView* GetBackBufferRTV()
	{
		return m_pRenderTargetView.Get();
	}

	static ID3D11DepthStencilView* GetDepthStencilView()
	{
		return m_pDepthStencilView.Get();
	}

	// 深度バッファを読み取るビュー（深度バッファとして設定している間は読めないため、外してから使う）
	static ID3D11ShaderResourceView* GetDepthShaderResourceView()
	{
		return m_pDepthShaderResourceView.Get();
	}

	// 画面以外へ描いた後、描画先をバックバッファへ戻している。
	static void SetBackBufferRenderTarget();

	// バックバッファを指定の色で消している
	static void ClearBackBuffer(float r, float g, float b, float a);

	// 深度バッファを消している
	static void ClearDepth();

	// 合成のしかたを切り替えている（EBlendState: なし・半透明・加算・減算）。
	static void SetBlendState(int nBlendState)
	{
		if (nBlendState >= 0 && nBlendState < MAX_BLENDSTATE) {
			float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			m_pDeviceContext->OMSetBlendState(
				m_pBlendState[nBlendState].Get(), blendFactor, 0xffffffff);
		}
	}
	
};
