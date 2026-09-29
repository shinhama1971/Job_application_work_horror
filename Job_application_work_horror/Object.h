// ============================================================================
// ファイルの役割: 全ゲームオブジェクト共通の座標、姿勢、寿命、仮想関数を定義します。
// 主な技術: 基底クラス、仮想関数、ライフサイクル管理
// ============================================================================

#pragma once
#include "Camera.h"
#include "Shader.h"
#include"Texture.h"
#include "ModelBounds.h"
#include <memory>
#include <vector>
#include "Renderer.h"
// 全ゲームオブジェクトの基底クラスです。実体はObjectManagerがunique_ptrで所有します。
// 寿命: 生成直後にInit → 毎フレームUpdate・Draw → Destroyで破棄予約 → 走査後にUninitして解放。
// どの描画パス（影・反射・補助カメラ）に参加するかは、下の仮想関数で各Objectが宣言します。
class Object {
protected:
	// SRT情報（姿勢情報）。回転はラジアンで、x=ピッチ、y=ヨー、z=ロールです。
	DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);
	bool m_IsDestroy = false;
	std::shared_ptr<const ModelBounds> m_ModelBounds;

	// 描画の為の情報（見た目に関わる部分）
	Shader m_Shader; // シェーダー
	Texture m_Texture;//テクスチャ
	bool m_IsFPS = true;
public:
	Object() {}
	virtual ~Object() {}

	// 生成直後に一度だけ呼ばれます。メッシュ・シェーダー・定数バッファを用意します。
	virtual void Init()=0;
	// ゲーム進行中の毎フレーム呼ばれます（ポーズ中とデバッグ停止中は呼ばれません）。
	virtual void Update() = 0;
	// 本描画・反射・補助カメラの各パスから、そのパスのカメラを渡して呼ばれます。
	virtual void Draw(Camera* cam) = 0;
	// CastsShadowがtrueのObjectだけ、懐中電灯のシャドウマップへ深度を描きます。
	virtual void DrawShadow() {}
	virtual bool CastsShadow() const { return false; }
	// 描画パスへの参加可否は型判定ではなく各Object自身が宣言します。
	// trueなら視錐台の外にあるとき描画を省きます（HUDや全画面効果はfalseのまま）。
	virtual bool UsesCameraCulling() const { return false; }
	// trueなら水たまりの平面反射に映ります。
	virtual bool ContributesToPlanarReflection() const { return false; }
	// 監視カメラなど、プレイヤー以外の視点で描くワールドに含めるかどうか。
	virtual bool DrawsInAuxiliaryView() const { return true; }
	// 光を放つObjectは、このフレームの点光源を追加します。
	// Gameが毎フレーム全Objectから集め、タイルベースのライトカリングへ渡します。
	virtual void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
	{
		(void)lights;
	}
	// 反射面（水たまり）を持つObjectが、いま画面に映っているかを返します。
	// どれもfalseのフレームは、反射用の描画パスを丸ごと省きます。
	virtual bool IsPlanarReflectionSurfaceVisible(const Camera&) const
	{
		return false;
	}
	// 解放直前に一度だけ呼ばれます。
	virtual void Uninit() = 0;

	void SetPosition(DirectX::SimpleMath::Vector3 pos) { m_Position = pos; }
	void SetRotation(DirectX::SimpleMath::Vector3 rot) { m_Rotation = rot; }
	void SetScale(DirectX::SimpleMath::Vector3 scl) { m_Scale = scl; }
	DirectX::SimpleMath::Vector3 GetPosition() const { return m_Position; }
	DirectX::SimpleMath::Vector3 GetRotation() const { return m_Rotation; }
	DirectX::SimpleMath::Vector3 GetScale() const { return m_Scale; }
	// モデルの境界球。視錐台カリングと影の対象判定に使います。
	// ModelCacheで読み込んだモデルは、同じ境界情報をshared_ptrで共有します。
	void SetModelBounds(const ModelBounds& bounds)
	{
		m_ModelBounds = std::make_shared<ModelBounds>(bounds);
	}
	void SetModelBounds(std::shared_ptr<const ModelBounds> bounds)
	{
		m_ModelBounds = std::move(bounds);
	}
	bool HasModelBounds() const
	{
		return m_ModelBounds != nullptr && m_ModelBounds->IsValid;
	}
	const ModelBounds& GetModelBounds() const { return *m_ModelBounds; }
	WorldBoundingSphere GetWorldBoundingSphere() const;

	// 破棄を予約します。その場では消えず、更新ループの後でまとめて解放されます。
	void Destroy() { m_IsDestroy = true; }
	bool IsDestroy() const { return m_IsDestroy; }
};
