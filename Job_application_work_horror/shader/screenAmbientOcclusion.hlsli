// ============================================================================
// 共通の処理: 画面空間の環境遮蔽（SSAO。AmbientOcclusion が本描画の直前に作った画像）を、画素の位置から読んでいる。
// litTexturePS と wetFloorPS で、環境光・天井の照明・映り込みを暗くするのに使っている。common.hlsl の後に読み込んでいる。
// 反射や環境マップなど、本描画以外の視点では使わない（その間は ScreenAmbientOcclusionEnabled が0）。
// ============================================================================
#ifndef SCREEN_AMBIENT_OCCLUSION_INCLUDED
#define SCREEN_AMBIENT_OCCLUSION_INCLUDED

// 環境遮蔽（1 = 遮る物なし、0 = 完全に囲まれている）。描く範囲の縦横半分の大きさで、描く範囲の左上が(0, 0)
Texture2D<float> g_ScreenAmbientOcclusion : register(t15);

float GetScreenAmbientOcclusion(float2 pixelPosition)
{
    float occlusion = 1.0f;
    [branch]
    if (ScreenAmbientOcclusionEnabled > 0.5f)
    {
        // 環境遮蔽は縦横半分の解像度で持っているので、画素の位置を半分にして読んでいる
        occlusion = g_ScreenAmbientOcclusion.Load(int3(int2((pixelPosition - LightViewportOffset) * 0.5f), 0));
    }
    return occlusion;
}

#endif
