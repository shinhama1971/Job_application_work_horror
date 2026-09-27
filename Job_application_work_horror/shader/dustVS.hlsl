// ============================================================================
// シェーダーの役割: GPUパーティクル（埃）のビルボードを頂点シェーダーだけで作ります。
// 頂点バッファは使わず、SV_VertexIDから「何番目の粒子の何番目の角か」を求め、
// 粒子のStructuredBufferを直接読みます（Geometry Shaderやインスタンス描画も使いません）。
// ============================================================================

#include "dustParticleCommon.hlsli"

StructuredBuffer<DustParticle> Particles : register(t0);

// 通常の行列バッファ(b0〜b2)は他の描画が書き換えるため、専用のスロットを使います。
cbuffer DustDrawBuffer : register(b11)
{
    matrix DustView;
    matrix DustProjection;
    float3 DustVolumeCenter;
    float DustTime;
    float3 DustVolumeExtent;
    float DustPixelWorldScale;  // 視点からの距離1あたり、1ピクセルが何ワールド単位か
};

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;          // -1〜1。円形の粒にするために使います
    float3 viewPos : TEXCOORD1;     // 懐中電灯の円錐判定用（ライトはカメラ位置にあります）
    float3 worldPos : TEXCOORD2;    // 点光源の距離減衰用
    float brightness : TEXCOORD3;   // 出現・消滅のフェードと瞬き
};

VS_OUT main(uint vertexId : SV_VertexID)
{
    // 0 ─ 1
    // │ ／│   インデックス {0,1,2, 2,1,3} で時計回りの2三角形になる並びです。
    // 2 ─ 3
    static const float2 Corners[4] =
    {
        float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
        float2(-1.0f, -1.0f), float2(1.0f, -1.0f)
    };

    const uint particleIndex = vertexId / 4;
    const uint corner = vertexId % 4;
    const DustParticle particle = Particles[particleIndex];

    VS_OUT output;
    output.uv = Corners[corner];
    output.worldPos = particle.Position;

    const float3 viewCenter = mul(float4(particle.Position, 1.0f), DustView).xyz;
    if (viewCenter.z < 1.0f)
    {
        // カメラの後ろやnearより手前の粒は、クリップ範囲外へ出して描きません。
        output.pos = float4(2.0f, 2.0f, 2.0f, 1.0f);
        output.viewPos = viewCenter;
        output.brightness = 0.0f;
        return output;
    }

    // 遠くの粒が1ピクセル未満になってちらつかないよう、最小サイズを保ちます。
    // 大きくした分は明るさを下げ、見た目の光の量が変わらないようにします。
    const float minimumSize = viewCenter.z * DustPixelWorldScale * 0.9f;
    const float size = max(particle.Size, minimumSize);
    const float sizeRatio = particle.Size / size;

    // ビューポート空間で右・上へずらすと、常にカメラを向くビルボードになります
    // （ビュー行列から右・上ベクトルを取り出してワールドでずらすのと同じ結果です）。
    const float3 viewPos = viewCenter + float3(output.uv * size, 0.0f);
    output.viewPos = viewPos;
    output.pos = mul(float4(viewPos, 1.0f), DustProjection);

    // 生まれた直後と寿命の直前、箱の端、カメラの目の前では薄くします。
    const float fadeIn = smoothstep(0.0f, 1.4f, particle.Age);
    const float fadeOut = 1.0f - smoothstep(
        particle.Lifetime - 1.8f, particle.Lifetime, particle.Age);
    const float3 boxLocal =
        abs(particle.Position - DustVolumeCenter) / max(DustVolumeExtent, 0.001f);
    const float edgeFade =
        1.0f - smoothstep(0.78f, 1.0f, max(boxLocal.x, max(boxLocal.y, boxLocal.z)));
    const float nearFade = smoothstep(2.5f, 9.0f, viewCenter.z);

    // 埃が空中で向きを変えて光を反射する、細かな瞬きです。
    const float twinkleSpeed = 1.1f + frac(particle.Phase * 7.31f) * 2.6f;
    const float twinkle = 0.45f + 0.55f *
        pow(saturate(sin(DustTime * twinkleSpeed + particle.Phase * 6.2831853f) * 0.5f + 0.5f), 3.0f);

    output.brightness = fadeIn * fadeOut * edgeFade * nearFade * twinkle *
        sizeRatio * sizeRatio;
    return output;
}
