struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
};

float4 main(in PS_IN input) : SV_Target
{
    return float4(1, 0, 0, 1);
}