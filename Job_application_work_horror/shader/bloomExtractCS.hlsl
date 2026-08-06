Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

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

    const uint2 source = dispatchThreadId.xy * 2;
    const uint2 maximumSource = uint2(inputWidth - 1, inputHeight - 1);
    const float3 color0 = InputTexture.Load(int3(min(source, maximumSource), 0)).rgb;
    const float3 color1 = InputTexture.Load(int3(min(source + uint2(1, 0), maximumSource), 0)).rgb;
    const float3 color2 = InputTexture.Load(int3(min(source + uint2(0, 1), maximumSource), 0)).rgb;
    const float3 color3 = InputTexture.Load(int3(min(source + uint2(1, 1), maximumSource), 0)).rgb;

    // Preserve small hot highlights while downsampling the scene to half resolution.
    const float3 brightColor = max(max(color0, color1), max(color2, color3));
    const float luminance = dot(brightColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float contribution = smoothstep(0.52f, 0.88f, luminance);
    OutputTexture[dispatchThreadId.xy] = float4(brightColor * contribution, 1.0f);
}
