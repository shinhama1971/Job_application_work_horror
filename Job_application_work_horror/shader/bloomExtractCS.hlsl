// ============================================================================
// シェーダーの役割: 描いた画面から、一定の明るさ以上の画素だけを取り出している（縦横1/4に縮めながら）。
// ============================================================================

// 入力の画面（t0）と、書き込む先（u0。縦横1/4の大きさ）
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

// 1スレッドが、縮めた画像の1画素を担当している
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint outputWidth;
    uint outputHeight;
    OutputTexture.GetDimensions(outputWidth, outputHeight);
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight)
    {
        return;
    }

    uint inputWidth;
    uint inputHeight;
    InputTexture.GetDimensions(inputWidth, inputHeight);

    // 縮めた1画素に対応する、元の画面の範囲（四隅と中心の5点）を読んでいる
    const uint2 maximumSource = uint2(inputWidth - 1, inputHeight - 1);
    const float2 sourceScale =
        float2(inputWidth, inputHeight) / float2(outputWidth, outputHeight);
    const uint2 sourceBegin = min(
        uint2(float2(dispatchThreadId.xy) * sourceScale),
        maximumSource);
    const uint2 sourceEnd = min(
        uint2(float2(dispatchThreadId.xy + 1) * sourceScale) - 1,
        maximumSource);
    const uint2 sourceCenter = (sourceBegin + sourceEnd) / 2;
    const float3 color0 = InputTexture.Load(
        int3(sourceBegin, 0)).rgb;
    const float3 color1 = InputTexture.Load(
        int3(uint2(sourceEnd.x, sourceBegin.y), 0)).rgb;
    const float3 color2 = InputTexture.Load(
        int3(uint2(sourceBegin.x, sourceEnd.y), 0)).rgb;
    const float3 color3 = InputTexture.Load(
        int3(sourceEnd, 0)).rgb;
    const float3 color4 = InputTexture.Load(
        int3(sourceCenter, 0)).rgb;

    // 最大値だけを使うと、1個の明るい画素が縦横1/4の解像度の範囲全体へ
    // 広がって輪郭をぼかしてしまう。中心を重く見た平均にして、光っている物だけを残している。
    const float3 brightColor =
        color4 * 0.40f +
        (color0 + color1 + color2 + color3) * 0.15f;
    const float luminance = dot(brightColor, float3(0.2126f, 0.7152f, 0.0722f));
    // 壁や床の中くらいの明るさは除き、照明・非常灯などの明るい部分だけを取り出している。
    const float contribution = smoothstep(0.72f, 0.98f, luminance);
    OutputTexture[dispatchThreadId.xy] = float4(brightColor * contribution, 1.0f);
}
