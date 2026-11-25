// ============================================================================
// ファイルの役割: 1面の濁流の波頭から飛ぶしぶき（頂点シェーダー）。
// 主な技術: 頂点バッファを使わない粒子（頂点番号から乱数を作り、粒ごとの飛び方を決める）、放物線の運動、
//           速さの向きに伸ばしたビルボード（速く飛ぶ粒ほど細長い筋に見える）
//
// ・粒は波頭のてっぺん（corridorFloodWaveShape.hlsli と同じ形）から、前と上へ飛び出し、重力で落ちる。
// ・粒ごとに寿命をずらして繰り返すので、粒の状態を毎フレーム保存しなくてよい。
// ・波が運ぶ勢いを含めた動きは「今の波頭からの相対的な動き」で表している（波と一緒に前へ進む）。
// ・4分の1ほどの粒は、大きく薄い霧のしぶきにしている（水滴だけだと粒がまばらに見えるため）。
// ============================================================================
#include "common.hlsl"
#include "corridorFloodWaveShape.hlsli"
#include "corridorFloodSpray.hlsli"
#include "fastNoise.hlsli"

// 重力（1秒あたりに下向きの速さが増える量。壁の高さ約51を3mとした値）
static const float SprayGravity = 160.0f;
// 粒の寿命（秒）
static const float SprayMinLife = 0.45f;
static const float SprayMaxLife = 0.85f;
// 波頭から飛び出す速さ：上向き、前向き（波頭より前へ。負なら後ろへ残る）、横へのばらつき
static const float SprayUpMinSpeed = 30.0f;
static const float SprayUpMaxSpeed = 60.0f;
static const float SprayForwardMinSpeed = -6.0f;
static const float SprayForwardMaxSpeed = 32.0f;
static const float SpraySideSpeed = 14.0f;
// 粒を出す幅（壁際の低い所からは出さない）と、波頭のてっぺんより少し前から出す量（格子の前後の割合）
static const float SprayLateralLimit = 0.84f;
static const float SprayRowLead = 0.03f;
// 前の向きを測るための、格子の前後の小さなずれ
static const float SprayDirectionStep = 0.06f;
// 水滴の大きさ（半径）と、速さで伸ばす長さ（速さに掛ける秒数）
static const float DropletMinSize = 0.08f;
static const float DropletMaxSize = 0.24f;
static const float DropletStreakSeconds = 0.03f;
// 霧のしぶきの割合・大きさ・濃さ（大きくゆっくり広がる）
static const float MistShare = 0.3f;
static const float MistMinSize = 1.4f;
static const float MistMaxSize = 3.6f;
static const float MistGrowth = 2.2f;
static const float MistAlpha = 0.3f;
static const float MistStreakShare = 0.2f;
// 出始めに濃くなるまでの秒数
static const float SprayFadeInSeconds = 0.05f;
// 乱数の種をずらす量
static const float2 SpraySeed = float2(1.37f, 7.91f);
static const float SpraySeedStep = 17.3f;
// 速さがほとんど無いときの、伸ばす向きを決めない下限
static const float SprayMinScreenSpeed = 0.001f;

float SprayRandom(float particle, float channel)
{
    return FastHash21(float2(particle, particle) * SpraySeed + channel * SpraySeedStep);
}

