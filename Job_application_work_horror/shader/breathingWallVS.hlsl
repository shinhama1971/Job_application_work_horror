// ============================================================================
// シェーダーの役割: 「呼吸する壁」の頂点シェーダー。細かく分けた壁の1面を、息を吸って吐くようにゆっくり膨らませ、
// 懐中電灯の光が当たっている所だけを平らに戻している（照らすと止まり、光を外すとまた動き出す）。
// 出力は litTextureVS と同じ形なので、ピクセルシェーダーは普段の壁（litTexturePS）をそのまま使っている。
// 主な技術: 頂点シェーダーでの変位（法線の向きへ押し出す）、変位後の法線を差分で求め直す計算、
//           懐中電灯の円錐（LightBuffer）による頂点ごとの止まり具合
// ============================================================================

#include "renderingConfig.hlsli"
#include "common.hlsl"

// 影の計算に使う、ライトのビュー×射影の行列（ShadowMap.cppと同じ並び）
cbuffer ShadowBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_FLASHLIGHT_SHADOW_BUFFER_SLOT))
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// 呼吸の値（Wall::Draw が b9 に入れている。Wall.h の BREATHING_WALL_BUFFER と同じ並び）
cbuffer BreathingWallBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT))
{
    // x = 時間（秒）、y = 一番大きく膨らむ量（ワールドの単位）、z = 動かす面の向き（箱の +z の面なら1、-z の面なら-1）、w = 縁を固定する幅（ワールドの単位）
    float4 BreathParams;
    // xyz = 懐中電灯の光が出る所（カメラの位置）
    float4 BreathFlashlightOrigin;
}

// ピクセルシェーダーへ渡す値（litTextureVS と同じ並び）
struct LIT_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
};

static const float BREATH_PI = 3.14159265f;
// 1回の呼吸（吸って吐く）の秒数（C++ の息の音と同じ値）
static const float BREATH_CYCLE_SECONDS = SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS;
// 膨らみのうち、壁全体がそろって膨らむ割合と、場所ごとにむらのある膨らみの割合
static const float BREATH_UNIFORM_SHARE = 0.25f;
static const float BREATH_LUMP_SHARE = 0.75f;
// むらを強める指数（大きいほど、膨らみが数か所にまとまる）
static const float BREATH_LUMP_SHARPNESS = 2.0f;
// むらの細かさ（ワールドの単位あたり）と、ゆっくり動く速さ
static const float2 BREATH_LUMP_SCALE_A = float2(0.045f, 0.085f);
static const float2 BREATH_LUMP_SCALE_B = float2(0.105f, 0.16f);
static const float2 BREATH_LUMP_SPEED_A = float2(0.31f, -0.23f);
static const float2 BREATH_LUMP_SPEED_B = float2(-0.47f, 0.37f);
static const float BREATH_LUMP_B_WEIGHT = 0.5f;
// 変位後の法線を求めるときに、隣を調べる距離（ワールドの単位）
static const float BREATH_NORMAL_STEP = 1.5f;
// 懐中電灯で止まる範囲：この距離までは光が当たっていれば止まり、それより遠いと少しずつ止まらなくなる
static const float BREATH_LIGHT_REACH = 260.0f;

