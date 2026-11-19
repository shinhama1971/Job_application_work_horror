// ============================================================================
// シェーダーの役割: CPU側と共有する行列・ライト・マテリアルの定数バッファや、点光源の一覧を定義している。
// 定数バッファのスロットと入出力の形は、CPU側の定義（Renderer.hなど）と一致させている。
// ============================================================================

// ワールド・ビュー・射影の行列（b0〜b2。Renderer::SetWorldMatrixなどが転置して送っている）
// 既存の床・霧・濡れの高さをそろえるための基準。部屋ごとの遮蔽には RoomFloorY を使う。
static const float SCENE_BASE_FLOOR_Y = -100.0f;

cbuffer WorldBuffer : register(b0)
{
	matrix World;
}
cbuffer ViewBuffer : register(b1)
{
	matrix View;
}
cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

// 頂点の入力（VERTEX_3Dと同じ並び。位置・法線・色・UV）
struct VS_IN
{
    float4 pos : POSITION0;
	float4 nrm : NORMAL0;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    
};

// 照明を使わない描画で、頂点シェーダーからピクセルシェーダーへ渡す値
struct PS_IN
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
	float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
};

// 懐中電灯と環境光（Renderer.hのLIGHTと同じ並び）
struct LIGHT
{
    bool Enable;
    bool FlashlightEnabled;
    float Intensity;
    float Range;
    float4 Direction;
    float4 Diffuse;
    float4 Ambient;
    float4 SpotParams;
};

// デバッグ表示の番号、2面の壁の湿り気の強さ、壁の古さ、周回の深さ（b7）
cbuffer DebugViewBuffer : register(b7)
{
    int DebugViewMode;
    float WallDampStrength;
    // 壁の古さ（0〜1）と、壁の古さに混ぜるホラーの跡（血の手形・引っかき傷・血の垂れ跡）の濃さ（Renderer::SetWallWeathering）。
    // 2面は異変として手形や傷を出すため、決まった跡は描かない（0）。
    float WallWeathering;
    float HorrorMarksStrength;
    // 2面の周回の深さ（0〜1。Renderer::SetLoopDepth）。周回が進むほど、色調が赤黒く沈み、霧が濃くなる（horrorGrade.hlsli）
    float LoopDepth;
    // 外の森の雨で、岩・樹皮・研究所の外壁が濡れる強さ（0〜1）と、水の筋を流す時間（秒）（forestWetness.hlsli。Renderer::SetRainWetness）
    float RainWetness;
    float RainTime;
    float RainWetnessPadding;
};

// 懐中電灯と環境光の定数バッファ（b3）
cbuffer LightBuffer : register(b3)
{
    LIGHT Light;
}

// マテリアル（Renderer.hのMATERIALと同じ並び。Ambient・Shininessの綴りはシェーダー側だけ違っている）
struct MATERIAL
{
    float4 Ambuent;
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float Shiness;
    bool TextureEnable;
    bool WeatheringSurface;   // 建物の壁ならtrue（壁の古さを描く面）
    // 崩れて現れる・消える度合い（0 = 元の形、1 = 消えた。Wall::DissolveIn / DissolveOut）
    float Dissolve;
    // PBR（金属・粗さの方式。pbrLighting.hlsli）。金属らしさ0〜1と、粗さ（0は指定なし。光沢の強さから換算する）
    float Metallic;
    float Roughness;
    float2 PbrPadding;
};
// ----------------------------------------------------------------------------
// タイルベースライティング
// 点光源はStructuredBufferで受け取り、本描画では、Compute Shaderが作った
// 「その画素のタイルに影響する光源の番号のリスト」だけを計算している。
// 反射・監視映像など別の視点の描画では、全部の光源を順に計算している。
// ----------------------------------------------------------------------------
struct ENVIRONMENT_POINT_LIGHT
{
    float4 PositionRange;   // xyz = 位置、w = 影響の半径（この距離で0になるよう弱めている）
    // rgb = 色、a = 強さ
    float4 ColorIntensity;
};

// TiledLighting.cppのShadingParamsと同じ並び（b6）
cbuffer TiledLightBuffer : register(b6)
{
    uint PointLightCount;
    uint TiledLightMode;        // 0: 全部の光源、1: タイルごとのリスト
    uint LightTilesX;
    uint LightTilesY;
    float2 LightViewportOffset;
    // 1なら、画面空間の環境遮蔽（t15。screenAmbientOcclusion.hlsli）を使う（本描画だけ）
    float ScreenAmbientOcclusionEnabled;
    float ScreenLightShaftsEnabled;
};

