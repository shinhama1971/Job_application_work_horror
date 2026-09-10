// ============================================================================
// ファイルの役割: 全ゲームオブジェクト共通の座標、姿勢、寿命、仮想関数を定義します。
// ============================================================================

#pragma once
#include "Camera.h"
#include "Shader.h"
#include"Texture.h"
using namespace DirectX::SimpleMath;
class Object {
protected:
	// SRT情報（姿勢情報）
	Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);
	bool m_IsDestroy = false;

	// 描画の為の情報（見た目に関わる部分）
	Shader m_Shader; // シェーダー
	Texture m_Texture;//テクスチャ
	bool m_IsFPS = true;
public:
	Object() {}
	virtual ~Object() {}

	virtual void Init()=0;
	virtual void Update() = 0;
	virtual void Draw(Camera* cam) = 0;
	virtual void DrawShadow() {}
	virtual bool CastsShadow() const { return false; }
	// 描画パスへの参加可否は型判定ではなく各Object自身が宣言します。
	virtual bool UsesCameraCulling() const { return false; }
	virtual bool ContributesToPlanarReflection() const { return false; }
	virtual bool IsPlanarReflectionSurfaceVisible(const Camera&) const
	{
		return false;
	}
	virtual void Uninit() = 0;

	void SetPosition(DirectX::SimpleMath::Vector3 pos) { m_Position = pos; }
	void SetRotation(DirectX::SimpleMath::Vector3 rot) { m_Rotation = rot; }
	void SetScale(DirectX::SimpleMath::Vector3 scl) { m_Scale = scl; }
	// 位置の取得
	DirectX::SimpleMath::Vector3 GetPosition() const { return m_Position; }
	DirectX::SimpleMath::Vector3 GetRotation() const { return m_Rotation; }
	DirectX::SimpleMath::Vector3 GetScale() const { return m_Scale; }

	void Destroy() { m_IsDestroy = true; }
	bool IsDestroy() const { return m_IsDestroy; }
};
