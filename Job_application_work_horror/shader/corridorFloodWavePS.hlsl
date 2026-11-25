// 水面の細かな光沢を動かし、白い泡は崩れている波頭と壁のそばにだけ溜める。
#include "common.hlsl"
#include "corridorFloodWave.hlsli"
#include "flashlightLighting.hlsli"

Texture2D<float4> Background : register(SIGNAL_LOST_FLOOD_WAVE_SRV(SIGNAL_LOST_FLOOD_WAVE_BACKGROUND_SLOT));
SamplerState BackgroundSampler : register(s0);

static const float3 DeepTint = float3(0.045f, 0.004f, 0.006f);
static const float3 Absorption = float3(1.1f, 2.6f, 2.9f);
static const float3 FoamTint = float3(0.86f, 0.74f, 0.70f);
static const float3 CeilingTint = float3(0.17f, 0.15f, 0.15f);
// 水面の模様の細かさ（幅方向・流れの方向）。幅方向が細かすぎると、前の面の筋がそろって垂れた布のように見える
static const float2 RippleScale = float2(17.0f, 0.52f);
static const float FoamHeightWarp = 0.39f;
static const float2 RippleSpeed = float2(-0.47f, 3.40f);
static const float MicroRippleFrequency = 2.7f;
static const float SurfaceNormalDetail = 0.24f;
static const float DetailWarp = 1.5f;
// 泡が出る所：波頭の高さ（x〜y の間で濃くなる）と、壁のそば（幅の割合 z から）。波頭の上半分に白く泡立つ帯を出している
static const float3 FoamMask = float3(0.7f, 1.0f, 0.94f);
static const float FineFoamStrength = 0.15f;
static const float FoamFrontStart = 0.055f;
static const float FoamFrontFull = 0.19f;
static const float FoamFrontFade = 0.38f;
static const float FoamFrontAmount = 0.48f;
static const float FineFoamCut = 0.54f;
static const float FoamLineWidth = 0.065f;
static const float FresnelNormalIncidence = 0.02f;
static const float ReflectionLineRate = 8.0f;
static const float ReflectionLineCut = 0.76f;
static const float ReflectionLineGain = 0.18f;
static const float SpotlightExponent = 140.0f;
static const float SpotlightGain = 2.2f;
static const float FoamLightGain = 0.22f;
static const float FoamBeamGain = 0.75f;
static const float MinOpticalDepth = 0.18f;
static const float MaxOpticalDepth = 1.0f;
static const float RefractionTravel = 4.5f;
static const float RefractionDepthFloor = 8.0f;
static const float RefractionMaxOffset = 0.08f;
static const float RefractionRippleGain = 0.45f;
// 波頭の後ろの低い水でも、背景がこの割合だけゆらいで透けて見える（水だと分かる手がかり）
static const float RefractionTailShare = 0.45f;
static const float GrazingReflectionGain = 0.64f;
static const float VisibleEdgeStart = 0.75f;
static const float ShallowFrontBlendEnd = 0.072f;
static const float ShallowTailBlendStart = 0.79f;

