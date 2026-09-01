// ============================================================================
// シェーダーの役割: 非ライティング描画用に頂点を画面へ変換します。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
    
    //uv���W���ړ�������
    float4 uv;
    uv.xy = input.tex;//�s��̊|���Z�̂���float4�^�Ɉڂ�
    uv.z = 0.0f;
    uv.w = 1.0f;
    uv = mul(uv, matrixTex);//UV���W�ƈړ��s����|���Z
    output.tex = uv.xy;//�|���Z�̌��ʂ𑗐M�p�ϐ��ɃZ�b�g
    output.col = input.col;
    
    output.depth = 0.0f; // �J������Ԃ�z���W���o�͂Ɋi�[
    output.viewPos = float3(0.0f, 0.0f, 0.0f);
    output.viewNormal = float3(0.0f, 0.0f, 0.0f);
    output.worldPos = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 0.0f, 0.0f); // �J�������猩��x,y,z���W���o�͂Ɋi�[
	
    return output;
}

