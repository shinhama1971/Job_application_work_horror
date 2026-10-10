// ============================================================================
// ファイルの役割: 床と、その上の水たまり・濡れた表現・水しぶきの描画を管理している。
// 主な技術: コードで作る格子状のメッシュ、拡大・回転・移動の行列、法線・UV、濡れた床のシェーダー（wetFloorPS）
// ============================================================================

#pragma once
#include <wrl/client.h>

#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include"Object.h"
#include"Material.h"
#include "WaterEffectSystem.h"
class Camera;

//-----------------------------------------------------------------------------
// Groundクラス：建物全体の床。濡れた床のシェーダーで水たまりと反射を描き、水滴・波紋はWaterEffectSystemに任せている
//-----------------------------------------------------------------------------
class Ground :public Object 
{
	// 濡れた床のシェーダーへ渡す値（定数バッファb10）
	struct WetFloorBuffer
	{
		// 経過時間（波紋を動かす）、波紋の強さ、反射の強さ、16バイト単位にそろえる詰め物
		float Time;
		float RippleStrength;
		float ReflectionStrength;
		float Padding;
		// 床一面が水に浸かった範囲（x・zの最小と最大）。範囲がないときは、最小が最大より大きい値にしている。
		DirectX::SimpleMath::Vector4 FloodRect;
	};
	
	// 頂点データ（50x50マスの格子）
	std::vector<VERTEX_3D> m_Vertices;

	// インデックスデータ
	std::vector<unsigned int> m_Indices;

	// 描画するための情報（メッシュに関わる情報）
	IndexBuffer	 m_IndexBuffer; // インデックスバッファ
	VertexBuffer<VERTEX_3D>	m_VertexBuffer; // 頂点バッファ
	// 床のマテリアル（テクスチャはObjectのm_Textureを使っている）
	std::unique_ptr<Material>m_Material;
	int m_SizeX = 0;// 格子の横のマス数
	int m_SizeZ = 0;// 格子の縦のマス数
	// 濡れた床の定数バッファ、経過時間
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_WetFloorBuffer;
	// マス目ごとの水たまりの表（定数バッファb13）。起動時に1回だけ書き込んでいる
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_PuddleCellBuffer;
	float m_WetTime = 0.0f;
	// 電力が戻ったときに反射を強める度合い、電力が戻った瞬間の水面の乱れ、前フレームで電力が戻っていたか
	float m_PowerReflectionBlend = 0.0f;
	float m_PowerSurge = 0.0f;
	bool m_WasPowerRestored = false;
	// 水たまりの位置、天井からの水滴、足元の波紋をまとめて管理している
	WaterEffectSystem m_WaterEffects;

	// 本描画と深度プリパスで共通のワールド行列（同じ計算にし、深度を一致させている）
	DirectX::SimpleMath::Matrix MakeWorldMatrix() const;
public:
	

	// 格子のメッシュ・シェーダー・テクスチャを作る／電力に合わせて水面を変化させる／床と水の効果を描く／解放する
	void Init();
	void Update();
	void Draw(Camera* cam);
	// 床は不透明なので、深度プリパスに床の面だけを描いている（水滴・波紋の半透明の板は描かない）
	bool WritesDepthPrepass() const override { return true; }
	void DrawDepthPrepass(Camera* cam) override;
	void Uninit();
	// 大きさと位置を設定している
	void SetScale(float x, float y, float z) { m_Scale = DirectX::SimpleMath::Vector3(x, y, z); }
	void SetPosition(float x, float y, float z) { m_Position = DirectX::SimpleMath::Vector3(x, y, z); }
	// 床のテクスチャを差し替えている
	void SetTexture(const char* filename);
	// 足元が水面なら波紋を1回作り、作ったらtrueを返している。足音と描画を同じ歩くタイミングにそろえている。
	bool TriggerFootstepRipple(
		const DirectX::SimpleMath::Vector3& position,
		bool sprinting);
	// その位置が水たまり（または水に浸かった範囲）の中かを返している
	bool IsInsidePuddle(
		const DirectX::SimpleMath::Vector3& position) const;
	// 水面が画面の外なら、重い平面反射の描画を丸ごと省けるようにしている。
	bool IsAnyPuddleVisible(const Camera& camera) const;
	// x・z の範囲を、床一面が水に浸かった場所にしている（1面の西棟）。描画・足音・波紋のすべてに反映される。
	void SetFloodRegion(
		const DirectX::SimpleMath::Vector2& minimum,
		const DirectX::SimpleMath::Vector2& maximum)
	{
		m_WaterEffects.SetFloodRegion(minimum, maximum);
	}
	// 水面が画面に映っているときだけ、反射を描くようにしている
	bool IsPlanarReflectionSurfaceVisible(const Camera& camera) const override
	{
		return IsAnyPuddleVisible(camera);
	}
	// 頂点をワールド座標に変換したものを返している
	std::vector<VERTEX_3D>GetVertices();
};
