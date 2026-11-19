// ============================================================================
// シェーダーの役割: 描いた画面の明るさを測り、目が暗さ・明るさに慣れる「自動露出」の倍率を計算している。
// 主な技術: 1グループ256スレッドの並列の合計（グループ共有メモリでの木構造の足し算）、
//           明るさの対数の平均（幾何平均）、画面の中央を重くする測光、時間による順応
// ・画面全体を64x64の格子で読み、1スレッドが4x4点を受け持っている（全画素は読まず、4096点で平均している）。
// ・明るさの平均は対数で取り、懐中電灯のように一部だけ明るい画面でも、平均が引っ張られすぎないようにしている。
// ・慣れた明るさと倍率はGPUのバッファに置いたまま更新し、露出のシェーダーが直接読んでいる。
//   CPUへ読み戻さないため、GPUの処理を待たずに済む。
// ============================================================================

// 1グループのスレッドの数（16x16）と、1スレッドが読む点の数（4x4）
#define GROUP_SIZE 16
#define SAMPLES_PER_THREAD 4
#define GRID_SIZE (GROUP_SIZE * SAMPLES_PER_THREAD)
#define THREAD_COUNT (GROUP_SIZE * GROUP_SIZE)

// PostProcess.cppのAutoExposureParamsと同じ並び（b0）
cbuffer AutoExposureBuffer : register(b0)
{
    float DeltaTime;            // 前のフレームからの秒数
    float TargetLuminance;      // 目標にする明るさ（この明るさなら倍率1）
    float MinGain;              // 倍率の下限
    float MaxGain;              // 倍率の上限
    float DarkAdaptSpeed;       // 暗い所へ慣れる速さ（1秒あたり。小さいほどゆっくり）
    float LightAdaptSpeed;      // 明るい所へ慣れる速さ
    float Enabled;              // 0なら倍率を1に固定する（比較用）
    float ResetAdaptation;      // 1なら今の画面の明るさにすぐ慣れさせる（始めのフレーム）
};

Texture2D<float4> SceneTexture : register(t0);              // 画面効果を掛ける前の、描いた画面
RWStructuredBuffer<float> ExposureState : register(u0);     // [0] 慣れた明るさ（対数）、[1] 露出の倍率

// 輝度比をそのまま補正すると暗がりが明るくなりすぎるため、1未満の指数で圧縮する。
static const float AUTO_EXPOSURE_GAIN_EXPONENT = 0.35f;
// log(0) を避けるために足し、線形値へ戻すときに同じ値を引く。
static const float AUTO_EXPOSURE_LOG_OFFSET = 0.002f;

// 中央の輝度を優先しつつ、周辺にも測定の重みを残す。
static const float AUTO_EXPOSURE_CENTER_WEIGHT_RADIUS = 0.9f;
static const float AUTO_EXPOSURE_EDGE_WEIGHT_FLOOR = 0.15f;
// 輝度の重み（Rec. 709）
#define AUTO_EXPOSURE_LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)

// 集計時の重み、復元した輝度、露出の比率にそれぞれ下限を設ける。
static const float AUTO_EXPOSURE_MIN_ACCUMULATED_WEIGHT = 0.0001f;

// 真っ暗な画面でも、露出の比率とべき乗を計算できる範囲に収める。
static const float AUTO_EXPOSURE_MIN_LUMINANCE = 0.0001f;
static const float AUTO_EXPOSURE_MIN_RATIO = 0.0001f;

// 各スレッドの、重みを掛けた明るさの対数の合計と、重みの合計
groupshared float g_LogLuminance[THREAD_COUNT];
groupshared float g_Weight[THREAD_COUNT];

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 threadId : SV_GroupThreadID, uint threadIndex : SV_GroupIndex)
{
    uint width;
    uint height;
    SceneTexture.GetDimensions(width, height);
    const float2 sceneSize = float2(width, height);
    const float aspect = sceneSize.x / max(sceneSize.y, 1.0f);

    float logSum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (uint sy = 0; sy < SAMPLES_PER_THREAD; ++sy)
    {
        [unroll]
        for (uint sx = 0; sx < SAMPLES_PER_THREAD; ++sx)
        {
            // 格子の点の中心を、画面の画素の位置にしている
            const float2 grid = float2(
                threadId.x * SAMPLES_PER_THREAD + sx,
                threadId.y * SAMPLES_PER_THREAD + sy) + 0.5f;
            const float2 uv = grid / GRID_SIZE;
            const int2 pixel = min(int2(uv * sceneSize), int2(sceneSize) - 1);
            const float3 color = SceneTexture.Load(int3(pixel, 0)).rgb;
            const float luminance = dot(color, AUTO_EXPOSURE_LUMINANCE_WEIGHTS);

            // 画面の中央ほど重くしている（視線と懐中電灯が向く所を優先して測る）。端も少しは数えている
            const float2 centered = (uv - 0.5f) * float2(aspect, 1.0f);
            const float weight = max(1.0f - length(centered) / AUTO_EXPOSURE_CENTER_WEIGHT_RADIUS, AUTO_EXPOSURE_EDGE_WEIGHT_FLOOR);

            // 真っ黒（0）の対数は求められないため、小さな値を足している
            logSum += weight * log(luminance + AUTO_EXPOSURE_LOG_OFFSET);
            weightSum += weight;
        }
    }
    g_LogLuminance[threadIndex] = logSum;
    g_Weight[threadIndex] = weightSum;
    GroupMemoryBarrierWithGroupSync();

    // 半分ずつ足し合わせ、256個の値を8段階で1つにまとめている
    [unroll]
    for (uint stride = THREAD_COUNT / 2; stride > 0; stride >>= 1)
    {
        if (threadIndex < stride)
        {
            g_LogLuminance[threadIndex] += g_LogLuminance[threadIndex + stride];
            g_Weight[threadIndex] += g_Weight[threadIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex != 0)
    {
        return;
    }

    const float frameLogLuminance = g_LogLuminance[0] / max(g_Weight[0], AUTO_EXPOSURE_MIN_ACCUMULATED_WEIGHT);

    // 前のフレームまでに慣れた明るさから、今の明るさへ近づけている。
    // 明るくなるときは素早く、暗くなるときはゆっくり慣れる（人の目の慣れ方に近づけている）。
    float adaptedLogLuminance = frameLogLuminance;
    if (ResetAdaptation < 0.5f)
    {
        const float previous = ExposureState[0];
        const float speed = frameLogLuminance > previous ? LightAdaptSpeed : DarkAdaptSpeed;
        const float blend = 1.0f - exp(-DeltaTime * speed);
        adaptedLogLuminance = previous + (frameLogLuminance - previous) * blend;
    }

    // 慣れた明るさが目標より暗いほど倍率を上げている。0.35乗にして、明るさの差をそのまま埋めずに控えめにしている
    // （ホラーの暗さを残しつつ、真っ暗な場所でも少しだけ見えるようになる程度にとどめている）。
    const float adaptedLuminance = max(exp(adaptedLogLuminance) - AUTO_EXPOSURE_LOG_OFFSET, AUTO_EXPOSURE_MIN_LUMINANCE);
    const float luminanceRatio = max(TargetLuminance / adaptedLuminance, AUTO_EXPOSURE_MIN_RATIO);
    float gain = clamp(pow(luminanceRatio, AUTO_EXPOSURE_GAIN_EXPONENT), MinGain, MaxGain);
    if (Enabled < 0.5f)
    {
        gain = 1.0f;
    }

    ExposureState[0] = adaptedLogLuminance;
    ExposureState[1] = gain;
}
