// ============================================================================
// 共通の処理: 画面空間の環境遮蔽（SSAO）と接触影（AmbientOcclusion が本描画の直前に作った画像）を、画素の位置から読んでいる。
// litTexturePS と wetFloorPS で、環境光・天井の照明・映り込みを暗くするのに使っている。common.hlsl の後に読み込んでいる。
// 反射や環境マップなど、本描画以外の視点では使わない（その間は ScreenAmbientOcclusionEnabled が0）。
// ============================================================================
#ifndef SCREEN_AMBIENT_OCCLUSION_INCLUDED
#define SCREEN_AMBIENT_OCCLUSION_INCLUDED

// x = 環境遮蔽（1 = 遮る物なし、0 = 完全に囲まれている）、y = 天井の照明の接触影（1 = 影なし）。
// 描く範囲の縦横半分の大きさで、描く範囲の左上が(0, 0)（ambientOcclusionCS.hlsl）
Texture2D<float2> g_ScreenAmbientOcclusion : register(t15);

// 各 Compute Shader が書く解像度に合わせて、本描画の画素を読み取り先へ変換する。
#define SCREEN_OCCLUSION_UV_SCALE 0.5f
#define SCREEN_INDIRECT_LIGHT_UV_SCALE 0.5f

// 環境遮蔽と接触影を、画素の位置から読んでいる（使わないときは、どちらも1）
float2 LoadScreenOcclusion(float2 pixelPosition)
{
    float2 occlusion = 1.0f;
    [branch]
    if (ScreenAmbientOcclusionEnabled > 0.5f)
    {
        // 縦横半分の解像度で持っているので、画素の位置を半分にして読んでいる
        occlusion = g_ScreenAmbientOcclusion.Load(int3(int2((pixelPosition - LightViewportOffset) * SCREEN_OCCLUSION_UV_SCALE), 0));
    }
    return occlusion;
}

float GetScreenAmbientOcclusion(float2 pixelPosition)
{
    return LoadScreenOcclusion(pixelPosition).x;
}

// DDGI の間接光（壁や床で跳ね返った光。DDGIVolume の ddgiApplyCS が縦横半分の解像度で求めた画像。t18）。
// 環境遮蔽と同じく、本描画のときだけ使う（使わない場面では0）
Texture2D<float4> g_ScreenIndirectLight : register(t18);
// 間接光を、照明（lighting）に足す強さ
static const float SCREEN_INDIRECT_LIGHT_INTENSITY = 1.0f;

float3 GetScreenIndirectLight(float2 pixelPosition)
{
    float3 indirect = 0.0f;
    [branch]
    if (ScreenAmbientOcclusionEnabled > 0.5f)
    {
        indirect = g_ScreenIndirectLight.Load(int3(int2((pixelPosition - LightViewportOffset) * SCREEN_INDIRECT_LIGHT_UV_SCALE), 0)).rgb *
            SCREEN_INDIRECT_LIGHT_INTENSITY;
    }
    return indirect;
}

// 天井の照明（点光源）の接触影：棚・机・ロッカーの足元の細い影。画素ごとに一番強く照らしている点光源の向きで求めてあり、
// 点光源の光に掛けている（1 = 影なし）
float GetScreenContactShadow(float2 pixelPosition)
{
    return LoadScreenOcclusion(pixelPosition).y;
}

#endif
