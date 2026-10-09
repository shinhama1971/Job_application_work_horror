// ============================================================================
// ファイルの役割: すべてのゲームオブジェクトに共通する位置・向き・大きさ、寿命、仮想関数を定義している。
// 主な技術: 基底クラス、仮想関数、生成から破棄までの流れの管理
// ============================================================================

#pragma once
#include "Camera.h"
#include "Shader.h"
#include"Texture.h"
#include "ModelBounds.h"
#include <memory>
#include <vector>
#include "Renderer.h"
// すべてのゲームオブジェクトの基底クラス。実体はObjectManagerがunique_ptrで所有している。
// 寿命: 生成直後にInit → 毎フレームUpdate・Draw → Destroyで破棄を予約 → 更新の後にUninitして解放。
// どの描画（影・反射・補助カメラ）に参加するかは、下の仮想関数で各Objectが自分で宣言している。
class Object {
protected:
	// 拡大・回転・移動の情報（姿勢）。回転はラジアンで、x=ピッチ、y=ヨー、z=ロール。
	DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);
	// 破棄を予約されたか、モデルの境界（モデルを使うObjectだけが持っている）
	bool m_IsDestroy = false;
	std::shared_ptr<const ModelBounds> m_ModelBounds;

	// 描画のための情報（見た目に関わる部分）
	Shader m_Shader; // シェーダー
	Texture m_Texture;// テクスチャ
public:
	Object() {}
	virtual ~Object() {}

	// 生成した直後に一度だけ呼ばれる。メッシュ・シェーダー・定数バッファを用意している。
	virtual void Init()=0;
	// ゲームが進んでいる間、毎フレーム呼ばれる（ポーズ中とデバッグ画面で止めている間は呼ばれない）。
	virtual void Update() = 0;
	// 本描画・反射・補助カメラの各描画から、その描画のカメラを渡して呼ばれる。
	virtual void Draw(Camera* cam) = 0;
	// CastsShadowがtrueのObjectだけ、懐中電灯のシャドウマップへ深度を描いている。
	virtual void DrawShadow() {}
	virtual bool CastsShadow() const { return false; }
	// 描画への参加の可否は、型で判定するのではなく、各Object自身が宣言している。
	// trueなら視錐台の外にあるとき描画を省いている（HUDや全画面の効果はfalseのまま）。
	virtual bool UsesCameraCulling() const { return false; }
	// trueなら水たまりの平面反射に映る。
	virtual bool ContributesToPlanarReflection() const { return false; }
	// 監視カメラなど、プレイヤー以外の視点で描くワールドに含めるかどうか。
	virtual bool DrawsInAuxiliaryView() const { return true; }
	// 光を放つObjectは、このフレームの点光源を追加している。
	// Gameが毎フレーム全Objectから集め、タイルベースのライトカリングへ渡している。
	virtual void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
	{
		(void)lights;
	}
	// 反射面（水たまり）を持つObjectが、今画面に映っているかを返している。
	// どれもfalseのフレームは、反射用の描画を丸ごと省いている。
	virtual bool IsPlanarReflectionSurfaceVisible(const Camera&) const
	{
		return false;
	}
	// 解放する直前に一度だけ呼ばれる。
	virtual void Uninit() = 0;

	// 位置・回転・大きさを設定・取得している
	void SetPosition(DirectX::SimpleMath::Vector3 pos) { m_Position = pos; }
	void SetRotation(DirectX::SimpleMath::Vector3 rot) { m_Rotation = rot; }
	void SetScale(DirectX::SimpleMath::Vector3 scl) { m_Scale = scl; }
	DirectX::SimpleMath::Vector3 GetPosition() const { return m_Position; }
	DirectX::SimpleMath::Vector3 GetRotation() const { return m_Rotation; }
	DirectX::SimpleMath::Vector3 GetScale() const { return m_Scale; }
	// モデルの境界球。視錐台カリングと、影を描く対象の判定に使っている。
	// ModelCacheで読み込んだモデルは、同じ境界の情報をshared_ptrで共有している。
	void SetModelBounds(const ModelBounds& bounds)
	{
		m_ModelBounds = std::make_shared<ModelBounds>(bounds);
	}
	void SetModelBounds(std::shared_ptr<const ModelBounds> bounds)
	{
		m_ModelBounds = std::move(bounds);
	}
	// 有効な境界を持っているか
	bool HasModelBounds() const
	{
		return m_ModelBounds != nullptr && m_ModelBounds->IsValid;
	}
	// ローカル座標の境界と、ワールド座標に変換した境界球を返している
	const ModelBounds& GetModelBounds() const { return *m_ModelBounds; }
	WorldBoundingSphere GetWorldBoundingSphere() const;

	// 破棄を予約している。その場では消えず、更新ループの後でまとめて解放される。
	void Destroy() { m_IsDestroy = true; }
	bool IsDestroy() const { return m_IsDestroy; }
};
