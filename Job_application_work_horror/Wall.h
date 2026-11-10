// ============================================================================
// ファイルの役割: 箱の形をした壁（と、同じ形を使う棚・配管・目印などの小物）の形・色・当たり判定・影の有無を管理している。
// 主な技術: コードで作る箱のメッシュ、AABBの当たり判定、一番近い面への押し戻し、拡大・回転・移動の行列、
//           見た目だけをMayaで作ったモデルにする小物（SetVisualModel。当たり判定・影の有無・水面に映す箱は箱のまま）
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"

#include <cmath>
#include <string>

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
    // 光る色と点光源の強さに掛ける倍率（SetEmissionScale）
    float m_EmissionScale = 1.0f;
    // 崩れ具合（0 = 元の形、1 = 消えた）と、向かう値、1秒あたりの変化（0なら止まっている）
    float m_Dissolve = 0.0f;
    float m_DissolveTarget = 0.0f;
    float m_DissolveSpeed = 0.0f;
    // 崩れ具合をマテリアルへ入れている
    void ApplyDissolve(float dissolve);
    // 絵を計算で描く面（肖像画・時計）か、その絵へ渡す値（肖像画は目の状態、時計は針の角度）
    bool m_HasSurfaceParams = false;
    DirectX::SimpleMath::Color m_SurfaceParams = DirectX::SimpleMath::Color(0.0f, 0.0f, 0.0f, 0.0f);
    // 壁の鏡の面か（鏡は自分の色ではなく、鏡で反射した光線の先を映す。水面に映す小物の箱にもしない）
    bool m_IsMirror = false;

    // 見た目に使うモデルの名前（空なら箱）と、箱の頂点（モデルをやめたときに戻すため）
    std::string m_VisualModel;
    // 外の森のモデルを、岩・樹皮の材質のシェーダー（rockPS・barkPS）で描いているか
    // Facade は、研究所の外壁の材質（facadePS。モデルではなく箱に、配置ファイルの facade で付ける）
    enum class NaturalSurface { None, Rock, Bark, Facade };
    bool m_FacadeSurface = false;
    NaturalSurface m_NaturalSurface = NaturalSurface::None;
    // 樹皮の木目の向き（モデルを箱の向きへ回した後。木と茂みは上、倒木は長い向き）。UploadMaterial がワールドへ回して渡す
    DirectX::SimpleMath::Vector3 m_GrainAxis = DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f);
    std::vector<VERTEX_3D> m_BoxVertices;
    std::vector<unsigned int> m_BoxIndices;
    // モデルの光る部位（レンズ・電球）が始まるインデックス。ここより前（枠・笠・台）は光る色を付けないマテリアルで描いている。
    // 箱と、光る部位のないモデルは0（全体に光る色を付ける）
    unsigned int m_GlowIndexStart = 0;
    std::unique_ptr<Material> m_UnlitPartMaterial;
    // 見た目の頂点（箱かモデル）を作り直し、GPUのバッファへ送っている
    void RebuildVisualModel();
    // マテリアルをGPUへ送っている（モデルのときは、色を頂点の色で付けるため、マテリアルの色を白にしている）
    void UploadMaterial();

    // 本描画・影・深度プリパスで共通のワールド行列（同じ計算にし、深度プリパスと本描画の深度を一致させている）
    DirectX::SimpleMath::Matrix MakeWorldMatrix() const;

