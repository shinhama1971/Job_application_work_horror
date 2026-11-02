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
#include	<filesystem>
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
    // 面ごとに雰囲気を変えるため、各SceneのInitで設定している。
    float WallWeathering;
    // 壁の古さに混ぜるホラーの跡（血の手形・引っかき傷・血の垂れ跡）の濃さ（0〜1）。
    // 2面は異変として手形や傷を出すため、決まった跡は描かない（0）。
    float HorrorMarksStrength;
    // 2面の周回の深さ（0〜1）。周回が進むほど、廊下の色調が赤黒く沈み、霧が濃くなる（shader/horrorGrade.hlsli）
    float LoopDepth;
    float LoopDepthPadding[3];
};

static_assert(sizeof(DEBUG_VIEW_BUFFER) == 32, "Debug view buffer must be 32 bytes");

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

// 水面の光の揺らぎ（コースティクス）の定数バッファ（shader/waterCaustics.hlsliのWaterCausticsBuffer、b12と同じ並び）
struct WATER_CAUSTICS_BUFFER
{
    // プレイヤーの視点のビュー行列（転置済み）。懐中電灯はこの視点から照らしているため、反射を描くときもこの行列を使う
    DirectX::SimpleMath::Matrix View;
    // 床一面が水に浸かった範囲。x,y = x・zの最小、z,w = x・zの最大（範囲がないときは最小を最大より大きくしている）
    DirectX::SimpleMath::Vector4 FloodRect;
    float WaterY;    // 水面の高さ
    float Time;      // 揺らぎを動かす時間（秒）
    float Strength;  // 強さ（0なら計算しない）
    float Padding;
    // 水たまりの波のシミュレーションの範囲と強さ（x,y = 範囲の最小のx・z、z = 範囲の一辺、w = 強さ。0なら使わない）
    DirectX::SimpleMath::Vector4 Ripple;
    // 水面の反射の比較（SSR）で使う、プレイヤーの視点の射影行列（転置済み）
    DirectX::SimpleMath::Matrix Projection;
    // 水面の反射の方式（x = 0 反射なし、1 キューブマップ、2 SSR、3 レイ反射（普段））。「反射なし」は床の反射の強さも0にしている。
    // y = SSRの深度と前のフレームの画面が用意されているか
    DirectX::SimpleMath::Vector4 Reflection;
    // 2面の「血の手形」の異変で壁に浮かべる手形（x = 壁の表面のx、y = 高さ、z = 奥行き、w = 見え方0〜1。0なら描かない）
    DirectX::SimpleMath::Vector4 Handprints[4];
    // 水面にだけ映す人影（xyz = 足もとの位置、w = 見え方0〜1。0なら描かない。負なら天井に逆さまに張り付いた人影）
    DirectX::SimpleMath::Vector4 PuddleFigure;
    // 2面の「見えない影」の異変で、影だけを落とす人影（xyz = 足もとの位置、w = 見え方0〜1。0なら影を付けない。shader/figureShadow.hlsli）
    DirectX::SimpleMath::Vector4 UnseenFigure;
    // レイ反射で水面に映す小物の箱の最大数と、その箱（[i*3] = 最小の角、[i*3+1] = 最大の角、[i*3+2] = 色）。数は Reflection.z
    static constexpr int MaxReflectionProps = 16;
    DirectX::SimpleMath::Vector4 ReflectionProps[MaxReflectionProps * 3];
    // 2面の壁の鏡に映す自分の体（xyz = 足もとの位置、w = 映すなら1）、向き（xy = 右の向きのx・z、zw = 前の向きのx・z）、
    // 動き（x = 歩く手足の振り -1〜1）。shader/mirrorPS.hlsl が光線と体の距離場で交差させている（MirrorSelf）
    DirectX::SimpleMath::Vector4 MirrorSelf;
    DirectX::SimpleMath::Vector4 MirrorSelfBasis;
    DirectX::SimpleMath::Vector4 MirrorSelfMotion;
    // 鏡の異変の、鏡にだけ映る黒い人影（xyz = 足もとの位置、w = 見え方0〜1。0なら描かない）
    DirectX::SimpleMath::Vector4 MirrorFigure;
    // プレイヤーの体が照明の光を遮って落とす影（xyz = 足もとの位置、w = 影を落とすなら1）と、
    // 向きと動き（xy = 前の向きのx・z、z = 歩く手足の振り -1〜1）。shader/figureShadow.hlsli がカプセルで影を求めている（Player）
    DirectX::SimpleMath::Vector4 PlayerBody;
    DirectX::SimpleMath::Vector4 PlayerBodyPose;
};

