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
#include "WaterEffectSystem.h"
class Camera;

//-----------------------------------------------------------------------------
//TestPlaneクラス
//-----------------------------------------------------------------------------
class Ground :public Object 
{
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
	WaterEffectSystem m_WaterEffects;
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
	bool IsPlanarReflectionSurfaceVisible(const Camera& camera) const override
	{
		return IsAnyPuddleVisible(camera);
	}
	//頂点情報を取得
	std::vector<VERTEX_3D>GetVertices();
};
