// ============================================================================
// ファイルの役割: 壁の当たり判定に重ねる外観モデルと、部位ごとの材質を設定する。
// ============================================================================

#include "Wall.h"
#include "PropModel.h"
#include "Utility.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 見た目だけモデルにするときの部位。UsesSurfaceColor の部位は配置ファイルの色（SetAppearance）で塗り、
    // それ以外（書類・取っ手・すき間の暗がりなど）はここで決めた色で塗っている
    // Glows の部位（警告灯のレンズ・電球）だけに、配置ファイルの光る色を付けている（枠や笠は光らせない）
    struct VisualModelPart
    {
        const char* Material;
        Color PartColor;
        bool UsesSurfaceColor;
        bool Glows = false;
    };

    // モデル1つ分：配置ファイルで使う名前、ファイル、標準の大きさ（長い物は x が長い向き）、箱の一番長い向きへ回すか、部位
    struct VisualModel
    {
        const char* Name;
        const char* Path;
        Vector3 Size;
        bool FollowsLongestAxis;
        std::vector<VisualModelPart> Parts;
    };

    const VisualModelPart MainPart{ "Main", Color(1.0f, 1.0f, 1.0f, 1.0f), true };
    const VisualModelPart DarkPart{ "Dark", Color(0.012f, 0.012f, 0.012f, 1.0f), false };
    const VisualModelPart SlitPart{ "Slit", Color(0.015f, 0.016f, 0.015f, 1.0f), false };
    const VisualModelPart HandlePart{ "Handle", Color(0.42f, 0.42f, 0.38f, 1.0f), false };
    // 警告灯とライト：光る部位（配置ファイルの色で塗って光らせる）と、光らない金属の枠
    const VisualModelPart LensPart{ "Lens", Color(1.0f, 1.0f, 1.0f, 1.0f), true, true };
    const VisualModelPart HousingPart{ "Housing", Color(0.075f, 0.08f, 0.078f, 1.0f), false };
    // 書類棚に並ぶファイル・段ボール箱・紙の束（長い棚と短い棚で同じ色）
    const VisualModelPart BinderPart{ "Binder", Color(0.10f, 0.12f, 0.17f, 1.0f), false };
    const VisualModelPart BoxPart{ "Box", Color(0.30f, 0.24f, 0.16f, 1.0f), false };
    const VisualModelPart PaperPart{ "Paper", Color(0.46f, 0.44f, 0.40f, 1.0f), false };

    const std::vector<VisualModel>& GetVisualModels()
    {
        static const std::vector<VisualModel> models =
        {
            // 書類棚（棚板・柱・筋かいと、ファイル・段ボール箱・紙の束）
            { "shelf_long", "assets/model/props/wall_shelf_long.fbx", Vector3(90.0f, 44.0f, 6.0f), true,
                { MainPart, BinderPart, BoxPart, PaperPart } },
            { "shelf_short", "assets/model/props/wall_shelf_short.fbx", Vector3(30.0f, 44.0f, 10.0f), true,
                { MainPart, BinderPart, BoxPart, PaperPart } },
            // 机の天板（幕板と引き出し）と脚
            { "desk_top", "assets/model/props/wall_desk_top.fbx", Vector3(40.0f, 2.0f, 24.0f), false,
                { MainPart, { "Metal", Color(0.40f, 0.38f, 0.33f, 1.0f), false } } },
            { "desk_leg", "assets/model/props/wall_desk_leg.fbx", Vector3(2.0f, 23.0f, 2.0f), false, { MainPart } },
            // 板を打ち付けた木箱と、崩れた資材の山
            { "crate", "assets/model/props/wall_crate.fbx", Vector3(14.0f, 14.0f, 14.0f), false, { MainPart, DarkPart } },
            { "debris", "assets/model/props/wall_debris.fbx", Vector3(40.0f, 30.0f, 30.0f), false,
                { MainPart, { "Concrete", Color(0.21f, 0.20f, 0.19f, 1.0f), false } } },
            // 鋼の梁・管の柱（長い向きへ回す）
            { "beam", "assets/model/props/wall_beam.fbx", Vector3(60.0f, 6.0f, 6.0f), true, { MainPart } },
            { "pipe_pole", "assets/model/props/wall_pipe_pole.fbx", Vector3(50.0f, 3.0f, 3.0f), true, { MainPart } },
            // 引き出しのキャビネット・壁際の配電箱
            { "cabinet", "assets/model/props/wall_cabinet.fbx", Vector3(28.0f, 24.0f, 12.0f), false, { MainPart, HandlePart } },
            { "service_box", "assets/model/props/wall_service_box.fbx", Vector3(10.0f, 30.0f, 24.0f), false,
                { MainPart, HandlePart, SlitPart } },
            // 西棟のポンプ・タンク・排水口
            { "pump", "assets/model/props/wall_pump.fbx", Vector3(30.0f, 26.0f, 26.0f), false,
                { MainPart, { "Paint", Color(0.12f, 0.18f, 0.15f, 1.0f), false } } },
            { "tank", "assets/model/props/wall_tank.fbx", Vector3(20.0f, 34.0f, 30.0f), false, { MainPart } },
            { "drain", "assets/model/props/wall_drain.fbx", Vector3(16.0f, 0.8f, 16.0f), false, { MainPart, DarkPart } },
            // 柱（上下の台つき）
            { "column", "assets/model/props/wall_column.fbx", Vector3(10.0f, 38.0f, 10.0f), false, { MainPart } },
            // 2面のロッカーの本体と、鏡・肖像画・偽の扉の額縁の棒
            { "locker_body", "assets/model/props/wall_locker_body.fbx", Vector3(10.0f, 44.0f, 16.0f), false, { MainPart, SlitPart } },
            { "frame_rail", "assets/model/props/wall_frame_rail.fbx", Vector3(20.0f, 2.0f, 1.8f), true, { MainPart } },
            // 壁の警告灯（光るレンズを金属の枠と格子で囲む。壁に沿った長い向きへ回す）
            { "warning_lamp", "assets/model/props/wall_warning_lamp.fbx", Vector3(10.0f, 4.0f, 1.0f), true,
                { HousingPart, LensPart } },
            // 安全地帯の裸電球（ソケットと光る電球）
            { "safe_bulb", "assets/model/props/wall_safe_bulb.fbx", Vector3(2.6f, 3.2f, 2.6f), false, { HousingPart, LensPart } },
            // 卓上ライト（緑の琺瑯の笠と、下から見える光る電球・笠の裏）、腕、台
            { "desk_lamp_shade", "assets/model/props/wall_desk_lamp_shade.fbx", Vector3(5.0f, 3.0f, 5.0f), false,
                { HousingPart, { "Shade", Color(0.10f, 0.16f, 0.12f, 1.0f), false }, LensPart } },
            // 天井沿いの長い配管（継ぎ目のフランジと吊り金具。長い向きへ回す）
            { "pipe_run", "assets/model/props/wall_pipe_run.fbx", Vector3(300.0f, 3.0f, 3.0f), true, { MainPart } },
            // 扉の上の表示灯（光る表示面を金属の枠で囲む）と、電球を吊るすコード
            { "door_indicator", "assets/model/props/wall_door_indicator.fbx", Vector3(10.0f, 5.0f, 1.0f), true,
                { HousingPart, LensPart } },
            { "hanging_cord", "assets/model/props/wall_hanging_cord.fbx", Vector3(0.4f, 2.5f, 0.4f), false, { MainPart } },
            // 外の森（forest_props.txt）：枯れ木2種（標準の大きさは幹の当たり判定の箱。枝はその外へ広がる）、倒木（長い向きへ回す）、岩、枯れた茂み
            { "forest_tree_dead", "assets/model/props/forest_tree_dead.fbx", Vector3(8.0f, 130.0f, 8.0f), false, { MainPart } },
            { "forest_tree_fork", "assets/model/props/forest_tree_fork.fbx", Vector3(8.0f, 130.0f, 8.0f), false, { MainPart } },
            { "forest_fallen_log", "assets/model/props/forest_fallen_log.fbx", Vector3(60.0f, 8.0f, 8.0f), true, { MainPart } },
            { "forest_rock", "assets/model/props/forest_rock.fbx", Vector3(14.0f, 9.0f, 12.0f), false, { MainPart } },
            { "forest_brush", "assets/model/props/forest_brush.fbx", Vector3(20.0f, 12.0f, 20.0f), false, { MainPart } },
            { "desk_lamp_arm", "assets/model/props/wall_desk_lamp_arm.fbx", Vector3(0.8f, 9.0f, 0.8f), false, { MainPart } },
            { "desk_lamp_base", "assets/model/props/wall_desk_lamp_base.fbx", Vector3(4.0f, 1.0f, 4.0f), false,
                { MainPart, HousingPart } },
        };
        return models;
    }

    const VisualModel* FindVisualModel(const std::string& name)
    {
        for (const VisualModel& model : GetVisualModels())
        {
            if (name == model.Name)
            {
                return &model;
            }
        }
        return nullptr;
    }
}

