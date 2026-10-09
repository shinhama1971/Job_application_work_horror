// ============================================================================
// ファイルの役割: 箱の形をした壁（と、同じ形を使う棚・配管・目印などの小物）の形・色・当たり判定・影の有無を管理している。
// 主な技術: コードで作る箱のメッシュ、AABBの当たり判定、一番近い面への押し戻し、拡大・回転・移動の行列
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"

class Camera;

// 1x1x1の箱を、大きさ（Scale）で好きな形に伸ばして使っている。ステージの壁・床・天井・小物の多くがこのクラス。
class Wall : public Object
{
private:
    // 頂点・インデックスとGPUのバッファ
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    IndexBuffer m_IndexBuffer;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;

    // マテリアルと、その値（色・光沢・壁の古さを描くかの印）
    std::unique_ptr<Material> m_Material;
    MATERIAL m_SurfaceMaterial{};
    // 当たり判定があるか、影を落とすか、表示しているか
    bool m_CollisionEnabled = true;
    bool m_CastsShadow = true;
    bool m_Visible = true;
    // 自分の光る色で周りを照らす点光源の範囲と強さ（0なら照らさない）
    float m_GlowRange = 0.0f;
    float m_GlowStrength = 0.0f;

public:
    // 箱を作る／何もしない／描く／影を描く／解放する
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return m_Visible && m_CastsShadow; }
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // プレイヤー（半径radiusの円）が箱にめり込んでいたら、外へ押し出している
    void ResolveCollision(
        DirectX::SimpleMath::Vector3& position,
        float radius) const;

    // 線分が箱に当たるかを調べ、当たった位置までの距離を返している（視線・音・調べる操作のさえぎりに使っている）
    bool IntersectsInteractionSegment(
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end,
        float& hitDistance) const;

    // 色・自己発光の色・光沢の強さを変えている
    void SetAppearance(
        const DirectX::SimpleMath::Color& diffuse,
        const DirectX::SimpleMath::Color& emission,
        float shininess);

    // 建物の壁として、壁の古さ（パネルの継ぎ目・ひび・水の跡・カビ）を描くかどうかを決めている。
    // 古さの濃さは面ごとに Renderer::SetWallWeathering で決めている。小物（棚・配管・標識）には使っていない。
    void SetWeatheringSurface(bool enabled);

    // 信号盤の目印のように、専用のピクセルシェーダー（signalPanelPS）で模様を描く面にするか
    void SetSignalSurface(bool enabled)
    {
        m_Shader.Create(
            "shader/litTextureVS.hlsl",
            enabled
                ? "shader/signalPanelPS.hlsl"
                : "shader/litTexturePS.hlsl");
    }

    // 当たり判定の有無、影の有無を切り替えている
    void SetCollisionEnabled(bool enabled)
    {
        m_CollisionEnabled = enabled;
    }

    void SetCastsShadow(bool castsShadow)
    {
        m_CastsShadow = castsShadow;
    }

    // 看板やランプのように、自分の光る色で周りを照らす点光源を出している。
    // rangeが0なら出さない。光る色が変わると、照らす色も自動でそれに合わせて変わる。
    void SetGlowLight(float range, float strength)
    {
        m_GlowRange = range;
        m_GlowStrength = strength;
    }

    // 照らす設定なら、点光源を1つ登録している
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;

    // 表示・非表示（非表示の間は当たり判定と影もなくなる）
    void SetVisible(bool visible)
    {
        m_Visible = visible;
    }

    // 大きさと位置を設定している
    void SetScale(float x, float y, float z)
    {
        m_Scale = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};
