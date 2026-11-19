// ============================================================================
// ファイルの役割: 外の森の草の、植え方（CPU）・揺らし方（Compute Shader）・描き方（区画ごとのカリングと LOD）。
// （説明は ForestGrass.h を参照）
// ============================================================================

#include "ForestGrass.h"
#include "MathUtility.h"
#include "shader/computeDispatchConfig.hlsli"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Ground.h"
#include "Renderer.h"
#include "TechDemo.h"
#include "Wall.h"
#include "utility.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace DirectX::SimpleMath;

namespace
{
    // shader/grassCS.hlsl の GrassSimulationBuffer と同じ並び（b0）
    struct SimulationParams
    {
        float Time;
        float DeltaTime;
        std::uint32_t BladeCount;
        float WindStrength;
        Vector2 WindDirection;
        Vector2 PlayerPosition;     // プレイヤーの足元（x・z）
        float PlayerRadius;         // 葉を押し分ける半径
        float PlayerStrength;       // 押し分ける強さ（0なら押さない）
        float Padding[2];
        Vector2 UnseenPosition;     // 草むらを通る「見えない何か」の足元（x・z。ForestEvents）
        float UnseenRadius;         // 押し倒す半径
        float UnseenStrength;       // 押し倒す強さ（0なら通っていない）
    };
    static_assert(sizeof(SimulationParams) == 64, "SimulationParams must match HLSL layout");

    // shader/grassVS.hlsl の GrassDrawBuffer と同じ並び（頂点シェーダーの b9。手の描画も b9 を毎回設定して使っている）
    struct DrawParams
    {
        std::uint32_t InstanceOffset; // この区画の最初の葉の番号（SV_InstanceID は描画ごとに0から始まるため）
        std::uint32_t Segments;       // 葉の節の数（遠いほど少ない）
        float WidthScale;             // 本数を減らした分、幅を広げて地面の見え方を保っている
        float FadeStart;              // この距離から、葉が地面へ縮んで消えていく
        Vector3 CameraPosition;
        float FadeEnd;
        std::uint32_t LodLevel;       // この区画の LOD（1 = 近い、2 = 中くらい、3 = 遠い）
        std::uint32_t ShowLodColors;  // 1 なら、LOD を色で表示する（技術デモ）
        float CameraBend;             // カメラの上の向きへ葉を曲げる強さ（授業資料 p.23。0 なら曲げない）
        float Padding;
    };
    static_assert(sizeof(DrawParams) == 48, "DrawParams must match HLSL layout");

    // 植える密度（1平方単位あたりの候補の数。群れの外の候補は捨てるので、実際はこの半分ほど）
    constexpr float CandidateDensity = 0.75f;
    // 区画の一辺の長さ
    constexpr float ChunkSize = 40.0f;
    // LOD：カメラから区画の一番近い所までの距離で、葉の細かさを3段に分けている。
    // NearDistance までは全部の葉を5節（曲がりがなめらか）、MiddleDistance までは本数を減らして3節、
    // その先は1節（三角形1枚）で描く。減らした本数の分は葉の幅を広げ（WidthScale）、遠くから見た草むらの濃さを保っている。
    // 区画の切り替わりで草が急に変わって見えないよう、本数は距離に合わせてなめらかに減らしている（Draw）
    constexpr float NearDistance = 140.0f;
    constexpr float MiddleDistance = 280.0f;
    // この距離から葉が地面へ縮み始め、FadeEnd で消える（森の闇と霧で、その先はほとんど見えない）。
    // FadeEnd より遠い区画は、描く処理ごと省いている
    constexpr float FadeStart = 330.0f;
    constexpr float FadeEnd = 410.0f;
    constexpr std::uint32_t GroupSize = SIGNAL_LOST_GRASS_GROUP_SIZE;

