// ============================================================================
// ファイルの役割: 画像を貼った四角形を2Dで描くクラス（アニメーションの分割の指定付き。今はどこからも使っていない）。
// 主な技術: Direct3D 11のテクスチャ、SRV、UVの行列による画像の切り出し
// ============================================================================

#include "Texture2D.h"
#include "utility.h"

using namespace std;
using namespace DirectX::SimpleMath;

//=======================================
// 初期化処理：原点を中心にした1x1の四角形と、照明を使わないシェーダーを用意している
//=======================================
void Texture2D::Init()
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
void Texture2D::Update()
{

}

//=======================================
// 描画処理
//=======================================
void Texture2D::Draw(Camera* cam)
{
	// 2D用の行列（画面の中央が原点）を設定している
	cam->SetCamera(1);

	// 拡大・回転・移動の行列を作っている
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

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

	// UVの設定：使うコマの位置と、1コマの大きさ（1/分割数）を行列にしている
	float u = m_NumU - 1;
	float v = m_NumV - 1;
	float uw = 1 / m_SplitX;
	float vh = 1 / m_SplitY;

	Renderer::SetUV(u, v, uw, vh);

	devicecontext->DrawIndexed(
		(UINT)m_Indices.size(), // 描くインデックスの数
		0, // インデックスバッファの最初の位置
		0);
}

//=======================================
// 終了処理（何もしない）
//=======================================
void Texture2D::Uninit()
{

}

// テクスチャを読み込んでいる（読めなければファイル名を表示して終了している）
void Texture2D::SetTexture(const char* imgname)
{
	// テクスチャを読み込んでいる
    if (!m_Texture.Load(imgname))
    {
        utility::ReportFatalError(
            std::string("テクスチャを読み込めませんでした。\n") + imgname);
    }
}

// 位置を指定している
void Texture2D::SetPosition(const float& x, const float& y, const float& z)
{
	Vector3 p = { x, y, z };
	SetPosition(p);
}
void Texture2D::SetPosition(const Vector3& pos)
{
	m_Position = pos;
}

// 角度を指定している
void Texture2D::SetRotation(const float& x, const float& y, const float& z)
{
	Vector3 r = { x, y, z };
	SetRotation(r);
}
void Texture2D::SetRotation(const Vector3& rot)
{
	m_Rotation = rot * 3.14f/180; // 度からラジアンに変換している
}

// 大きさを指定している
void Texture2D::SetScale(const float& x, const float& y, const float& z)
{
	Vector3 s = { x, y, z };
	SetScale(s);
}
void Texture2D::SetScale(const Vector3& scl)
{
	m_Scale = scl;
}

// UV座標を指定している
void Texture2D::SetUV(const float& nu, const float& nv, const float& sx, const float& sy)
{
	m_NumU = nu;
	m_NumV = nv;
	m_SplitX = sx;
	m_SplitY = sy;
}

// 画像の縦横の分割数を覚えている
void Texture2D::SetDivide(int divX, int divY)
{
	m_DivX = divX;
	m_DivY = divY;
}

// 指定したコマのUVを計算して設定している
void Texture2D::SetFrame(int frame)
{
	// コマの番号から、横と縦の番号を計算している
	// 例：横10分割でコマが12なら、x=2, y=1
	int xIndex = frame % m_DivX;
	int yIndex = frame / m_DivX;

	// UVの設定に渡している
	// （u座標, v座標, 横の分割数, 縦の分割数）
	SetUV((float)xIndex, (float)yIndex, (float)m_DivX, (float)m_DivY);
}