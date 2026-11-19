// ============================================================================
// 共通の処理: 壁の汚れ、床の凹凸、霧に使う軽いプロシージャルノイズ（計算で作る模様）。
// ゲームの進行と同期する乱数には使わず、見た目専用として使っている。
// ============================================================================
// ほかの部品（樹皮・岩・壁の汚れなど）もこの部品を読み込むので、二重に読まれないよう、1回だけ読む印を付けている
#ifndef FAST_NOISE_INCLUDED
#define FAST_NOISE_INCLUDED

// 水たまりの形の照合に使う GroundWaterEffects.cpp の NoiseHashScale・NoiseHashShift と同じ値。
static const float FAST_HASH_COORDINATE_SCALE = 0.1031f;
static const float FAST_HASH_DOT_OFFSET = 33.33f;

// 2次元の座標から、0〜1の疑似乱数を作っている
float FastHash21(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * FAST_HASH_COORDINATE_SCALE);
    p3 += dot(p3, p3.yzx + FAST_HASH_DOT_OFFSET);
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

// 3段のノイズ：段の数、1段目の強さ、段ごとに細かくする割合（2から少しずらして模様が揃わないようにする）とずらし、段ごとに弱める割合
#define FRACTAL_OCTAVES 3
#define FRACTAL_FIRST_AMPLITUDE 0.55f
#define FRACTAL_LACUNARITY 2.03f
#define FRACTAL_OCTAVE_OFFSET 7.17f
#define FRACTAL_GAIN 0.48f

// 細かさの違うノイズを3段重ねた、自然な揺らぎのノイズ
float FastFractalNoise3(float2 value)
{
    float result = 0.0f;
    float amplitude = FRACTAL_FIRST_AMPLITUDE;
    [unroll]
    for (int octave = 0; octave < FRACTAL_OCTAVES; ++octave)
    {
        result += FastValueNoise(value) * amplitude;
        value = value * FRACTAL_LACUNARITY + FRACTAL_OCTAVE_OFFSET;
        amplitude *= FRACTAL_GAIN;
    }
    return result;
}

#endif
