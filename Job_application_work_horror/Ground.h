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
#include "WaterRippleSimulation.h"
#include "WetFootprints.h"
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
		// 濡れた足跡（WetFootprints::Fill）。x = 数、と、1つにつき2つの値（位置と向き、濡れ具合・左右・裸足か）
		DirectX::SimpleMath::Vector4 FootprintInfo;
		DirectX::SimpleMath::Vector4 Footprints[WetFootprints::MaxCount * 2];
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
	// 床に残る濡れた足跡
	WetFootprints m_Footprints;
	// 足音や水滴でできた水たまりの波を、GPUの波の式で計算している
	WaterRippleSimulation m_RippleSimulation;
	// 波を進めるのは描画の最初（RunGpuSimulation）なので、更新で受け取った経過時間・中心・浸水した範囲を覚えている
	float m_PendingRippleSeconds = 0.0f;
	DirectX::SimpleMath::Vector2 m_RippleCenter;
	DirectX::SimpleMath::Vector4 m_RippleFloodRect;
	// 波を跳ね返す、床に立った物を上から見た長方形（カメラに近い順）
	std::vector<DirectX::SimpleMath::Vector4> m_RippleObstacles;

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
	// 描画の最初に、水たまりの波をCompute Shaderで進め、床と壁のシェーダーへ渡している
	void RunGpuSimulation() override;
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
	// 足元が水面なら、音もレンズの曇りも出さずに、水たまりの波だけを立てている（自動撮影で、歩いた跡の波を写すため）
	void AddRippleImpulse(const DirectX::SimpleMath::Vector3& position, float radius, float strength)
	{
		if (m_WaterEffects.IsInsidePuddle(position))
		{
			m_RippleSimulation.AddImpulse(
				DirectX::SimpleMath::Vector2(position.x, position.z), radius, strength);
		}
	}
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
	// 一歩ごとに、濡れた足跡を残すかを決めている（水の中の一歩なら、その後の数歩に濡れた靴の跡を残す）。
	// プレイヤーの足音（TriggerFootstepRipple）と、自動撮影の歩いた扱いから呼んでいる
	void RecordFootstep(const DirectX::SimpleMath::Vector3& position)
	{
		m_Footprints.OnPlayerStep(position, IsInsidePuddle(position));
	}
	// 足跡を1つ足している（2面の「誰か」の裸足の足跡）。yawは足の向き、sideは右足1・左足-1
	void AddFootprint(const DirectX::SimpleMath::Vector2& position, float yaw, float side, bool bare, float wetness)
	{
		m_Footprints.Add(position, yaw, side, bare, wetness);
	}
	// プレイヤーが水から出た瞬間なら、その場所を返してtrueにしている（1回だけ）
	bool ConsumeWaterExit(DirectX::SimpleMath::Vector2& exitPoint) { return m_Footprints.ConsumeWaterExit(exitPoint); }
	// 水たまりを決まった場所に置き直している（2面の廊下）。足音・波紋・水滴・シェーダーの表のすべてに反映される。
	void SetPuddleLayout(const WaterEffectSystem::PuddleLayout& layout);
	// 壁の鏡（2面）には、床も映している（水たまりの反射は床そのものなので、床は映さない）
	bool ContributesToWallMirror() const override { return true; }
	// 水面が画面に映っているときだけ、反射を描くようにしている
	bool IsPlanarReflectionSurfaceVisible(const Camera& camera) const override
	{
		return IsAnyPuddleVisible(camera);
	}
	// 頂点をワールド座標に変換したものを返している
	std::vector<VERTEX_3D>GetVertices();
};