public:
    // 箱を作る／何もしない／描く／影を描く／解放する
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void DrawShadow() override;
    bool CastsShadow() const override { return m_Visible && m_CastsShadow; }
    // 天井や影を落とさない小物も含め、不透明な箱は深度プリパスに描いている。
    // 半透明の色は後ろの物が透けて見えるため、描かない。
    bool WritesDepthPrepass() const override
    {
        // 崩れている途中は、ピクセルシェーダーで画素を捨てるため描かない
        return m_Visible && m_SurfaceMaterial.Diffuse.w >= 0.999f && m_Dissolve <= 0.0f;
    }
    void DrawDepthPrepass(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToReflection() const override { return true; }
    // 床に置いた棚・ロッカー・柱などの小物だけ、レイ反射で水面に映す箱を返している（建物の壁・天井・床板・鏡・半透明の物は除く）
    bool GetReflectionProxy(
        DirectX::SimpleMath::Vector3& minimum, DirectX::SimpleMath::Vector3& maximum, DirectX::SimpleMath::Color& color) const override;
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

    // PBRの表面の仕上げ（金属らしさ0〜1、粗さ0.045〜1。粗さ0は光沢の強さから換算する）を決めている
    void SetSurfaceFinish(float metallic, float roughness);

    // 建物の壁として、壁の古さ（パネルの継ぎ目・ひび・水の跡・カビ）を描くかどうかを決めている。
    // 古さの濃さは面ごとに Renderer::SetWallWeathering で決めている。小物（棚・配管・標識）には使っていない。
    void SetWeatheringSurface(bool enabled);
    // 研究所の外壁の材質（facadePS）で描く箱にするか。次の SetVisualModel でシェーダーを選ぶ
    void SetFacadeSurface(bool enabled) { m_FacadeSurface = enabled; }

    // 信号盤の目印のように、専用のピクセルシェーダー（signalPanelPS）で模様を描く面にするか
    void SetSignalSurface(bool enabled)
    {
        m_Shader.Create(
            "shader/litTextureVS.hlsl",
            enabled
                ? "shader/signalPanelPS.hlsl"
                : "shader/litTexturePS.hlsl");
    }

    // 2面の肖像画のように、古い油絵の人物を計算で描く面にしている（portraitPS。照明は壁と同じ）
    void SetPortraitSurface()
    {
        m_HasSurfaceParams = true;
        m_Shader.Create("shader/litTextureVS.hlsl", "shader/portraitPS.hlsl");
    }
    // 肖像画の目の状態（開き具合0〜1、瞳の向き-1〜1の横と縦、瞳の赤い光0〜1）を設定している。
    // 絵のシェーダーには、肖像画では使っていないマテリアルの環境の色（Ambient）に入れて渡している
    void SetPortraitEyes(float openness, float gazeX, float gazeY, float glow);
    // 2面の時計の文字盤のように、文字盤・目盛り・数字・針を計算で描く面にしている（clockPS。照明は壁と同じ）
    void SetClockSurface()
    {
        m_HasSurfaceParams = true;
        m_Shader.Create("shader/litTextureVS.hlsl", "shader/clockPS.hlsl");
    }
    // 時計の針の角度（12時が0、右回りが+）を設定している（文字盤のシェーダーが針を描く）
    void SetClockHands(float hourAngle, float minuteAngle);
    // 2面の偽の扉のように、木の扉（羽目板・木目・剥げた塗装・部屋番号の札・鍵穴）を計算で描く面にしている（doorPS）
    void SetDoorSurface()
    {
        m_HasSurfaceParams = true;
        m_Shader.Create("shader/litTextureVS.hlsl", "shader/doorPS.hlsl");
    }
    // 絵を計算で描く面へ渡す値（肖像画は目の状態、時計は針の角度、扉は取っ手の側）を設定している。
    // シェーダーには、これらの面では使っていないマテリアルの環境の色（Ambient）に入れて渡している
    void SetSurfaceParams(const DirectX::SimpleMath::Color& params);

    // 壁の鏡（2面）の面にしている。映る景色は、鏡のピクセルシェーダー（mirrorPS）が、面の向きで反射した光線を
    // 部屋の形・小物の箱・画面の中（SSR）へ飛ばして求めている（景色をもう一度描かない）。
    void SetMirrorSurface()
    {
        m_IsMirror = true;
        m_Shader.Create("shader/litTextureVS.hlsl", "shader/mirrorPS.hlsl");
    }

    // 見た目を、Mayaで作ったモデル（tools/maya/build_wall_props.py）にしている。空の名前なら箱に戻している。
    // モデルは箱の大きさに合わせて伸ばし、長い物（棚・梁・額縁の棒）は箱の一番長い向きへ回している。
    // 当たり判定・影の有無・水面に映す箱は、これまでどおり箱の大きさのまま。大きさを変えた後に呼んでいる
    void SetVisualModel(const std::string& name);
    const std::string& GetVisualModel() const { return m_VisualModel; }
    // 配置ファイルに書ける、モデルの名前か
    static bool IsKnownVisualModel(const std::string& name);

    // 当たり判定の有無、影の有無を切り替えている
    void SetCollisionEnabled(bool enabled)
    {
        m_CollisionEnabled = enabled;
    }
    // 今、当たり判定があるか（表示していない間はない）。水たまりの波を跳ね返す物を選ぶのにも使っている
    bool IsCollisionEnabled() const
    {
        return m_Visible && m_CollisionEnabled;
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

    // 光る色と、照らす点光源の強さに掛ける倍率（0 = 消えている、1 = 配置ファイルのまま）。
    // 外の森の街灯のちらつきや、研究所の窓の明かりが消える演出（ForestEvents）で使っている
    void SetEmissionScale(float scale);
    float GetEmissionScale() const { return m_EmissionScale; }

    // 照らす設定なら、点光源を1つ登録している
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;

    // 表示・非表示（非表示の間は当たり判定と影もなくなる）
    // すぐに切り替える（崩れて現れる・消える途中なら、それをやめて元の形に戻している）
    void SetVisible(bool visible);
    bool IsVisible() const { return m_Visible; }

    // 焦げた縁が広がるように崩れて現れる・消える（secondsかけて。消え終わったら非表示にしている）。
    // 崩れ具合はマテリアルの Dissolve でシェーダー（litTexturePS）へ渡し、ノイズより小さい所を捨てている。
    void DissolveIn(float seconds);
    void DissolveOut(float seconds);
    // 崩れて消えていく途中か
    bool IsDissolvingOut() const { return m_DissolveSpeed > 0.0f && m_DissolveTarget >= 1.0f; }

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
