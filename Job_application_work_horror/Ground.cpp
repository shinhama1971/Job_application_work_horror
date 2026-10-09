// ============================================================================
// ファイルの役割: 床と、その上の水たまり・濡れた表現・水しぶきの描画を管理している。
// 主な技術: コードで作る格子状のメッシュ、拡大・回転・移動の行列、法線・UV、濡れた床のシェーダー（wetFloorPS）
// Ground.hで宣言した処理の中身（格子の作成・電力に合わせた水面の変化・描画）をここで定義している。
// ============================================================================

#include "Ground.h"
#include "Application.h"
#include "Game.h"
#include "Player.h"
#include "utility.h"
#include <algorithm>
#include <cmath>
using namespace DirectX::SimpleMath;

//=======================================
// 初期化処理：50x50マスの格子を作り、濡れた床のシェーダーとテクスチャを用意している
//=======================================
void Ground::Init()
{
	m_WetTime = 0.0f;
	m_PowerReflectionBlend = 0.0f;
	m_PowerSurge = 0.0f;
	m_WasPowerRestored = false;
	m_WaterEffects.Init();

	// 頂点データ：1マスを2つの三角形（6頂点）で作り、原点を中心に並べている
	m_SizeX = 50;
	m_SizeZ = 50;
	m_Vertices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;
			m_Vertices[n + 0].position = Vector3(-0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 1].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 2].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 3].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 4].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 5].position = Vector3(0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);

			m_Vertices[n + 0].color = Color(1, 1, 1, 1);
			m_Vertices[n + 1].color = Color(1, 1, 1, 1);
			m_Vertices[n + 2].color = Color(1, 1, 1, 1);
			m_Vertices[n + 3].color = Color(1, 1, 1, 1);
			m_Vertices[n + 4].color = Color(1, 1, 1, 1);
			m_Vertices[n + 5].color = Color(1, 1, 1, 1);

			// テクスチャはマスごとに0〜1を割り当て、マスごとに繰り返して貼っている
			m_Vertices[n + 0].uv = Vector2(0, 0);
			m_Vertices[n + 1].uv = Vector2(1, 0);
			m_Vertices[n + 2].uv = Vector2(0, 1);
			m_Vertices[n + 3].uv = Vector2(0, 1);
			m_Vertices[n + 4].uv = Vector2(1, 0);
			m_Vertices[n + 5].uv = Vector2(1, 1);

			m_Vertices[n + 0].normal = Vector3(0, 1, 0);
			m_Vertices[n + 1].normal = Vector3(0, 1, 0);
			m_Vertices[n + 2].normal = Vector3(0, 1, 0);
			m_Vertices[n + 3].normal = Vector3(0, 1, 0);
			m_Vertices[n + 4].normal = Vector3(0, 1, 0);
			m_Vertices[n + 5].normal = Vector3(0, 1, 0);
		}
	}

	// 各三角形の法線を、2辺の外積から計算し直している（平らなので、どれも真上を向く）
	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;

			// 三角形の2辺のベクトル
			Vector3 v1 = m_Vertices[n + 1].position - m_Vertices[n + 0].position;
			Vector3 v2 = m_Vertices[n + 2].position - m_Vertices[n + 0].position;
			Vector3 normal = v1.Cross(v2);// 外積を計算している
			normal.Normalize();			  // 長さを1にしている
			m_Vertices[n + 0].normal = normal;
			m_Vertices[n + 1].normal = normal;
			m_Vertices[n + 2].normal = normal;

			// 2つめの三角形の2辺のベクトル
			v1 = m_Vertices[n + 4].position - m_Vertices[n + 3].position;
			v2 = m_Vertices[n + 5].position - m_Vertices[n + 3].position;
			normal = v1.Cross(v2);// 外積を計算している
			normal.Normalize();	  // 長さを1にしている

			m_Vertices[n + 3].normal = normal;
			m_Vertices[n + 4].normal = normal;
			m_Vertices[n + 5].normal = normal;
		}
	}

	// 頂点バッファを作っている
	m_VertexBuffer.Create(m_Vertices);

	// インデックスデータ（頂点を順番どおりに使っている）
	m_Indices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;
			m_Indices[n + 0] = n + 0;
			m_Indices[n + 1] = n + 1;
			m_Indices[n + 2] = n + 2;
			m_Indices[n + 3] = n + 3;
			m_Indices[n + 4] = n + 4;
			m_Indices[n + 5] = n + 5;
		}
	}

	// インデックスバッファを作っている
	m_IndexBuffer.Create(m_Indices);

	// 濡れた床のピクセルシェーダーを使うシェーダーを作っている
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/wetFloorPS.hlsl");

	// 濡れた床の値を渡す定数バッファを作っている
	Renderer::CreateConstantBuffer(
		sizeof(WetFloorBuffer),
		m_WetFloorBuffer.ReleaseAndGetAddressOf());

	// 床のテクスチャを読み込んでいる（読めなければファイル名を表示して終了している）
    constexpr const char* fieldTexturePath = "assets/texture/field.jpg";
    if (!m_Texture.Load(fieldTexturePath))
    {
        utility::ReportFatalError(
            std::string("テクスチャを読み込めませんでした。\n") + fieldTexturePath);
    }

	// マテリアルを作っている
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;// テクスチャを使う
	m_Material->Create(mtrl);
	// 床の高さを-100にし、1マスを20単位に広げている（全体で1000x1000）
	m_Position.y = -100.0f;
	m_Scale.x = 20.0f;
	m_Scale.z = 20.0f;
}