bool Wall::IsKnownVisualModel(const std::string& name)
{
    return FindVisualModel(name) != nullptr;
}

void Wall::SetVisualModel(const std::string& name)
{
    if (!name.empty() && FindVisualModel(name) == nullptr)
    {
        Utility::ReportFatalError("Wall に知らないモデルの名前が指定されました: " + name);
    }
    m_VisualModel = name;
    // 外の森の岩は岩の材質（rockPS。色むら・ひび・苔・鉱物の粒・地面際の湿り）、枯れ木・倒木・茂みは樹皮の材質
    // （barkPS。木目に沿った溝・剥がれ・苔）で描いている（どちらもPBR）。配置を読み直して変わったときは、シェーダーを替え直している
    // 研究所の外壁（モデルのない箱に facade を付けた物）は、外壁の材質（facadePS。型枠の跡・Pコンの穴・雨の黒い垂れ跡・根元の苔）
    const NaturalSurface surface = name == "forest_rock" ? NaturalSurface::Rock
        : (name == "forest_tree_dead" || name == "forest_tree_fork" || name == "forest_fallen_log" || name == "forest_brush")
            ? NaturalSurface::Bark
            : (name.empty() && m_FacadeSurface) ? NaturalSurface::Facade : NaturalSurface::None;
    if (surface != m_NaturalSurface)
    {
        m_NaturalSurface = surface;
        m_Shader.CreateRequired("shader/litTextureVS.hlsl",
            surface == NaturalSurface::Rock ? "shader/rockPS.hlsl"
            : surface == NaturalSurface::Bark ? "shader/barkPS.hlsl"
            : surface == NaturalSurface::Facade ? "shader/facadePS.hlsl" : "shader/litTexturePS.hlsl");
    }
    RebuildVisualModel();
    UploadMaterial();
}

