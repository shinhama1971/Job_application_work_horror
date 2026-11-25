#pragma once

#include "Shader.h"
#include <SimpleMath.h>

// 床の水面から立ち上げる、1面の濁流の先頭だけを描く。波頭から飛ぶしぶき（corridorFloodSprayVS・PS）も一緒に描いている。
class CorridorFloodWave final
{
public:
    void SetFront(float distance, float crestStrength);
    void Draw(float floorY, float time);
    void Reset();

private:
    DirectX::SimpleMath::Vector4 CaptureBackground();
    // 波頭から飛ぶしぶきを、半透明・深度は書き込まず・両面で描いている
    void DrawSpray();

    Shader m_Shader;
    Shader m_SprayShader;
    // しぶきの四角形を裏表どちらからも描く設定
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Constants;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Background;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_BackgroundView;
    D3D11_TEXTURE2D_DESC m_BackgroundDescription{};
    float m_Distance = 0.0f;
    float m_CrestStrength = 0.0f;
};
