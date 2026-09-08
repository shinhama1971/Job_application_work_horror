// ============================================================================
// ファイルの役割: 床面、水たまり、濡れ表現と関連する描画を管理します。
// ============================================================================

#pragma once
#include <wrl/client.h>

#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include"Object.h"
#include"Material.h"
class Camera;

//-----------------------------------------------------------------------------
//TestPlaneクラス
//-----------------------------------------------------------------------------
class Ground :public Object 
{
	struct FallingDrop
	{
		DirectX::SimpleMath::Vector3 Position;
		float Speed = 20.0f;
		float WaitTimer = 0.0f;
		float ImpactTimer = 0.0f;
		float Seed = 0.0f;
		bool Active = true;
	};

	struct WaterRipple
	{
		DirectX::SimpleMath::Vector3 Position;
		float Age = 0.0f;
		float Duration = 0.7f;
		float MaxRadius = 3.0f;
	};

	struct WetFloorBuffer
	{
		float Time;
		float RippleStrength;
		float ReflectionStrength;
		float Padding;
	};
	
	// 頂点データ
	std::vector<VERTEX_3D> m_Vertices;

	//インデックスデータ
	std::vector<unsigned int> m_Indices;

	// 描画の為の情報（メッシュに関わる情報）
	IndexBuffer	 m_IndexBuffer; // インデックスバッファ
	VertexBuffer<VERTEX_3D>	m_VertexBuffer; // 頂点バッファ
	//Texture m_Texture;
	std::unique_ptr<Material>m_Material;
	int m_SizeX = 0;//横サイズ
	int m_SizeZ = 0;//縦サイズ
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_WetFloorBuffer;
	float m_WetTime = 0.0f;
	float m_PowerReflectionBlend = 0.0f;
	float m_PowerSurge = 0.0f;
	bool m_WasPowerRestored = false;
	std::vector<FallingDrop> m_FallingDrops;
	std::vector<DirectX::SimpleMath::Vector2> m_PuddleCenters;
	std::vector<VERTEX_3D> m_DropVertices;
	VertexBuffer<VERTEX_3D> m_DropVertexBuffer;
	Shader m_DropShader;
	std::unique_ptr<Material> m_DropMaterial;
	std::vector<VERTEX_3D> m_RippleVertices;
	VertexBuffer<VERTEX_3D> m_RippleVertexBuffer;
	std::unique_ptr<Material> m_RippleMaterial;
	std::vector<WaterRipple> m_FootstepRipples;
	DirectX::SimpleMath::Vector3 m_LastPlayerPosition;
	float m_FootstepRippleCooldown = 0.0f;
	float m_LensSplashCooldown = 0.0f;
	bool m_HasLastPlayerPosition = false;

	void BuildFallingDrops();
	void UpdateFallingDrops(float deltaTime);
	void DrawFallingDrops(Camera* camera);
	void UpdateFootstepRipples(float deltaTime);
	void DrawWaterRipples(Camera* camera);
public:
	

	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();
	void SetScale(float x, float y, float z) { m_Scale = DirectX::SimpleMath::Vector3(x, y, z); }
	void SetPosition(float x, float y, float z) { m_Position = DirectX::SimpleMath::Vector3(x, y, z); }
	void SetTexture(const char* filename);
	// 足元が水面なら波紋を1回生成します。足音と描画を同じ歩行イベントへ同期します。
	bool TriggerFootstepRipple(
		const DirectX::SimpleMath::Vector3& position,
		bool sprinting);
	bool IsInsidePuddle(
		const DirectX::SimpleMath::Vector3& position) const;
	// 反射面が画面外なら高コストな平面反射パスを丸ごと省略できます。
	bool IsAnyPuddleVisible(const Camera& camera) const;
	//頂点情報を取得
	std::vector<VERTEX_3D>GetVertices();
};
