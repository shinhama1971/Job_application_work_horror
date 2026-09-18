#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

// �s�N�Z���V�F�[�_�[�̃G���g���|�C���g
float4 main(in PS_IN input) : SV_Target
{
    float4 color;
	
    // Sample�֐����e�N�X�`������Y����UV�ʒu�̃s�N�Z���F�����ė���
    color = g_Texture.Sample(g_SamplerState, input.tex);
    color *= input.col;

   // color = input.col;

    return color;
}
