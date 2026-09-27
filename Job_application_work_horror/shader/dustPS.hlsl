// ============================================================================
// シェーダーの役割: GPUパーティクル（埃）を、当たっている光の分だけ光らせます。
// 埃は自分では光らず、懐中電灯の円錐の中や点光源の近くでだけ見えるようにします。
// 点光源は本描画と同じタイル別リスト（Compute Shaderで作成）を使います。
// 加算合成で描くため、奥から順に並べ替える（Zソート）必要がありません。
// ============================================================================

#include "common.hlsl"
#include "flashlightShadow.hlsli"
#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"

struct DUST_PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 viewPos : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float brightness : TEXCOORD3;
};

static const float3 DustAlbedo = float3(0.92f, 0.86f, 0.76f);

float4 main(DUST_PS_IN input) : SV_TARGET
{
    // 四角形を中心ほど濃い円形の粒にします。
    const float radiusSquared = dot(input.uv, input.uv);
    if (radiusSquared >= 1.0f || input.brightness <= 0.0f)
    {
        discard;
    }
    const float shape = (1.0f - radiusSquared) * (1.0f - radiusSquared);

    float3 lighting = Light.Ambient.rgb * 0.05f;

    // 懐中電灯: カメラから粒へ向かう方向が円錐の内側なら照らされます。
    // 光を遮る物の陰にある埃は、シャドウマップで暗くして光の筋を作ります。
    const float distanceFromCamera = length(input.viewPos);
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        [branch]
        if (beamProfile > 0.001f)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, 0.001f));
            const float rangeFade = 1.0f - normalizedDistance * normalizedDistance;
            const float4 shadowPosition =
                mul(float4(input.worldPos, 1.0f), ShadowViewProjection);
            // 光の向きへ舞う埃は明るく光る（前方散乱）ので、手前の粒ほど強めます。
            const float scattering = lerp(2.2f, 1.0f, normalizedDistance);
            lighting += Light.Diffuse.rgb * Light.Intensity *
                beamProfile * rangeFade * rangeFade * scattering *
                GetFlashlightShadow(shadowPosition);
        }
    }

    // 点光源: このピクセルのタイルに影響する光源だけを計算します。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);
        const float3 offsetToLight = pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, 0.001f);
        const float distanceToLight = length(offsetToLight);
        float attenuation = saturate(1.0f - distanceToLight / lightRange);
        attenuation *= attenuation;
        lighting += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a *
            attenuation * 0.55f;
    }

    const float alpha = saturate(shape * input.brightness);
    return float4(lighting * DustAlbedo * 0.32f, alpha);
}
