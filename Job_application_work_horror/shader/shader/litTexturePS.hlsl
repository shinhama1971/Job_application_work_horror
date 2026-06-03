#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

float4 main(in PS_IN input)  : SV_Target
{	
    float4 color;
    if (Material.TextureEnable)
    {
    
    // Sample関数→テクスチャから該当のUV位置のピクセル色を取って来る
        color = g_Texture.Sample(g_SamplerState, input.tex);
        color *= input.col;
    }
    else
    {
        color = input.col * Material.Diffuse;

    }
    float fogStart = 20.0f; // この距離から暗くなり始める（※ステージの広さに合わせて後で調整）
    float fogEnd = 80.0f; // この距離で完全に真っ暗（見えなくなる）になる
    float4 fogColor = float4(0.0f, 0.0f, 0.0f, 1.0f); // 霧の色（真っ黒）
    float4 fogColor2 = float4(0.5f, 0.0f, 0.0f, 1.0f); // 霧の色（赤色）

    // 距離から、霧の濃さ（0.0 ～ 1.0）を計算する
    // 近ければ 0.0、遠ければ 1.0 になる計算式です。saturateで上限下限をカットします。
    float fogFactor = saturate((input.depth - fogStart) / (fogEnd - fogStart));

    // カメラ（懐中電灯）は常に原点にあり、Z軸の奥を向いている
    float3 lightPos = float3(0.0f, 0.0f, 0.0f);
    float3 lightDir = float3(0.0f, 0.0f, 1.0f);

    // カメラから、描画しようとしているピクセルへの「方向（ベクトル）」を作る
    float3 pixelDir = normalize(input.viewPos - lightPos);

    // 2つのベクトルの内積（Dot）を取る
    // （向いている方向とピクセルの方向がピッタリ一致すれば 1.0 になる）
    float spotFactor = dot(pixelDir, lightDir);

    // 懐中電灯の「円の大きさ」を設定（cos関数を使って角度を指定）
    // 15度までは一番明るく、25度に向かって徐々に暗くなる
    float spotInner = cos(radians(15.0f));
    float spotOuter = cos(radians(25.0f));

    // 30度までは一番明るく、25度に向かって徐々に暗くなる
    //障害物にライトが当たった際の光を避けるようになる
    /* float spotInner = cos(radians(30.0f));
    float spotOuter = cos(radians(25.0f));*/
    
    // smoothstep関数で、光の輪郭をふんわりとボカす（0.0 ～ 1.0）
    float intensity = smoothstep(spotOuter, spotInner, spotFactor);

    // 全体の色に光の強さを掛け算して、光が当たっていない場所を真っ暗にする
    color.rgb *= intensity;
    
    
    // 本来の色(color)と、真っ黒(fogColor)を、fogFactor の割合で混ぜ合わせる！
    color.rgb = lerp(color.rgb, fogColor.rgb, fogFactor);
    //color = input.col;

    return color;
}