static_assert(sizeof(WATER_CAUSTICS_BUFFER) == 1152, "Water caustics buffer must match HLSL layout");

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
	// 崩れて現れる・消える度合い（0 = 元の形、1 = 消えた）。Wall::DissolveIn / DissolveOut が変えている
	float Dissolve;
	// PBR（金属・粗さの方式。shader/pbrLighting.hlsli）。Metallic は金属らしさ（0 = 金属でない、1 = 金属。金属は色がそのまま映り込みの色になる）。
	// Roughness は表面の粗さ（0.045〜1）。0 は指定なしで、Shininess（光沢の強さ）から換算している。
	float Metallic;
	float Roughness;
	float PbrPadding[2];


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
	static Microsoft::WRL::ComPtr<ID3D11Buffer> m_pWaterCausticsBuffer;
	static WATER_CAUSTICS_BUFFER m_WaterCaustics;
	// 計測モード（--figures）で、水たまりの人影と見えない影を決めた位置に置き続けるか、その足もとの位置
	static bool m_FigureOverride;
	static DirectX::SimpleMath::Vector3 m_PuddleFigureOverride;
	static DirectX::SimpleMath::Vector3 m_UnseenFigureOverride;
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

	// 新しい.csoがあればそれを読み、なければ（または古ければ）.hlslを実行時にコンパイルして.csoに保存している。
	static HRESULT CompileShader(
		const char* szFileName,
		LPCSTR szEntryPoint,
		LPCSTR szShaderModel,
		std::vector<unsigned char>& shaderObject);
	// フォルダの中のシェーダー（名前の最後が VS・PS・CS の.hlsl）を、まとめて.csoへコンパイルしている（ビルド時に使う）。
	// 失敗したシェーダーがあればfalseを返し、errorsにコンパイラーのエラーを入れている
	static bool PrecompileShaders(const std::filesystem::path& folder, std::string& errors);
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
	// 壁の古さ（0〜1）と、それに混ぜるホラーの跡（血の手形・引っかき傷・血の垂れ跡）の濃さ（0〜1）を設定している。
	// 古さが0なら前と同じ見た目のまま。
	static void SetWallWeathering(float weathering, float horrorMarks = 1.0f);
	// 2面の周回の深さ（0〜1）を設定している。0ならこれまでと同じ色調と霧
	static void SetLoopDepth(float depth);
	// 部屋の角の暗がりに使う壁の形と、床・天井の高さを設定している。boxCount が0なら暗がりを付けない。
	static void SetRoomOcclusion(
		const DirectX::SimpleMath::Vector4* boxes, unsigned int boxCount,
		float floorY, float ceilingY, float strength);
	// 浸水した範囲の水面で跳ね返った懐中電灯の光が、壁や天井に揺らいで映る効果の値を設定している。strengthが0なら計算しない。
	static void SetWaterCaustics(
		const DirectX::SimpleMath::Vector4& floodRect, float waterY, float time, float strength);
	// 水面の光の揺らぎで使う、プレイヤーの視点のビュー行列を設定している（毎フレーム、描画の最初に呼ぶ）
	static void SetWaterCausticsView(const DirectX::SimpleMath::Matrix& view);
	// 水たまりの波のシミュレーションの高さのテクスチャ（t9）と、その範囲・強さを、床と壁のシェーダーへ渡している。strengthが0なら使わない。
	static void SetWaterRipples(ID3D11ShaderResourceView* heightTexture,
		const DirectX::SimpleMath::Vector2& origin, float worldSize, float strength);
	// 水面の反射の方式（0 反射なし、1 キューブマップ、2 SSR、3 レイ反射）と、SSRで使うプレイヤーの視点の射影行列を、床と鏡のシェーダーへ渡している。
	// screenSpaceAvailable は、このフレームでSSRの深度と前のフレームの画面を用意するか（レイ反射が画面の中の物を拾うのに使う）
	static void SetReflectionMode(int mode, const DirectX::SimpleMath::Matrix& projection,
		bool screenSpaceAvailable = false);
	// 2面の「血の手形」の異変で壁に浮かべる手形（最大4つ。x = 壁の表面のx、y = 高さ、z = 奥行き、w = 見え方）を、壁のシェーダーへ渡している。countが0なら描かない
	static void SetWallHandprints(const DirectX::SimpleMath::Vector4* handprints, int count);
	// 水面にだけ映す人影の足もとの位置と見え方（0〜1。0なら描かない）を、床のシェーダーへ渡している。
	// 2面の「水たまり」の異変は床に立つ人影、hanging が true なら1面の西棟の天井に逆さまに張り付いた人影（足もとが天井）
	static void SetPuddleFigure(const DirectX::SimpleMath::Vector3& footPosition, float visibility, bool hanging = false);
	// 2面の「見えない影」の異変で、影だけを落とす人影の足もとの位置と見え方（0〜1。0なら影を付けない）を、壁と床のシェーダーへ渡している
	static void SetUnseenFigure(const DirectX::SimpleMath::Vector3& footPosition, float visibility);
	// 今の見えない影の人影（xyz = 足もと、w = 見え方）。光の筋の Compute Shader にも同じ人影の影を落とすために渡している
	static DirectX::SimpleMath::Vector4 GetUnseenFigure() { return m_WaterCaustics.UnseenFigure; }
	// 計測モード（--figures）用：異変の進行に関係なく、2つの人影を指定した位置に見え方1で置き続けている（人影の描画の重さを測るため）
	static void SetFigureOverride(bool enabled,
		const DirectX::SimpleMath::Vector3& puddleFoot, const DirectX::SimpleMath::Vector3& unseenFoot);
	// レイ反射で水面に映す小物の箱（props は1個につき 最小の角・最大の角・色 の3つ。最大 MaxReflectionProps 個）を、床のシェーダーへ渡している
	static void SetReflectionProps(const DirectX::SimpleMath::Vector4* props, int count);
	// 2面の壁の鏡に映す自分の体（足もとの位置・右と前の向き・歩く手足の振り -1〜1・映すか）を、鏡のシェーダーへ渡している（MirrorSelf）
	static void SetMirrorSelf(const DirectX::SimpleMath::Vector3& footPosition, const DirectX::SimpleMath::Vector3& right,
		const DirectX::SimpleMath::Vector3& forward, float swing, bool visible);
	// 鏡の異変の、鏡にだけ映る黒い人影の足もとの位置と見え方（0〜1。0なら描かない）を、鏡のシェーダーへ渡している
	static void SetMirrorFigure(const DirectX::SimpleMath::Vector3& footPosition, float visibility);
	// プレイヤーの体の位置・向き・手足の振りをb12へ設定している（壁と床のシェーダーが、体が照明を遮る影を付ける）
	static void SetPlayerBody(const DirectX::SimpleMath::Vector3& footPosition, const DirectX::SimpleMath::Vector3& forward,
		float swing, bool castsShadow);
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