    // 研究所までの小道の曲がり：中心の x が、z に沿ってこの振れ幅・この長さの周期（2π倍）で左右に揺れる
    constexpr float PathSwing = 28.0f;
    constexpr float PathWavelength = 115.0f;
    // 小道の中心からこの距離までは生やさず、この距離で普通の濃さに戻る
    constexpr float PathBareWidth = 7.0f;
    constexpr float PathFullWidth = 17.0f;
    // 群れの濃さ（0〜1）がこの範囲で、生やす割合が0から1へ上がる。高い草になる濃さの範囲
    constexpr float ClumpKeepStart = 0.30f;
    constexpr float ClumpKeepEnd = 0.58f;
    constexpr float ClumpTallStart = 0.40f;
    constexpr float ClumpTallEnd = 0.80f;
    // 葉の高さ（最低＋高い群れで足す分）、幅（最低＋乱数の幅）、傾きの最大、硬さ（最低＋乱数の幅）
    constexpr float BladeMinHeight = 3.5f;
    constexpr float BladeTallExtra = 8.5f;
    constexpr float BladeMinWidth = 0.55f;
    constexpr float BladeWidthRange = 0.55f;
    constexpr float BladeMaxLean = 0.45f;
    constexpr float BladeMinStiffness = 14.0f;
    constexpr float BladeStiffnessRange = 22.0f;
    // 候補の数が上限を超えないよう密度を下げるとき、群れで捨てる分（約半分）を見込んで、この倍まで候補を作る
    constexpr float CandidateHeadroom = 1.6f;
    // 葉を生やさない所：枯れた茂みの枝の広がり（大きさに掛ける割合）、この高さより上にある物（屋根の上など）は数えない、
    // 細い物（木の幹。この太さ未満）の周りに空ける幅、そのほかの物の周りに空ける幅、水面の岸から空ける幅
    constexpr float BrushClearRate = 0.42f;
    constexpr float FootprintMaxHeight = 120.0f;
    constexpr float ThinTrunkWidth = 12.0f;
    constexpr float ThinTrunkMargin = 2.5f;
    constexpr float FootprintMargin = 1.0f;
    constexpr float PuddleShoreMargin = 3.0f;
    // 風の強さと向き、プレイヤーが葉を押し分ける半径、目がこの高さより地面から離れていたら押さない（デバッグの自由視点）
    constexpr float WindStrength = 0.55f;
    const Vector2 WindDirection(0.80f, 0.60f);
    constexpr float PlayerPushRadius = 9.0f;
    constexpr float PlayerPushMaxEyeHeight = 80.0f;
    // 1回のシミュレーションで進める時間の上限（止まった後の大きな飛びを防ぐ）
    constexpr float MaxSimulationSeconds = 0.1f;
    // 最初の大きさ（置き場所の place ForestGrass で上書きされる）と、床の高さ
    constexpr float DefaultFieldSize = 400.0f;
    constexpr float GroundY = -100.0f;
    // LOD：遠い区画で描く本数の割合、中くらいの区画で最大まで減らす割合、節の数（近い・中くらい・遠い）
    constexpr float FarBladeFraction = 0.22f;
    constexpr float MiddleBladeReduction = 0.6f;
    constexpr std::uint32_t NearSegments = 5;
    constexpr std::uint32_t MiddleSegments = 3;
    constexpr std::uint32_t FarSegments = 1;
    // 区画の中心から一番近い角までの距離（一辺に対する割合。√2 ÷ 2）
    constexpr float ChunkCornerRate = 0.71f;

    // 研究所までの小道の中心（forest_props.txt のコメントと同じ式。木や岩もこの式から離して置いている）
    float GetPathCenterX(float z)
    {
        return PathSwing * std::sin(z / PathWavelength);
    }

    // 植える場所のむら（群れ）に使う、なめらかな乱数（シェーダーの FastValueNoise と同じく、格子の点の乱数をなめらかにつないでいる）
    float Hash21(float x, float y)
    {
        std::uint32_t hash = static_cast<std::uint32_t>(static_cast<std::int32_t>(x)) * 374761393u +
            static_cast<std::uint32_t>(static_cast<std::int32_t>(y)) * 668265263u;
        hash = (hash ^ (hash >> 13)) * 1274126177u;
        hash ^= hash >> 16;
        // 下位24ビットを、1には届かない0〜1未満の値に変える。
        constexpr std::uint32_t HashFractionMask = 0x00FFFFFFu;
        constexpr std::uint32_t HashFractionDivisor = HashFractionMask + 1u;
        return static_cast<float>(hash & HashFractionMask) / static_cast<float>(HashFractionDivisor);
    }

    float ValueNoise(float x, float y)
    {
        const float cellX = std::floor(x);
        const float cellY = std::floor(y);
        float blendX = x - cellX;
        float blendY = y - cellY;
        blendX = MathUtility::SmoothCurve(blendX);
        blendY = MathUtility::SmoothCurve(blendY);
        const float a = Hash21(cellX, cellY);
        const float b = Hash21(cellX + 1.0f, cellY);
        const float c = Hash21(cellX, cellY + 1.0f);
        const float d = Hash21(cellX + 1.0f, cellY + 1.0f);
        const float top = a + (b - a) * blendX;
        const float bottom = c + (d - c) * blendX;
        return top + (bottom - top) * blendY;
    }


