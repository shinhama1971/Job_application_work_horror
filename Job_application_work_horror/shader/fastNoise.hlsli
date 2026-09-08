// ============================================================================
// 共通処理: 壁の汚れ、床の凹凸、霧に使う軽量なプロシージャルノイズ。
// ゲーム進行と同期する乱数には使わず、見た目専用として使用します。
// ============================================================================

float FastHash21(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float FastValueNoise(float2 value)
{
    const float2 cell = floor(value);
    float2 blend = frac(value);
    blend = blend * blend * (3.0f - 2.0f * blend);
    const float a = FastHash21(cell);
    const float b = FastHash21(cell + float2(1.0f, 0.0f));
    const float c = FastHash21(cell + float2(0.0f, 1.0f));
    const float d = FastHash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

float FastFractalNoise3(float2 value)
{
    float result = 0.0f;
    float amplitude = 0.55f;
    [unroll]
    for (int octave = 0; octave < 3; ++octave)
    {
        result += FastValueNoise(value) * amplitude;
        value = value * 2.03f + 7.17f;
        amplitude *= 0.48f;
    }
    return result;
}