// 点光源の一覧（t10）、タイルごとの光源の番号の並び（t11）、タイルごとの光源の数（t12）
StructuredBuffer<ENVIRONMENT_POINT_LIGHT> g_PointLights : register(t10);
StructuredBuffer<uint> g_TileLightIndices : register(t11);
StructuredBuffer<uint> g_TileLightCounts : register(t12);
// 点光源ごとの、光の届く範囲に重なる建物の壁の印（1ビット = 1枚。光源が付いている壁は除く。TiledLighting が CPU で求めている）。
// 壁を抜けて隣の部屋を照らさない判定（roomOcclusion.hlsli の GetRoomLightVisibility）で、調べる壁を絞るのに使う
StructuredBuffer<uint> g_PointLightWallMasks : register(t19);

// タイルの大きさと、1タイルに入れる光源の上限（TiledLighting.hと同じ値）
#include "tiledLightingConfig.hlsli"
#include "renderingConfig.hlsli"

// 天井照明の下向きの配光が広がる範囲。壁・床・手元モデルの境界を揃える。
static const float FIXTURE_BEAM_DOT_START = 0.04f;
static const float FIXTURE_BEAM_DOT_END = 0.72f;
static const uint LIGHT_TILE_SIZE = SIGNAL_LOST_LIGHT_TILE_SIZE;
static const uint MAX_LIGHTS_PER_TILE = SIGNAL_LOST_MAX_LIGHTS_PER_TILE;

// この画素で計算する光源の数と、タイルごとのリストの先頭の位置を返している。
uint GetPixelLightCount(float2 pixelPosition, out uint listOffset)
{
    listOffset = 0;
    if (TiledLightMode == 0)
    {
        return PointLightCount;
    }

    const float2 localPixel = max(pixelPosition - LightViewportOffset, 0.0f);
    const uint2 tile = min(
        uint2(localPixel) / LIGHT_TILE_SIZE,
        uint2(LightTilesX - 1, LightTilesY - 1));
    const uint tileIndex = tile.y * LightTilesX + tile.x;
    listOffset = tileIndex * MAX_LIGHTS_PER_TILE;
    return g_TileLightCounts[tileIndex];
}

// 光源の数の表示：赤になる数、色（0 = 暗い青、少ない = 緑、中くらい = 黄、多い = 赤）、タイルの境目の線の暗さ
#define HEATMAP_MAX_LIGHTS 4.0f
#define HEATMAP_NONE_COLOR float3(0.02f, 0.03f, 0.10f)
#define HEATMAP_LOW_COLOR float3(0.10f, 0.75f, 0.20f)
#define HEATMAP_MID_COLOR float3(1.0f, 0.85f, 0.10f)
#define HEATMAP_HIGH_COLOR float3(1.0f, 0.12f, 0.08f)
#define HEATMAP_EDGE_SHADE 0.45f

// デバッグ表示: タイルごとの光源の数を色で表している（0=暗い青、1=緑、2=黄、4以上=赤）。
// タイルの境目の線も重ね、画面がどう分かれているかを確かめられるようにしている。
float4 GetLightTileHeatmap(float2 pixelPosition, uint lightCount)
{
    const float amount = saturate(float(lightCount) / HEATMAP_MAX_LIGHTS);
    float3 heat = lightCount == 0
        ? HEATMAP_NONE_COLOR
        : lerp(HEATMAP_LOW_COLOR, HEATMAP_MID_COLOR,
            saturate(amount * 2.0f - 0.5f));
    heat = lerp(heat, HEATMAP_HIGH_COLOR, saturate(amount * 2.0f - 1.0f));

    const float2 localPixel = pixelPosition - LightViewportOffset;
    const float2 inTile = fmod(max(localPixel, 0.0f), float(LIGHT_TILE_SIZE));
    const bool onEdge = inTile.x < 1.0f || inTile.y < 1.0f;
    return float4(onEdge ? heat * HEATMAP_EDGE_SHADE : heat, 1.0f);
}

// その画素で計算するlightNumber番目の光源の、光源の一覧（t10）での番号を返している
uint GetPixelLightIndex(uint listOffset, uint lightNumber)
{
    return TiledLightMode == 0 ? lightNumber : g_TileLightIndices[listOffset + lightNumber];
}

// その画素で計算するlightNumber番目の光源の、光の届く範囲に重なる壁の印
uint GetPixelLightWallMask(uint listOffset, uint lightNumber)
{
    return g_PointLightWallMasks[GetPixelLightIndex(listOffset, lightNumber)];
}

// その画素で計算するlightNumber番目の光源を返している（タイルのリストを使うときは番号を読み替えている）
ENVIRONMENT_POINT_LIGHT GetPixelLight(uint listOffset, uint lightNumber)
{
    if (TiledLightMode == 0)
    {
        return g_PointLights[lightNumber];
    }
    return g_PointLights[g_TileLightIndices[listOffset + lightNumber]];
}

// マテリアルの定数バッファ（b4）
cbuffer MaterialBuffer : register(b4)
{
  MATERIAL Material;
}