// 見た目の頂点を作り直している。モデルは、箱の一番長い向きへ回してから、箱の標準の大きさで割り、1×1×1の箱の空間にしている
// （ワールド行列の拡大で、置いた箱の大きさに伸びる）
void Wall::RebuildVisualModel()
{
    m_GlowIndexStart = 0;
    if (m_VisualModel.empty())
    {
        m_Vertices = m_BoxVertices;
        m_Indices = m_BoxIndices;
    }
    else
    {
        const VisualModel& model = *FindVisualModel(m_VisualModel);
        Matrix orientation = Matrix::Identity;
        Vector3 nominal = model.Size;
        if (model.FollowsLongestAxis)
        {
            const Vector3 size(std::abs(m_Scale.x), std::abs(m_Scale.y), std::abs(m_Scale.z));
            if (size.z > size.x && size.z >= size.y)
            {
                // モデルの x を z へ（y軸まわりに回す）
                orientation = Matrix(
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
                nominal = Vector3(model.Size.z, model.Size.y, model.Size.x);
            }
            else if (size.y > size.x && size.y > size.z)
            {
                // モデルの x を y へ（z軸まわりに回す）
                orientation = Matrix(
                    0.0f, 1.0f, 0.0f, 0.0f,
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
                nominal = Vector3(model.Size.y, model.Size.x, model.Size.z);
            }
        }
        // 樹皮の木目の向き：長い向きへ回すモデル（倒木）はモデルの x、それ以外（木・茂み）は上
        m_GrainAxis = Vector3::TransformNormal(
            model.FollowsLongestAxis ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 1.0f, 0.0f), orientation);
        // 光らない部位を先に、光る部位を後に並べ、光る部位の始まりを覚えている
        std::vector<PropModel::Part> parts;
        bool hasGlowPart = false;
        std::size_t unlitPartCount = 0;
        for (const bool glowing : { false, true })
        {
            for (const VisualModelPart& part : model.Parts)
            {
                if (part.Glows != glowing)
                {
                    continue;
                }
                hasGlowPart = hasGlowPart || part.Glows;
                unlitPartCount += part.Glows ? 0 : 1;
                const Color& surface = m_SurfaceMaterial.Diffuse;
                parts.push_back({ part.Material, part.UsesSurfaceColor
                    ? Color(surface.x, surface.y, surface.z, 1.0f) : part.PartColor });
            }
        }
        m_Vertices.clear();
        m_Indices.clear();
        const std::vector<PropModel::IndexRange> ranges =
            PropModel::Append(model.Path, parts, nominal, m_Vertices, m_Indices, orientation);
        if (hasGlowPart)
        {
            m_GlowIndexStart = ranges[unlitPartCount].Start;
        }
    }
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
}