// 箱の面の上の位置（-0.5〜0.5）から、その所の膨らむ量を求めている
float BreathDisplacement(float2 local, float3 worldPosition, float3 tangent, float faceWidth, float faceHeight)
{
    // 縁（隣の壁・床・天井とつながる所）は動かさない。縁からの距離で、なめらかに動きを始めている
    const float edgeWidth = max(BreathParams.w, 0.001f);
    const float edgeX = (0.5f - abs(local.x)) * faceWidth;
    const float edgeY = (0.5f - abs(local.y)) * faceHeight;
    const float pinned = smoothstep(0.0f, edgeWidth, edgeX) * smoothstep(0.0f, edgeWidth, edgeY);

    // 吸って吐く：0（吐き切った平ら）〜1（吸い切って膨らんだ）を、ゆっくり行き来している
    const float time = BreathParams.x;
    const float breath = 0.5f - 0.5f * cos(time * (2.0f * BREATH_PI / BREATH_CYCLE_SECONDS));

    // 場所ごとのむら：壁の中で何かが押しているような、ゆっくり動く2つの膨らみを重ねている
    const float2 plane = float2(dot(worldPosition, tangent), worldPosition.y);
    const float2 phaseA = plane * BREATH_LUMP_SCALE_A + BREATH_LUMP_SPEED_A * time;
    const float2 phaseB = plane * BREATH_LUMP_SCALE_B + BREATH_LUMP_SPEED_B * time;
    const float lumps = saturate(0.5f + 0.5f * (sin(phaseA.x) * sin(phaseA.y) +
        BREATH_LUMP_B_WEIGHT * sin(phaseB.x) * sin(phaseB.y)) / (1.0f + BREATH_LUMP_B_WEIGHT));

    // 懐中電灯の円錐の中は止める（光の芯ほど平ら）
    float still = 0.0f;
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float3 toPoint = worldPosition - BreathFlashlightOrigin.xyz;
        const float distanceToPoint = length(toPoint);
        const float coneDot = dot(toPoint / max(distanceToPoint, 0.001f), normalize(Light.Direction.xyz));
        const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
        still = cone * saturate(1.0f - distanceToPoint / BREATH_LIGHT_REACH + 0.5f);
    }

    const float shapedLumps = pow(lumps, BREATH_LUMP_SHARPNESS) * 2.0f;
    return BreathParams.y * pinned * breath * (BREATH_UNIFORM_SHARE + BREATH_LUMP_SHARE * shapedLumps) * (1.0f - saturate(still));
}

// 箱の面の上の位置（-0.5〜0.5）を、膨らませた後のワールドの位置にしている
float3 BreathWorldPosition(float2 local, float faceZ, float3 faceNormal, float3 tangent, float faceWidth, float faceHeight)
{
    const float3 worldPosition = mul(float4(local, faceZ, 1.0f), World).xyz;
    return worldPosition + faceNormal * BreathDisplacement(local, worldPosition, tangent, faceWidth, faceHeight);
}

// 頂点を画面の座標へ変換している（動かす面の頂点だけを膨らませている）
LIT_PS_IN main(in VS_IN input)
{
    LIT_PS_IN output;

    float3 worldPosition = mul(input.pos, World).xyz;
    float3 worldNormal = normalize(mul(input.nrm.xyz, (float3x3)World));

    const float faceSign = BreathParams.z;
    if (input.nrm.z * faceSign > 0.5f)
    {
        // 面の幅と高さ（箱の x・y の大きさ）と、面に沿った横の向き
        const float faceWidth = length(World[0].xyz);
        const float faceHeight = length(World[1].xyz);
        const float3 tangent = World[0].xyz / max(faceWidth, 0.001f);
        const float faceZ = input.pos.z;
        const float2 local = input.pos.xy;

        worldPosition = BreathWorldPosition(local, faceZ, worldNormal, tangent, faceWidth, faceHeight);
        // 横と縦に少しずらした所も膨らませ、その3点から、膨らんだ面の法線を求め直している
        const float2 stepX = float2(BREATH_NORMAL_STEP / max(faceWidth, 0.001f), 0.0f);
        const float2 stepY = float2(0.0f, BREATH_NORMAL_STEP / max(faceHeight, 0.001f));
        const float3 alongX = BreathWorldPosition(local + stepX, faceZ, worldNormal, tangent, faceWidth, faceHeight) - worldPosition;
        const float3 alongY = BreathWorldPosition(local + stepY, faceZ, worldNormal, tangent, faceWidth, faceHeight) - worldPosition;
        float3 bentNormal = normalize(cross(alongX, alongY));
        // 外積の向きは面の向きで逆になるので、元の法線と同じ側へそろえている
        bentNormal *= sign(dot(bentNormal, worldNormal) + 0.0001f);
        worldNormal = bentNormal;
    }

    const float4 world4 = float4(worldPosition, 1.0f);
    const float4 viewPos = mul(world4, View);
    output.pos = mul(viewPos, Projection);
    output.depth = viewPos.z;
    output.viewPos = viewPos.xyz;
    output.worldPos = worldPosition;
    output.worldNormal = worldNormal;
    output.shadowPos = mul(world4, ShadowViewProjection);
    output.viewNormal = normalize(mul(worldNormal, (float3x3)View));

    // 頂点の色に、マテリアルの透明度を掛けている
    output.col = input.col;
    output.col.a *= Material.Diffuse.a;
    output.tex = input.tex;

    return output;
}
