// ============================================================================
// シェーダーの役割: CPU側と共有する行列・ライト・マテリアルの定数バッファや、点光源の一覧を定義している。
// 定数バッファのスロットと入出力の形は、CPU側の定義（Renderer.hなど）と一致させている。
// ============================================================================

// ワールド・ビュー・射影の行列（b0〜b2。Renderer::SetWorldMatrixなどが転置して送っている）
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

// デバッグ表示の番号、2面の壁の湿り気の強さ、壁の古さ（b7）
cbuffer DebugViewBuffer : register(b7)
{
    int DebugViewMode;
    float WallDampStrength;
    // 壁の古さ（0〜1）。1面は1、2面は0（Renderer::SetWallWeathering）。
    float WallWeathering;
    float DebugViewPadding;
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
    // 16バイトにそろえるための詰め物
    bool Dummy;
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
    float2 TiledLightPadding;
};

// 点光源の一覧（t10）、タイルごとの光源の番号の並び（t11）、タイルごとの光源の数（t12）
StructuredBuffer<ENVIRONMENT_POINT_LIGHT> g_PointLights : register(t10);
StructuredBuffer<uint> g_TileLightIndices : register(t11);
StructuredBuffer<uint> g_TileLightCounts : register(t12);

// タイルの大きさと、1タイルに入れる光源の上限（TiledLighting.hと同じ値）
static const uint LIGHT_TILE_SIZE = 16;
static const uint MAX_LIGHTS_PER_TILE = 64;

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

// デバッグ表示: タイルごとの光源の数を色で表している（0=暗い青、1=緑、2=黄、4以上=赤）。
// タイルの境目の線も重ね、画面がどう分かれているかを確かめられるようにしている。
float4 GetLightTileHeatmap(float2 pixelPosition, uint lightCount)
{
    const float amount = saturate(float(lightCount) / 4.0f);
    float3 heat = lightCount == 0
        ? float3(0.02f, 0.03f, 0.10f)
        : lerp(float3(0.10f, 0.75f, 0.20f), float3(1.0f, 0.85f, 0.10f),
            saturate(amount * 2.0f - 0.5f));
    heat = lerp(heat, float3(1.0f, 0.12f, 0.08f), saturate(amount * 2.0f - 1.0f));

    const float2 localPixel = pixelPosition - LightViewportOffset;
    const float2 inTile = fmod(max(localPixel, 0.0f), float(LIGHT_TILE_SIZE));
    const bool onEdge = inTile.x < 1.0f || inTile.y < 1.0f;
    return float4(onEdge ? heat * 0.45f : heat, 1.0f);
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
static const int DEBUG_VIEW_FULLBRIGHT = 8;

float4 GetFullbrightColor(float3 baseColor, float3 viewNormal, float3 viewPosition)
{
    const float facing = abs(dot(normalize(viewNormal), normalize(-viewPosition)));
    return float4(baseColor * (0.60f + 0.40f * facing) * 1.35f + Material.Emission.rgb, 1.0f);
}