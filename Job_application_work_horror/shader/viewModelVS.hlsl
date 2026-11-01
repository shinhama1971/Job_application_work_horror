// ============================================================================
// シェーダーの役割: 一人称の手と懐中電灯の頂点を、手元だけの射影行列（b9）で画面へ変換している。
// ビュー行列は本描画と同じなので、ワールドの位置と法線は、壁と同じ照らし方に使える。
// 深度は、FirstPersonHands がビューポートの深度の範囲を手前のわずかな幅にしているので、壁より必ず手前になる。
// ============================================================================

#include "common.hlsl"
#include "viewModel.hlsli"

VIEW_MODEL_PS_IN main(in VS_IN input)
{
    VIEW_MODEL_PS_IN output;
    const float4 worldPosition = mul(input.pos, World);
    output.pos = mul(mul(worldPosition, View), ViewModelProjection);
    output.worldPos = worldPosition.xyz;
    output.worldNormal = normalize(mul(input.nrm.xyz, (float3x3)World));
    output.localPos = input.pos.xyz;
    output.localNormal = input.nrm.xyz;
    output.col = input.col;
    output.tex = input.tex;
    return output;
}
