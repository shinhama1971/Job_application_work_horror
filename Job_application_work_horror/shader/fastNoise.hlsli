// ============================================================================
// 共通の処理: 壁の汚れ、床の凹凸、霧に使う軽いプロシージャルノイズ（計算で作る模様）。
// ゲームの進行と同期する乱数には使わず、見た目専用として使っている。
// ============================================================================

// 2次元の座標から、0〜1の疑似乱数を作っている
float FastHash21(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

// 格子の4隅の乱数を、なめらかに補間した値のノイズ（0〜1）
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

// 細かさの違うノイズを3段重ねた、自然な揺らぎのノイズ
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