    // 上から見た、葉を生やさない長方形（当たり判定のある物の足元。回っている物は向きも使っている）
    struct Footprint
    {
        Vector2 Center;
        Vector2 HalfSize;
        float Cosine;
        float Sine;
    };

    bool IsInsideFootprint(const Footprint& footprint, float x, float z)
    {
        const float dx = x - footprint.Center.x;
        const float dz = z - footprint.Center.y;
        // 物の向き（yaw）の逆に回して、箱の中の座標にしている
        const float localX = dx * footprint.Cosine - dz * footprint.Sine;
        const float localZ = dx * footprint.Sine + dz * footprint.Cosine;
        return std::fabs(localX) < footprint.HalfSize.x && std::fabs(localZ) < footprint.HalfSize.y;
    }

    // --- 草の群れ：大きなむら（細かさ・ずらし・割合）と小さなむら（細かさ・ずらし・割合）
    constexpr float LargeClumpScale = 0.016f;
    const Vector2 LargeClumpOffset(3.1f, -7.4f);
    constexpr float LargeClumpWeight = 0.65f;
    constexpr float SmallClumpScale = 0.07f;
    const Vector2 SmallClumpOffset(-11.0f, 5.0f);
    constexpr float SmallClumpWeight = 0.35f;
    // 葉の高さのばらつき（最低の割合＋乱数の幅）と、小道の縁で短くする割合（最低＋離れるほど足す幅）
    constexpr float HeightRandomMin = 0.65f;
    constexpr float HeightRandomRange = 0.6f;
    constexpr float PathEdgeHeightMin = 0.55f;
    constexpr float PathEdgeHeightRange = 0.45f;
    // 枯れ具合：ノイズの細かさとずらし、強め方と下げる量、葉ごとの乱数の幅
    constexpr float DryNoiseScale = 0.03f;
    constexpr float DryNoiseOffsetX = 40.0f;
    constexpr float DryNoiseGain = 1.3f;
    constexpr float DryNoiseBias = 0.15f;
    constexpr float DryRandomRange = 0.3f;
    // 色：雨に濡れた夜の草。くすんだ緑と、枯れた黄土色（地面のテクスチャに近い明るさ）。葉ごとの明るさのばらつき（最低＋幅）
    const Vector3 GrassGreen(0.17f, 0.21f, 0.10f);
    const Vector3 GrassStraw(0.34f, 0.29f, 0.16f);
    constexpr float BrightnessRandomMin = 0.8f;
    constexpr float BrightnessRandomRange = 0.4f;
    // 草を描く定数のシェーダーの番号（grassVS.hlsl の b9）
    constexpr UINT GrassDrawBufferSlot = 9;
    // LOD の段階（grassVS.hlsl の色分けと同じ：1 = 近い、2 = 中くらい、3 = 遠い）
    constexpr std::uint32_t NearLodLevel = 1;
    constexpr std::uint32_t MiddleLodLevel = 2;
    constexpr std::uint32_t FarLodLevel = 3;
}

