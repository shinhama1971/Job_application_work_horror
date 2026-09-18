#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
    
    //uv���W��ړ�������
    float4 uv;
    uv.xy = input.tex;//�s��̊|���Z�̂���float4�^�Ɉڂ�
    uv.z = 0.0f;
    uv.w = 1.0f;
    uv = mul(uv, matrixTex);//UV���W�ƈړ��s���|���Z
    output.tex = uv.xy;//�|���Z�̌��ʂ𑗐M�p�ϐ��ɃZ�b�g
    output.col = input.col;
	
    return output;
}

