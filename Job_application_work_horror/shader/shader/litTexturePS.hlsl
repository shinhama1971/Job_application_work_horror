#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

float4 main(in PS_IN input)  : SV_Target
{	
    float4 color;
    if (Material.TextureEnable)
    {
    
    // Sample�֐����e�N�X�`������Y����UV�ʒu�̃s�N�Z���F�����ė���
        color = g_Texture.Sample(g_SamplerState, input.tex);
        color *= input.col;
    }
    else
    {
        color = input.col * Material.Diffuse;

    }
    float fogStart = 20.0f; // ���̋�������Â��Ȃ�n�߂�i���X�e�[�W�̍L���ɍ��킹�Č�Œ����j
    float fogEnd = 80.0f; // ���̋����Ŋ��S�ɐ^���Ái�����Ȃ��Ȃ�j�ɂȂ�
    float4 fogColor = float4(0.0f, 0.0f, 0.0f, 1.0f); // ���̐F�i�^�����j
    float4 fogColor2 = float4(0.5f, 0.0f, 0.0f, 1.0f); // ���̐F�i�ԐF�j

    // ��������A���̔Z���i0.0 �` 1.0�j��v�Z����
    // �߂���� 0.0�A������� 1.0 �ɂȂ�v�Z���ł��Bsaturate�ŏ��������J�b�g���܂��B
    float fogFactor = saturate((input.depth - fogStart) / (fogEnd - fogStart));

    // �J�����i�����d���j�͏�Ɍ��_�ɂ���AZ���̉�������Ă���
    float3 lightPos = float3(0.0f, 0.0f, 0.0f);
    float3 lightDir = float3(0.0f, 0.0f, 1.0f);

    // �J��������A�`�悵�悤�Ƃ��Ă���s�N�Z���ւ́u�����i�x�N�g���j�v����
    float3 pixelDir = normalize(input.viewPos - lightPos);

    // 2�̃x�N�g���̓�ρiDot�j����
    // �i�����Ă�������ƃs�N�Z���̕������s�b�^����v����� 1.0 �ɂȂ�j
    float spotFactor = dot(pixelDir, lightDir);

    // �����d���́u�~�̑傫���v��ݒ�icos�֐���g���Ċp�x��w��j
    // 15�x�܂ł͈�Ԗ��邭�A25�x�Ɍ������ď��X�ɈÂ��Ȃ�
    float spotInner = cos(radians(15.0f));
    float spotOuter = cos(radians(25.0f));

    // 30�x�܂ł͈�Ԗ��邭�A25�x�Ɍ������ď��X�ɈÂ��Ȃ�
    //��Q���Ƀ��C�g�����������ۂ̌�������悤�ɂȂ�
    /* float spotInner = cos(radians(30.0f));
    float spotOuter = cos(radians(25.0f));*/
    
    // smoothstep�֐��ŁA���̗֊s��ӂ���ƃ{�J���i0.0 �` 1.0�j
    float intensity = smoothstep(spotOuter, spotInner, spotFactor);

    // �S�̂̐F�Ɍ��̋�����|���Z���āA�����������Ă��Ȃ��ꏊ��^���Âɂ���
    color.rgb *= intensity;
    
    
    // �{���̐F(color)�ƁA�^����(fogColor)��AfogFactor �̊����ō������킹��I
    color.rgb = lerp(color.rgb, fogColor.rgb, fogFactor);
    //color = input.col;

    return color;
}