void ForestGrass::Init()
{
    // 同じ森で草の負荷だけを比べるための起動時の切り替え。通常は草を使う。
    wchar_t disabled[2]{};
    m_Disabled = GetEnvironmentVariableW(L"SIGNAL_LOST_DISABLE_FOREST_GRASS", disabled, 2) == 1 && disabled[0] == L'1';
    if (m_Disabled) return;
    m_Shader.CreateRequired("shader/grassVS.hlsl", "shader/grassPS.hlsl", true);
    if (!m_SimulationShader.Create("shader/grassCS.hlsl"))
    {
        utility::ReportFatalError("草の Compute Shader の作成に失敗しました。");
    }

    ID3D11Device* device = Renderer::GetDevice();
    D3D11_BUFFER_DESC paramsDesc{};
    paramsDesc.Usage = D3D11_USAGE_DEFAULT;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    paramsDesc.ByteWidth = sizeof(SimulationParams);
    if (FAILED(device->CreateBuffer(&paramsDesc, nullptr, m_SimulationParams.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("草の定数バッファの作成に失敗しました。");
    }
    paramsDesc.ByteWidth = sizeof(DrawParams);
    if (FAILED(device->CreateBuffer(&paramsDesc, nullptr, m_DrawParams.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("草の定数バッファの作成に失敗しました。");
    }

    // 葉は薄い1枚の帯なので、表と裏の両方を描いている（ほかの物は裏の面を描かない設定のまま）
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, m_TwoSidedRasterizer.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("草のラスタライザーの設定の作成に失敗しました。");
    }

    // 最初の大きさは、置き場所（place ForestGrass）で上書きされる
    m_Position = Vector3(0.0f, GroundY, 0.0f);
    m_Scale = Vector3(DefaultFieldSize, 1.0f, DefaultFieldSize);
    m_NeedsRebuild = true;
}

void ForestGrass::Rebuild()
{
    m_NeedsRebuild = true;
}

void ForestGrass::PlantBlades(std::vector<Blade>& blades, std::vector<BladeState>& states)
{
    const float groundY = m_Position.y;
    const float halfX = (std::max)(m_Scale.x, 1.0f) * 0.5f;
    const float halfZ = (std::max)(m_Scale.z, 1.0f) * 0.5f;
    const float minX = m_Position.x - halfX;
    const float minZ = m_Position.z - halfZ;

    // 候補が多すぎるときは、上限に収まるよう密度を下げている（群れで半分ほどに減る分を見込んでいる）
    float density = CandidateDensity;
    const float area = halfX * 2.0f * halfZ * 2.0f;
    density = (std::min)(density, static_cast<float>(MaxBlades) * CandidateHeadroom / area);
    const float spacing = 1.0f / std::sqrt(density);

    // 葉を生やさない所：当たり判定があり、足元が地面の近くにある物（木の幹・岩・倒木・研究所の壁と屋根の下・門柱・街灯の柱）
    std::vector<Footprint> footprints;
    for (const Wall* wall : Core::Game::GetInstance()->GetObjects<Wall>())
    {
        if (wall->IsDestroy())
        {
            continue;
        }
        const Vector3 position = wall->GetPosition();
        const Vector3 scale = wall->GetScale();
        // 枯れた茂み（forest_brush）は当たり判定がないが、草に埋もれて見えなくなるので、枝の広がりの内側には生やさない
        if (!wall->IsCollisionEnabled())
        {
            if (wall->GetVisualModel() == "forest_brush")
            {
                const float yaw = wall->GetRotation().y;
                footprints.push_back(Footprint{ Vector2(position.x, position.z),
                    Vector2(scale.x * BrushClearRate, scale.z * BrushClearRate), std::cos(yaw), std::sin(yaw) });
            }
            continue;
        }
        // 屋根の下（研究所の中）にも生やさないよう、低い所にある物は全部含めている
        if (position.y - scale.y * 0.5f > groundY + FootprintMaxHeight)
        {
            continue;
        }
        const float yaw = wall->GetRotation().y;
        // 木の幹は細いので、少し広めに空けている
        const float margin = (std::min)(scale.x, scale.z) < ThinTrunkWidth ? ThinTrunkMargin : FootprintMargin;
        footprints.push_back(Footprint{ Vector2(position.x, position.z),
            Vector2(scale.x * 0.5f + margin, scale.z * 0.5f + margin), std::cos(yaw), std::sin(yaw) });
    }

    // 生やさない所を、上から見た格子（一辺 FootprintCellSize）に振り分けている。
    // 葉の候補（数十万）ごとに全部の物（百数十）を調べると、Debug版の起動で数秒かかっていたため、
    // 候補のいるマスに重なる物だけを調べるようにしている（結果は全部を調べたときと同じ）
    constexpr float FootprintCellSize = 40.0f;
    const int footprintColumns = (std::max)(1, static_cast<int>(std::ceil(halfX * 2.0f / FootprintCellSize)));
    const int footprintRows = (std::max)(1, static_cast<int>(std::ceil(halfZ * 2.0f / FootprintCellSize)));
    std::vector<std::vector<std::uint32_t>> footprintCells(static_cast<std::size_t>(footprintColumns * footprintRows));
    for (std::size_t index = 0; index < footprints.size(); ++index)
    {
        const Footprint& footprint = footprints[index];
        // 回っている箱も含むよう、箱の対角線の半分を半径にしている
        const float radius = footprint.HalfSize.Length();
        const int column0 = std::clamp(static_cast<int>(std::floor((footprint.Center.x - radius - minX) / FootprintCellSize)), 0, footprintColumns - 1);
        const int column1 = std::clamp(static_cast<int>(std::floor((footprint.Center.x + radius - minX) / FootprintCellSize)), 0, footprintColumns - 1);
        const int row0 = std::clamp(static_cast<int>(std::floor((footprint.Center.y - radius - minZ) / FootprintCellSize)), 0, footprintRows - 1);
        const int row1 = std::clamp(static_cast<int>(std::floor((footprint.Center.y + radius - minZ) / FootprintCellSize)), 0, footprintRows - 1);
        for (int row = row0; row <= row1; ++row)
        {
            for (int column = column0; column <= column1; ++column)
            {
                footprintCells[static_cast<std::size_t>(row * footprintColumns + column)].push_back(static_cast<std::uint32_t>(index));
            }
        }
    }

    // 水たまりの位置は、地面（Ground）が作られたときに決まっている（床のシェーダーと同じ式）
    const Ground* ground = nullptr;
    for (const Ground* candidate : Core::Game::GetInstance()->GetObjects<Ground>())
    {
        if (!candidate->IsDestroy())
        {
            ground = candidate;
            break;
        }
    }

    // プレイ時も撮影時も、草を毎回同じ場所に置く。
    constexpr unsigned int ForestGrassPlacementSeed = 20261106u;
    std::mt19937 random(ForestGrassPlacementSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const int countX = static_cast<int>(halfX * 2.0f / spacing);
    const int countZ = static_cast<int>(halfZ * 2.0f / spacing);
    blades.clear();
    blades.reserve(static_cast<std::size_t>(countX) * countZ / 2);

    for (int iz = 0; iz < countZ; ++iz)
    {
        for (int ix = 0; ix < countX; ++ix)
        {
            // 格子の中で位置をずらし、並んで見えないようにしている
            const float x = minX + (ix + unit(random)) * spacing;
            const float z = minZ + (iz + unit(random)) * spacing;

            // 群れ：大きなむらと小さなむらを重ね、濃い所ほど高く生やしている（授業資料の「不均一な植え方」）
            const float clump = ValueNoise(x * LargeClumpScale + LargeClumpOffset.x, z * LargeClumpScale + LargeClumpOffset.y) * LargeClumpWeight +
                ValueNoise(x * SmallClumpScale + SmallClumpOffset.x, z * SmallClumpScale + SmallClumpOffset.y) * SmallClumpWeight;
            // 小道の上は踏まれて生えず、縁は短くまばらにしている
            const float pathDistance = std::fabs(x - GetPathCenterX(z));
            const float pathFade = MathUtility::SmoothStep(PathBareWidth, PathFullWidth, pathDistance);
            const float keep = MathUtility::SmoothStep(ClumpKeepStart, ClumpKeepEnd, clump) * pathFade;
            if (unit(random) > keep)
            {
                continue;
            }
            bool blocked = false;
            const int footprintColumn = std::clamp(static_cast<int>((x - minX) / FootprintCellSize), 0, footprintColumns - 1);
            const int footprintRow = std::clamp(static_cast<int>((z - minZ) / FootprintCellSize), 0, footprintRows - 1);
            for (const std::uint32_t index : footprintCells[static_cast<std::size_t>(footprintRow * footprintColumns + footprintColumn)])
            {
                if (IsInsideFootprint(footprints[index], x, z))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
            {
                continue;
            }
            // 見えている水面と、そのすぐ外の岸（水面のふちが草に隠れないよう、3単位）には生やさない
            if (ground != nullptr && ground->IsNearVisiblePuddle(Vector3(x, groundY, z), PuddleShoreMargin))
            {
                continue;
            }

            Blade blade{};
            blade.Root = Vector3(x, groundY, z);
            // 高さ：群れの濃い所ほど高い（膝より少し下まで）。小道の縁は短い
            const float tall = MathUtility::SmoothStep(ClumpTallStart, ClumpTallEnd, clump);
            blade.Height = (BladeMinHeight + tall * BladeTallExtra) * (HeightRandomMin + unit(random) * HeightRandomRange) *
                (PathEdgeHeightMin + pathFade * PathEdgeHeightRange);
            const float angle = unit(random) * DirectX::XM_2PI;
            blade.Facing = Vector2(std::cos(angle), std::sin(angle));
            blade.Width = BladeMinWidth + unit(random) * BladeWidthRange;
            blade.Lean = (unit(random) * 2.0f - 1.0f) * BladeMaxLean;
            // 色：雨に濡れた夜の草。枯れた黄土色と、くすんだ緑を混ぜている（照らしたとき、枯れ草が多い寂しい森に見えるように）
            const float dry = std::clamp(ValueNoise(x * DryNoiseScale + DryNoiseOffsetX, z * DryNoiseScale) * DryNoiseGain - DryNoiseBias +
                (unit(random) - 0.5f) * DryRandomRange, 0.0f, 1.0f);
            // 地面のテクスチャに近い明るさにしている（暗すぎると、懐中電灯で照らした地面の上で草が黒い影に見えるため）
            blade.Color = Vector3::Lerp(GrassGreen, GrassStraw, dry) * (BrightnessRandomMin + unit(random) * BrightnessRandomRange);
            blade.Stiffness = BladeMinStiffness + unit(random) * BladeStiffnessRange;
            blades.push_back(blade);
        }
    }

    // 区画ごとに並べ替えている。区画の中の順番はばらばらなので、先頭から何割かを描けば区画全体が均一に薄くなる
    const int chunksX = (std::max)(1, static_cast<int>(std::ceil(halfX * 2.0f / ChunkSize)));
    const int chunksZ = (std::max)(1, static_cast<int>(std::ceil(halfZ * 2.0f / ChunkSize)));
    auto chunkOf = [&](const Blade& blade)
    {
        const int cx = std::clamp(static_cast<int>((blade.Root.x - minX) / ChunkSize), 0, chunksX - 1);
        const int cz = std::clamp(static_cast<int>((blade.Root.z - minZ) / ChunkSize), 0, chunksZ - 1);
        return cz * chunksX + cx;
    };
    std::shuffle(blades.begin(), blades.end(), random);
    // 上限を超えたら、ばらばらに並べた後ろを捨てている（範囲の一部だけが抜けないように）
    if (blades.size() > MaxBlades)
    {
        blades.resize(MaxBlades);
    }
    // 区画の番号ごとに数えてから置き直す並べ替え（数え上げソート）。同じ区画の中の順番は保たれるので、
    // std::stable_sort と同じ並びになる。比べながら並べ替えるより速く、Debug版の起動で1秒以上かかっていたのを短くした
    {
        const std::size_t chunkCount = static_cast<std::size_t>(chunksX * chunksZ);
        std::vector<std::uint32_t> chunkIndices(blades.size());
        std::vector<std::uint32_t> offsets(chunkCount + 1, 0u);
        for (std::size_t i = 0; i < blades.size(); ++i)
        {
            chunkIndices[i] = static_cast<std::uint32_t>(chunkOf(blades[i]));
            ++offsets[chunkIndices[i] + 1];
        }
        for (std::size_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            offsets[chunk + 1] += offsets[chunk];
        }
        std::vector<Blade> sorted(blades.size());
        for (std::size_t i = 0; i < blades.size(); ++i)
        {
            sorted[offsets[chunkIndices[i]]++] = blades[i];
        }
        blades.swap(sorted);
    }

    m_Chunks.clear();
    std::size_t index = 0;
    while (index < blades.size())
    {
        const int chunk = chunkOf(blades[index]);
        Chunk entry;
        entry.First = static_cast<std::uint32_t>(index);
        float tallest = 0.0f;
        while (index < blades.size() && chunkOf(blades[index]) == chunk)
        {
            tallest = (std::max)(tallest, blades[index].Height);
            ++index;
        }
        entry.Count = static_cast<std::uint32_t>(index) - entry.First;
        const float centerX = minX + ((chunk % chunksX) + 0.5f) * ChunkSize;
        const float centerZ = minZ + ((chunk / chunksX) + 0.5f) * ChunkSize;
        entry.Center = Vector3(centerX, groundY + tallest * 0.5f, centerZ);
        // 区画の角までの距離に、葉が倒れて外へはみ出す分を足している
        entry.Radius = std::sqrt(ChunkSize * ChunkSize * 0.5f + tallest * tallest * 0.25f) + tallest;
        m_Chunks.push_back(entry);
    }

    // 最初の葉先：何もないときの傾きのまま（ばねが落ち着いた形）
    states.resize(blades.size());
    for (std::size_t i = 0; i < blades.size(); ++i)
    {
        const Blade& blade = blades[i];
        const Vector3 leanDirection(-blade.Facing.y, 0.0f, blade.Facing.x);
        Vector3 tip = Vector3::UnitY + leanDirection * blade.Lean;
        tip.Normalize();
        tip *= blade.Height;
        states[i].Tip = Vector4(tip.x, tip.y, tip.z, 0.0f);
        states[i].Velocity = Vector4::Zero;
    }
}

void ForestGrass::CreateBuffers(const std::vector<Blade>& blades, const std::vector<BladeState>& states)
{
    ReleaseBuffers();
    m_BladeCount = blades.size();
    if (blades.empty())
    {
        return;
    }
    ID3D11Device* device = Renderer::GetDevice();

    // 葉ごとに変わらない値：一度だけ送り、頂点シェーダーと Compute Shader が読む
    D3D11_BUFFER_DESC bladeDesc{};
    bladeDesc.ByteWidth = static_cast<UINT>(sizeof(Blade) * blades.size());
    bladeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bladeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bladeDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bladeDesc.StructureByteStride = sizeof(Blade);
    D3D11_SUBRESOURCE_DATA bladeData{ blades.data(), 0, 0 };
    // 毎フレーム変わる値：Compute Shader が書き（UAV）、頂点シェーダーが読む（SRV）
    D3D11_BUFFER_DESC stateDesc = bladeDesc;
    stateDesc.ByteWidth = static_cast<UINT>(sizeof(BladeState) * states.size());
    stateDesc.Usage = D3D11_USAGE_DEFAULT;
    stateDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    stateDesc.StructureByteStride = sizeof(BladeState);
    D3D11_SUBRESOURCE_DATA stateData{ states.data(), 0, 0 };

    if (FAILED(device->CreateBuffer(&bladeDesc, &bladeData, m_BladeBuffer.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(m_BladeBuffer.Get(), nullptr, m_BladeView.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateBuffer(&stateDesc, &stateData, m_StateBuffer.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(m_StateBuffer.Get(), nullptr, m_StateView.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateUnorderedAccessView(m_StateBuffer.Get(), nullptr, m_StateAccess.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("草のバッファの作成に失敗しました。");
    }
}

void ForestGrass::ReleaseBuffers()
{
    m_BladeView.Reset();
    m_BladeBuffer.Reset();
    m_StateAccess.Reset();
    m_StateView.Reset();
    m_StateBuffer.Reset();
    m_BladeCount = 0;
}

void ForestGrass::Update()
{
    if (m_Disabled) return;
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;
    m_PendingSeconds += deltaTime;
    // 置き場所が変わったら植え直している（Unity から書き出して読み直したときなど）
    if (m_Position != m_PlantedPosition || m_Scale != m_PlantedScale)
    {
        m_NeedsRebuild = true;
    }
}

void ForestGrass::RunGpuSimulation()
{
    if (m_Disabled) return;
    if (m_NeedsRebuild)
    {
        // 木や岩の当たり判定を見て植えるので、それらが置かれた後（最初の描画の前）に植えている
        std::vector<Blade> blades;
        std::vector<BladeState> states;
        PlantBlades(blades, states);
        CreateBuffers(blades, states);
        m_PlantedPosition = m_Position;
        m_PlantedScale = m_Scale;
        m_NeedsRebuild = false;
    }
    const float seconds = (std::min)(m_PendingSeconds, MaxSimulationSeconds);
    m_PendingSeconds = 0.0f;
    if (m_BladeCount == 0 || seconds <= 0.0f)
    {
        return;
    }

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    SimulationParams params{};
    params.Time = m_Time;
    params.DeltaTime = seconds;
    params.BladeCount = static_cast<std::uint32_t>(m_BladeCount);
    params.WindStrength = WindStrength;
    params.WindDirection = WindDirection;
    // プレイヤーの足元（目の高さから下ろした位置）。目が地面から離れすぎている（デバッグの自由視点など）ときは押さない
    const Vector3 eye = Core::Game::GetInstance()->GetCamera()->GetPosition();
    params.PlayerPosition = Vector2(eye.x, eye.z);
    params.PlayerRadius = PlayerPushRadius;
    params.PlayerStrength = eye.y - m_Position.y < PlayerPushMaxEyeHeight ? 1.0f : 0.0f;
    params.UnseenPosition = m_UnseenPosition;
    params.UnseenRadius = m_UnseenRadius;
    params.UnseenStrength = m_UnseenStrength;
    context->UpdateSubresource(m_SimulationParams.Get(), 0, nullptr, &params, 0, 0);

    // 頂点シェーダーが前の描画で読んでいた葉先のバッファを外してから、書き込み先にしている
    ID3D11ShaderResourceView* nullViews[2] = {};
    context->VSSetShaderResources(0, 2, nullViews);

    m_SimulationShader.SetGPU();
    ID3D11Buffer* constantBuffer = m_SimulationParams.Get();
    context->CSSetConstantBuffers(0, 1, &constantBuffer);
    ID3D11ShaderResourceView* bladeView = m_BladeView.Get();
    context->CSSetShaderResources(0, 1, &bladeView);
    ID3D11UnorderedAccessView* stateAccess = m_StateAccess.Get();
    context->CSSetUnorderedAccessViews(0, 1, &stateAccess, nullptr);
    context->Dispatch((params.BladeCount + GroupSize - 1) / GroupSize, 1, 1);

    ID3D11UnorderedAccessView* nullAccess = nullptr;
    ID3D11ShaderResourceView* nullView = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullAccess, nullptr);
    context->CSSetShaderResources(0, 1, &nullView);
    context->CSSetShader(nullptr, nullptr, 0);
}

void ForestGrass::Draw(Camera* camera)
{
    m_DrawnBlades = 0;
    m_DrawnChunks = 0;
    if (m_BladeCount == 0 || camera == nullptr)
    {
        return;
    }
    camera->SetCamera();
    Matrix world = Matrix::Identity;
    Renderer::SetWorldMatrix(&world);
    // 草はマテリアルの値を使わない（フルブライトの表示で、前の物の光る色が混ざらないよう消している）
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    Renderer::SetMaterial(material);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    m_Shader.SetGPU();
    // 頂点は頂点シェーダーが番号から作るので、頂点バッファも入力レイアウトも使わない
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11ShaderResourceView* views[2] = { m_BladeView.Get(), m_StateView.Get() };
    context->VSSetShaderResources(0, 2, views);
    ID3D11Buffer* drawBuffer = m_DrawParams.Get();
    context->VSSetConstantBuffers(GrassDrawBufferSlot, 1, &drawBuffer);

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
    context->RSGetState(previousRasterizer.GetAddressOf());
    context->RSSetState(m_TwoSidedRasterizer.Get());

    // 草のLODの描画設定：0 = LODなし（全部の葉を5節で）、1 = LODあり（普段）、2 = LODを色で表示。
    // 「草をカメラの上方向へ曲げる」：上から見下ろしたとき、葉の面を見せる（授業資料 p.23）
    constexpr auto lodMode = Tools::TechDemo::DefaultGrassLodMode;
    const float cameraBend = Tools::TechDemo::UseGrassCameraBend ? 1.0f : 0.0f;
    const Vector3 eye = camera->GetPosition();
    for (const Chunk& chunk : m_Chunks)
    {
        const float distance = Vector2::Distance(Vector2(eye.x, eye.z), Vector2(chunk.Center.x, chunk.Center.z));
        const float nearestDistance = (std::max)(0.0f, distance - ChunkSize * ChunkCornerRate);
        if (nearestDistance > FadeEnd || !camera->IsSphereVisible(chunk.Center, chunk.Radius))
        {
            continue;
        }
        // LOD：区画のいちばん近い所までの距離で、描く本数の割合と葉の節の数を決めている
        float fraction = 1.0f;
        std::uint32_t segments = NearSegments;
        std::uint32_t lodLevel = NearLodLevel;
        if constexpr (lodMode == Tools::TechDemo::GrassLodMode::Off)
        {
            // LODなし：遠くまで全部の葉を一番細かく描く（比べるための方式）
        }
        else if (nearestDistance > MiddleDistance)
        {
            fraction = FarBladeFraction;
            segments = FarSegments;
            lodLevel = FarLodLevel;
        }
        else if (nearestDistance > NearDistance)
        {
            fraction = 1.0f - MiddleBladeReduction * MathUtility::SmoothStep(NearDistance, MiddleDistance, nearestDistance);
            segments = MiddleSegments;
            lodLevel = MiddleLodLevel;
        }
        const std::uint32_t count = (std::max)(1u, static_cast<std::uint32_t>(chunk.Count * fraction));

        DrawParams params{};
        params.InstanceOffset = chunk.First;
        params.Segments = segments;
        // 本数を減らした分だけ幅を広げ、遠くの草むらが急に薄くならないようにしている
        params.WidthScale = std::sqrt(static_cast<float>(chunk.Count) / static_cast<float>(count));
        params.FadeStart = FadeStart;
        params.CameraPosition = eye;
        params.FadeEnd = FadeEnd;
        params.LodLevel = lodLevel;
        params.ShowLodColors = lodMode == Tools::TechDemo::GrassLodMode::DebugColors ? 1u : 0u;
        params.CameraBend = cameraBend;
        context->UpdateSubresource(m_DrawParams.Get(), 0, nullptr, &params, 0, 0);
        // 葉1本 = 1インスタンス。節が n なら、左右の頂点 2n 個と、とがった先の1個の三角形の帯
        context->DrawInstanced(segments * 2 + 1, count, 0, 0);
        m_DrawnBlades += count;
        ++m_DrawnChunks;
    }

    context->RSSetState(previousRasterizer.Get());
    ID3D11ShaderResourceView* nullViews[2] = {};
    context->VSSetShaderResources(0, 2, nullViews);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ForestGrass::Uninit()
{
    ReleaseBuffers();
    m_SimulationShader.Uninit();
    m_SimulationParams.Reset();
    m_DrawParams.Reset();
    m_TwoSidedRasterizer.Reset();
    m_Chunks.clear();
}