float4 main(in FloodWavePixel input) : SV_Target
{
    // 泡も各列が通った道の距離を使う。角に来ても模様を別の向きへ付け替えない。
    const float runLength = min(FloodShape.w, SIGNAL_LOST_FLOOD_WAVE_DEPTH);
    const float2 surface = float2(input.Grid.x, FloodShape.w - input.Grid.y * runLength) * RippleScale;
    const float height = (input.WorldPosition.y - FloodShape.x) * FoamHeightWarp;
    const float2 flow = surface + height * float2(0.5f, 1.0f) + FloodShape.y * RippleSpeed;
    const float broad = FastValueNoise(flow);
    const float2 microField = flow * MicroRippleFrequency + broad * DetailWarp;
    const float2 ripples = float2(FastValueNoise(microField), FastValueNoise(microField.yx + 0.5f));

    const float edge = abs(input.Grid.x * 2.0f - 1.0f);
    const float crest = smoothstep(FoamMask.x, FoamMask.y, input.Crest);
    const float wallFoam = smoothstep(FoamMask.z, 1.0f, edge) * input.Crest;
    // ノイズの等高線を細い泡の筋にし、高周波の模様で途切れさせる。遠い画素は fwidth でならす。
    const float contour = abs(broad - 0.5f);
    const float tolerance = max(fwidth(broad), FoamLineWidth);
    const float foamLines = (1.0f - smoothstep(0.0f, tolerance, contour)) *
        smoothstep(FineFoamCut, 1.0f, ripples.x);
    // 床に近い先端にも白い砕け泡を出し、波が倒れながら前へ流れる形を読ませる。
    const float brokenFront = smoothstep(FoamFrontStart, FoamFrontFull, input.Grid.y) *
        (1.0f - smoothstep(FoamFrontFull, FoamFrontFade, input.Grid.y)) * FoamFrontAmount;
    const float foam = saturate(max(max(crest, wallFoam), brokenFront) *
        (foamLines + ripples.y * FineFoamStrength)) * FloodShape.z;

    // ジオメトリの法線に細かな揺れを足し、同じ画角でも光沢が流れるようにする。
    const float2 perturb = (ripples * 2.0f - 1.0f) * SurfaceNormalDetail;
    const float3 normal = normalize(input.ViewNormal + float3(perturb, 0.0f));
    const float3 eye = -normalize(input.ViewPosition);
    const float facing = saturate(dot(normal, eye));
    const float fresnel = FresnelNormalIncidence +
        (1.0f - FresnelNormalIncidence) * pow(1.0f - facing, 5.0f);
    // 天井から拾う細い反射光を近似する。波を直接照らしていないときにも輪郭が残る。
    const float3 reflected = mul(reflect(-eye, normal), transpose((float3x3)View));
    const float strips = smoothstep(ReflectionLineCut, 1.0f,
        abs(sin(reflected.x * ReflectionLineRate)));
    const float3 sheen = CeilingTint *
        (fresnel + strips * ReflectionLineGain * saturate(reflected.y));

    const float beam = Light.FlashlightEnabled ? GetFlashlightBeamProfile(-eye) : 0.0f;
    const float range = saturate(1.0f - length(input.ViewPosition) / max(Light.Range, FLASHLIGHT_MIN_DIVISOR));
    const float3 flashlight = Light.Diffuse.rgb * (beam * range * Light.Intensity);
    const float specular = pow(facing, SpotlightExponent) * SpotlightGain;
    const float thickness = lerp(MinOpticalDepth, MaxOpticalDepth, input.Crest * FloodShape.z);
    float3 transmitted = DeepTint;
    float3 backdrop = DeepTint;
    [branch]
    if (FloodRefraction.z > 0.0f)
    {
        // 水面の法線で背景の読み位置をずらす。近くほどゆがみが大きくなり、薄い波先では消える。
        const float travel = min(RefractionMaxOffset, RefractionTravel / max(abs(input.ViewPosition.z), RefractionDepthFloor));
        const float2 offset = float2(normal.x, -normal.y) + (ripples * 2.0f - 1.0f) * RefractionRippleGain;
        const float2 minUV = FloodRefraction.xy * 0.5f;
        const float2 screenUV = input.Position.xy * FloodRefraction.xy;
        const float2 uv = clamp(screenUV + offset * travel * lerp(RefractionTailShare, 1.0f, input.Crest), minUV, 1.0f - minUV);
        backdrop = Background.SampleLevel(BackgroundSampler, screenUV, 0.0f).rgb;
        const float3 behind = Background.SampleLevel(BackgroundSampler, uv, 0.0f).rgb;
        const float3 transmission = exp(-Absorption * thickness);
        transmitted = behind * transmission + DeepTint * (1.0f - transmission);
    }

    const float3 water = lerp(transmitted, sheen, fresnel * GrazingReflectionGain) + flashlight * specular;
    const float3 foamColor = FoamTint * (FoamLightGain + flashlight * FoamBeamGain);
    // 浅い水に向かって端の屈折と泡を弱める。格子の端が板のように見えるのを防ぐ。
    const float coverage = (1.0f - smoothstep(VisibleEdgeStart, 1.0f, edge)) *
        smoothstep(0.0f, ShallowFrontBlendEnd, input.Grid.y) *
        (1.0f - smoothstep(ShallowTailBlendStart, 1.0f, input.Grid.y));
    const float3 surfaceColor = lerp(water, foamColor, foam);
    return float4(FloodRefraction.z > 0.0f ? lerp(backdrop, surfaceColor, coverage) : surfaceColor, 1.0f);
}