// UV座標を動かす行列（b5）
cbuffer TextureBuffer : register(b5)
{
    matrix matrixTex;
}

// ----------------------------------------------------------------------------
// デバッグ表示8: フルブライト（DebugUIの「Shader debug view」で選んでいる）
// 照明・影・霧を無視し、元の色に「面がカメラを向いているか」だけの陰影を付けて表示している。
// 暗い場所の配置（アイテムが埋まっていないか、壁の文字の位置など）を確かめるためのもの。
// DebugUIはDebug構成だけなので、Release版の見た目には影響しない。
// ----------------------------------------------------------------------------
// デバッグ表示の番号（DebugUI.cpp の「Shader debug view」の並びと同じ）。
// 1 = 法線、2 = 懐中電灯の影、3 = 照明だけ、4 = 水たまりの濃さとメッシュの細かさ（床）、5 = 反射の画像（床）、
// 6 = 壁の湿り気（壁）、7 = タイルごとの光源の数。
// static const int にすると wetFloorPS の FXC の最適化が変わってしまうため、ここは #define にしている
#define DEBUG_VIEW_NORMAL SIGNAL_LOST_DEBUG_VIEW_NORMAL
#define DEBUG_VIEW_FLASHLIGHT_SHADOW SIGNAL_LOST_DEBUG_VIEW_FLASHLIGHT_SHADOW
#define DEBUG_VIEW_LIGHTING SIGNAL_LOST_DEBUG_VIEW_LIGHTING
#define DEBUG_VIEW_PUDDLE SIGNAL_LOST_DEBUG_VIEW_PUDDLE
#define DEBUG_VIEW_REFLECTION SIGNAL_LOST_DEBUG_VIEW_REFLECTION
#define DEBUG_VIEW_WALL_DAMP SIGNAL_LOST_DEBUG_VIEW_WALL_DAMP
#define DEBUG_VIEW_LIGHT_TILES SIGNAL_LOST_DEBUG_VIEW_LIGHT_TILES
// 照明だけの表示で、明るすぎないよう掛ける割合
#define DEBUG_VIEW_LIGHTING_SCALE 0.5f
static const int DEBUG_VIEW_FULLBRIGHT = SIGNAL_LOST_DEBUG_VIEW_FULLBRIGHT;
// PBRの材質を色で見る表示（赤 = 金属らしさ、緑 = 粗さ、青 = 濡れ具合。壁・扉・小物だけ。床は黒）
static const int DEBUG_VIEW_PBR_MATERIAL = SIGNAL_LOST_DEBUG_VIEW_PBR_MATERIAL;
// 画面空間の環境遮蔽（SSAO）だけを白黒で見る表示（白 = 遮る物なし、黒 = 囲まれている）
static const int DEBUG_VIEW_AMBIENT_OCCLUSION = SIGNAL_LOST_DEBUG_VIEW_AMBIENT_OCCLUSION;
// 室内を明るくし、水たまりの反射を残す表示と、森の草の LOD を色分けする表示。
static const int DEBUG_VIEW_FULLBRIGHT_REFLECTION = SIGNAL_LOST_DEBUG_VIEW_FULLBRIGHT_REFLECTION;
static const int DEBUG_VIEW_GRASS_LOD = SIGNAL_LOST_DEBUG_VIEW_GRASS_LOD;

bool IsFullbrightDebugView()
{
    return DebugViewMode == DEBUG_VIEW_FULLBRIGHT ||
        DebugViewMode == DEBUG_VIEW_FULLBRIGHT_REFLECTION ||
        DebugViewMode == DEBUG_VIEW_GRASS_LOD;
}

// 明るさを揃えたデバッグ表示：最低の明るさ、向きで足す幅、全体の明るさ（一人称の手と懐中電灯でも使う）、反射の確認で暗部を持ち上げる量
#define FULLBRIGHT_BASE 0.60f
#define FULLBRIGHT_FACING 0.40f
#define FULLBRIGHT_GAIN 1.35f
#define FULLBRIGHT_REFLECTION_LIFT 0.16f

float3 GetFullbrightSurfaceColor(float3 baseColor, float facing)
{
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        // 暗い天井板や扉の模様を残したまま、暗部の底を持ち上げる。
        baseColor += FULLBRIGHT_REFLECTION_LIFT * (1.0f - saturate(baseColor));
    }
    return baseColor * (FULLBRIGHT_BASE + FULLBRIGHT_FACING * facing) * FULLBRIGHT_GAIN;
}

float4 GetFullbrightColor(float3 baseColor, float3 viewNormal, float3 viewPosition)
{
    const float facing = abs(dot(normalize(viewNormal), normalize(-viewPosition)));
    return float4(GetFullbrightSurfaceColor(baseColor, facing) + Material.Emission.rgb, 1.0f);
}