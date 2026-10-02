// ============================================================================
// シェーダーの役割: CPU側と共有する行列、ライト、マテリアルの定数バッファを定義します。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

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

struct VS_IN
{
    float4 pos : POSITION0;
	float4 nrm : NORMAL0;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    
};

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

cbuffer DebugViewBuffer : register(b7)
{
    int DebugViewMode;
    float WallDampStrength;
    float2 DebugViewPadding;
};

cbuffer LightBuffer : register(b3)
{
    LIGHT Light;
}

struct MATERIAL
{
    float4 Ambuent;
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float Shiness;
    bool TextureEnable;
    bool2 Dummy;
};
// ----------------------------------------------------------------------------
// タイルベースライティング
// 点光源はStructuredBufferで受け取り、本描画ではCompute Shaderが作った
// 「そのピクセルのタイルに影響する光源の番号リスト」だけを計算します。
// 反射・監視映像など別視点の描画では、全光源を順に計算します。
// ----------------------------------------------------------------------------
struct ENVIRONMENT_POINT_LIGHT
{
    float4 PositionRange;   // xyz = 位置, w = 影響半径（この距離で0になるよう減衰させる）
    float4 ColorIntensity;
};

cbuffer TiledLightBuffer : register(b6)
{
    uint PointLightCount;
    uint TiledLightMode;        // 0: 全光源、1: タイル別リスト
    uint LightTilesX;
    uint LightTilesY;
    float2 LightViewportOffset;
    float2 TiledLightPadding;
};

StructuredBuffer<ENVIRONMENT_POINT_LIGHT> g_PointLights : register(t10);
StructuredBuffer<uint> g_TileLightIndices : register(t11);
StructuredBuffer<uint> g_TileLightCounts : register(t12);

static const uint LIGHT_TILE_SIZE = 16;
static const uint MAX_LIGHTS_PER_TILE = 64;

// このピクセルで計算する光源の数と、タイル別リストの先頭位置を返します。
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

// デバッグ表示: タイルごとの光源数を色で表します（0=暗い青、1=緑、2=黄、4以上=赤）。
// タイルの境界線も重ね、画面がどう分割されているかを確認できるようにします。
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

ENVIRONMENT_POINT_LIGHT GetPixelLight(uint listOffset, uint lightNumber)
{
    if (TiledLightMode == 0)
    {
        return g_PointLights[lightNumber];
    }
    return g_PointLights[g_TileLightIndices[listOffset + lightNumber]];
}

cbuffer MaterialBuffer : register(b4)
{
  MATERIAL Material;
}

//UV座標移動行列
cbuffer TextureBuffer : register(b5)
{
    matrix matrixTex;
}

// ----------------------------------------------------------------------------
// デバッグ表示8: フルブライト（DebugUIの「Shader debug view」で選択）
// 照明・影・霧を無視し、元の色に「面がカメラを向いているか」だけの陰影を付けて表示します。
// 暗い場所の配置（アイテムが埋まっていないか、壁の文字の位置など）を確認するためのものです。
// DebugUIはDebug構成だけなので、Release版の見た目には影響しません。
// ----------------------------------------------------------------------------
static const int DEBUG_VIEW_FULLBRIGHT = 8;

float4 GetFullbrightColor(float3 baseColor, float3 viewNormal, float3 viewPosition)
{
    const float facing = abs(dot(normalize(viewNormal), normalize(-viewPosition)));
    return float4(baseColor * (0.60f + 0.40f * facing) * 1.35f + Material.Emission.rgb, 1.0f);
}