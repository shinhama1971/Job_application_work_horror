#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	//positoin=============================
	// ���[���h�A�r���[�A�v���W�F�N�V�����s���|�����킹�č��W�ϊ���s��
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
	
    //�J������Ԃ̂����W��v�Z����
    matrix wv = mul(World, View);
    float4 viewPos = mul(input.pos, wv);
    output.depth = viewPos.z; // // �J������Ԃ�z���W��o�͂Ɋi�[
    output.viewPos = viewPos.xyz; // �J�������猩��x,y,z���W��o�͂Ɋi�[
    
	//color=============================
    float4 normal = float4(input.nrm.xyz, 0.0);
    float4 worldNormal = mul(normal, World);
    worldNormal = normalize(worldNormal);
	
    if (Light.Enable)
    {
        float d = -dot(Light.Direction.xyz, worldNormal.xyz);
        d = saturate(d);
        output.col.xyz = input.col.xyz * d * Light.Diffuse.xyz; // �g�U���̉e�����Z
        output.col.xyz += input.col.xyz * Light.Ambient.xyz; // �A���r�G���g������Z
    }
    else
    {
        output.col.xyz = input.col.xyz;
    }
    output.col.xyz += Material.Emission.xyz;
    output.col.a = input.col.a * Material.Diffuse.a; // �A���t�@�l�͂��̂܂܎g�p
	
	//texture=============================
	// �e�N�X�`�����W�͂��̂܂܎g�p
    output.tex = input.tex;
	
    return output;
}


