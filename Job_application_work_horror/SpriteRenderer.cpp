// ============================================================================
// ファイルの役割: 画像を貼った四角形（2Dスプライト）を描くためのクラス（今はどこからも使っていない）。
// 主な技術: 頂点・インデックスバッファ、テクスチャ、2D用の行列
// ============================================================================

#include "SpriteRenderer.h"
#include "utility.h"
using namespace std;
using namespace DirectX::SimpleMath;


//=======================================
// 初期化処理：原点を中心にした1x1の四角形と、照明を使わないシェーダーを用意している
//=======================================
void SpriteRenderer::Init()
{
	// 頂点データ
	m_Vertices.resize(4);

	m_Vertices[0].position = Vector3(-0.5f, 0.5f, 0);
	m_Vertices[1].position = Vector3(0.5f, 0.5f, 0);
	m_Vertices[2].position = Vector3(-0.5f, -0.5f, 0);
	m_Vertices[3].position = Vector3(0.5f, -0.5f, 0);

	m_Vertices[0].color = Color(1, 1, 1, 1);
	m_Vertices[1].color = Color(1, 1, 1, 1);
	m_Vertices[2].color = Color(1, 1, 1, 1);
	m_Vertices[3].color = Color(1, 1, 1, 1);

	m_Vertices[0].uv = Vector2(0, 0);
	m_Vertices[1].uv = Vector2(1, 0);
	m_Vertices[2].uv = Vector2(0, 1);
	m_Vertices[3].uv = Vector2(1, 1);

	// 頂点バッファを作っている
	m_VertexBuffer.Create(m_Vertices);

	// インデックスデータ（三角形ストリップで4頂点）
	m_Indices.resize(4);

	m_Indices[0] = 0;
	m_Indices[1] = 1;
	m_Indices[2] = 2;
	m_Indices[3] = 3;

	// インデックスバッファを作っている
	m_IndexBuffer.Create(m_Indices);

	// 照明を使わないシェーダーを作っている
	m_Shader.Create("shader/unlitTextureVS.hlsl", "shader/unlitTexturePS.hlsl");

	// マテリアルを作っている
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true; // テクスチャを使う
	m_Material->Create(mtrl);
}

//=======================================
// 更新処理（何もしない）
//=======================================
void SpriteRenderer::Update()
{

}

//=======================================
// 描画処理
//=======================================
void SpriteRenderer::Draw(Camera*cam)
{
	// 2D用の行列（画面の中央が原点）を設定している
	cam->SetCamera(1);

	// 拡大・回転・移動の行列を作っている
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	// ※大きさの行列をCreateTranslationで作っているため、大きさが反映されない（今はこのクラスを使っていないので影響はない）
	Matrix s = Matrix::CreateTranslation(m_Scale.x, m_Scale.y, m_Scale.x);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUへ設定している

	// デバイスコンテキストを取得している
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// 三角形ストリップとして描く
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	m_Texture.SetGPU();
	m_Material->SetGPU();

	// 四角形を描いている
	devicecontext->DrawIndexed(
		(UINT)m_Indices.size(), // 描くインデックスの数
		0, // インデックスバッファの最初の位置
		0);

	
}


//=======================================
// 終了処理（何もしない）
//=======================================
void SpriteRenderer::Uninit()
{

}


// テクスチャを読み込んでいる（読めなければファイル名を表示して終了している）
void SpriteRenderer::SetTexture(const char* imgname)
{
	// テクスチャを読み込んでいる
    if (!m_Texture.Load(imgname))
    {
        utility::ReportFatalError(
            std::string("テクスチャを読み込めませんでした。\n") + imgname);
    }
}

// UVの分割の情報を覚えている
void SpriteRenderer::SetUV(const float& nu, const float& nv, const float& sx, const float& sy)
{
	m_NumU = nu;
	m_NumV = nv;
	m_SplitX = sx;
	m_SplitY = sy;
}