//=======================================
// 更新処理：経過時間を進め、電力が戻ったときの水面の変化と、水滴・波紋を更新している
//=======================================
void Ground::Update()
{
	const float deltaTime = Application::GetDeltaTime();
	m_WetTime += deltaTime;
	// 長く遊んでも浮動小数点の精度が落ちないよう、時間を戻している
	if (m_WetTime > 10000.0f)
	{
		m_WetTime = 0.0f;
	}

	const bool powerRestored =
		Core::Game::GetInstance()->IsPowerRestored();
	if (powerRestored && !m_WasPowerRestored)
	{
		// 天井照明へ電気が流れた瞬間に水面を短く乱し、電力が戻った衝撃を見た目でも伝えている。
		m_PowerSurge = 1.0f;
	}
	m_WasPowerRestored = powerRestored;

	// 電力が戻ったら、反射をゆっくり強くしている（停電したら速く弱める）
	const float targetBlend = powerRestored ? 1.0f : 0.0f;
	const float response = powerRestored ? 2.2f : 4.0f;
	m_PowerReflectionBlend +=
		(targetBlend - m_PowerReflectionBlend) * response * deltaTime;
	m_PowerReflectionBlend = std::clamp(
		m_PowerReflectionBlend, 0.0f, 1.0f);
	// 水面の乱れは約2.4秒で収まる
	m_PowerSurge = (std::max)(0.0f, m_PowerSurge - deltaTime * 0.42f);
	m_WaterEffects.Update(deltaTime);
}

//=======================================
// 描画処理：床を濡れた床のシェーダーで描き、続けて水滴と波紋を描いている
//=======================================
void Ground::Draw(Camera* cam)
{
	// プレイヤーの視点の行列を設定している
	cam->SetCamera();

	// 拡大・回転・移動の行列を作り、GPUへ設定している
	Matrix worldmtx = MakeWorldMatrix();
	Renderer::SetWorldMatrix(&worldmtx);

	// デバイスコンテキストを取得している
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// 三角形のリストとして描く
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	m_Texture.SetGPU();
	m_Material->SetGPU();

	// 濡れた床の値：電力が戻ると波紋と反射を少し強め、戻った瞬間は細かく揺らしている
	WetFloorBuffer wetFloor{};
	wetFloor.Time = m_WetTime;
	const float surgeWave = m_PowerSurge *
		(0.55f + 0.45f * std::sin(m_WetTime * 17.0f));
	wetFloor.RippleStrength =
		0.92f + m_PowerReflectionBlend * 0.10f + surgeWave * 0.18f;
	wetFloor.ReflectionStrength =
		0.96f + m_PowerReflectionBlend * 0.22f + surgeWave * 0.12f;
	// 水に浸かった範囲がなければ、最小(1,1)が最大(0,0)より大きい値を渡して「範囲なし」にしている
	wetFloor.FloodRect = m_WaterEffects.HasFloodRegion()
		? DirectX::SimpleMath::Vector4(
			m_WaterEffects.GetFloodMin().x, m_WaterEffects.GetFloodMin().y,
			m_WaterEffects.GetFloodMax().x, m_WaterEffects.GetFloodMax().y)
		: DirectX::SimpleMath::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
	devicecontext->UpdateSubresource(
		m_WetFloorBuffer.Get(), 0, nullptr, &wetFloor, 0, 0);
	// ピクセルシェーダーのb10へ設定している
	ID3D11Buffer* wetFloorBuffer = m_WetFloorBuffer.Get();
	devicecontext->PSSetConstantBuffers(10, 1, &wetFloorBuffer);

	devicecontext->DrawIndexed(
		(UINT)m_Indices.size(),	// 描くインデックスの数
		0,					// インデックスバッファの最初の位置
		0);

	// 天井からの水滴と、足元の波紋を描いている
	m_WaterEffects.Draw(cam);
}

//=======================================
// ワールド行列：拡大・回転・移動の順に掛けている
//=======================================
Matrix Ground::MakeWorldMatrix() const
{
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.x, m_Rotation.y, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	return s * r * t;
}

//=======================================
// 深度プリパス：本描画と同じ頂点シェーダー・同じ行列で、床の面の深度だけを描いている
//=======================================
void Ground::DrawDepthPrepass(Camera* cam)
{
	cam->SetCamera();
	Matrix worldmtx = MakeWorldMatrix();
	Renderer::SetWorldMatrix(&worldmtx);

	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// ピクセルシェーダーを外し、深度だけを書いている
	m_Shader.SetGPU();
	devicecontext->PSSetShader(nullptr, nullptr, 0);
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();
	devicecontext->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

//=======================================
// 終了処理：水の効果と定数バッファを解放している
//=======================================
void Ground::Uninit()
{
	m_WaterEffects.Uninit();
	m_WetFloorBuffer.Reset();
}


//=======================================
// 頂点情報を取得している
//=======================================
std::vector<VERTEX_3D>Ground::GetVertices()
{
	std::vector<VERTEX_3D>res;
	res.resize(m_Vertices.size());
	// 拡大・回転・移動の行列を作っている
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	Matrix worldmtx = s * r * t;

	// 各頂点をワールド座標へ変換して入れている
	for (int i = 0; i < m_Vertices.size(); i++)
	{
		res[i].position = Vector3::Transform(m_Vertices[i].position, worldmtx);
		res[i].normal= Vector3::Transform(m_Vertices[i].normal, worldmtx);
		res[i].uv = m_Vertices[i].uv;
	}
	return res;
}

// 床のテクスチャを差し替えている
void Ground::SetTexture(const char* filename)
{
	m_Texture.Load(filename);
}