FloodSprayPixel main(uint id : SV_VertexID)
{
    // 四角形の角（三角形2つ）。CULL_NONE で描くので、並びの向きは問わない
    const float2 corners[SIGNAL_LOST_FLOOD_SPRAY_VERTICES_PER_PARTICLE] =
    {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, -1.0f),
        float2(1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f)
    };
    const float particle = (float)(id / SIGNAL_LOST_FLOOD_SPRAY_VERTICES_PER_PARTICLE);
    const float2 corner = corners[id % SIGNAL_LOST_FLOOD_SPRAY_VERTICES_PER_PARTICLE];

    // 粒ごとの寿命と、今が寿命のどこか（粒ごとに始まりをずらし、繰り返している）
    const float life = lerp(SprayMinLife, SprayMaxLife, SprayRandom(particle, 0.0f));
    const float phase = frac(FloodShape.y / life + SprayRandom(particle, 1.0f));
    const float age = phase * life;
    // 繰り返すたびに出る場所と飛び方が変わるよう、何回目かを乱数の種に混ぜている
    const float cycle = floor(FloodShape.y / life + SprayRandom(particle, 1.0f));
    const float seed = particle + cycle * SIGNAL_LOST_FLOOD_SPRAY_COUNT;

    // 波頭のてっぺんの位置と、波が進む向き
    const float lateral = (SprayRandom(seed, 2.0f) * 2.0f - 1.0f) * SprayLateralLimit;
    const float row = max(FirstCrestWidth - SprayRowLead, 0.0f);
    float crest;
    const float3 top = WavePosition(float2(lateral * 0.5f + 0.5f, row), crest);
    float unusedCrest;
    const float3 behind = WavePosition(float2(lateral * 0.5f + 0.5f, row + SprayDirectionStep), unusedCrest);
    float3 forward = top - behind;
    forward.y = 0.0f;
    forward = normalize(forward + float3(0.0f, 0.0f, SprayMinScreenSpeed));
    const float3 side = float3(forward.z, 0.0f, -forward.x);

    // 飛び出す速さと、今の位置（放物線）
    const float up = lerp(SprayUpMinSpeed, SprayUpMaxSpeed, SprayRandom(seed, 3.0f));
    const float ahead = lerp(SprayForwardMinSpeed, SprayForwardMaxSpeed, SprayRandom(seed, 4.0f));
    const float across = (SprayRandom(seed, 5.0f) * 2.0f - 1.0f) * SpraySideSpeed;
    const float3 velocity = forward * ahead + side * across + float3(0.0f, up - SprayGravity * age, 0.0f);
    const float3 position = top + (forward * ahead + side * across) * age +
        float3(0.0f, up * age - 0.5f * SprayGravity * age * age, 0.0f);

    // 霧のしぶきか水滴か、大きさ、濃さ
    const float mist = SprayRandom(seed, 6.0f) < MistShare ? 1.0f : 0.0f;
    const float size = mist > 0.0f
        ? lerp(MistMinSize, MistMaxSize, SprayRandom(seed, 7.0f)) * (1.0f + MistGrowth * phase)
        : lerp(DropletMinSize, DropletMaxSize, SprayRandom(seed, 7.0f));
    // 床より下へ落ちた粒は消している
    const float aboveFloor = position.y > FloodShape.x ? 1.0f : 0.0f;
    const float alpha = saturate(age / SprayFadeInSeconds) * (1.0f - phase * phase) *
        saturate(crest) * FloodShape.z * aboveFloor * (mist > 0.0f ? MistAlpha : 1.0f);

    // 画面の上で、飛ぶ向きに細長くした四角形にしている
    const float3 viewCenter = mul(float4(position, 1.0f), View).xyz;
    const float3 viewVelocity = mul(float4(velocity, 0.0f), View).xyz;
    const float screenSpeed = length(viewVelocity.xy);
    const float2 along = screenSpeed > SprayMinScreenSpeed ? viewVelocity.xy / screenSpeed : float2(0.0f, 1.0f);
    const float2 across2 = float2(-along.y, along.x);
    const float streak = screenSpeed * DropletStreakSeconds * (mist > 0.0f ? MistStreakShare : 1.0f);
    const float2 offset = along * corner.y * (size + streak) + across2 * corner.x * size;
    const float3 viewPosition = viewCenter + float3(offset, 0.0f);

    FloodSprayPixel result;
    result.Position = mul(float4(viewPosition, 1.0f), Projection);
    result.Corner = corner;
    result.ViewPosition = viewPosition;
    result.Alpha = alpha;
    result.Mist = mist;
    return result;
}
