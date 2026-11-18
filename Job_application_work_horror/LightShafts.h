#pragma once

#include "ComputeShader.h"
#include <SimpleMath.h>
#include <wrl/client.h>
#include <vector>

struct ENVIRONMENT_POINT_LIGHT;

namespace Effect
{
    // 光の筋を半解像度で計算し、プレイヤー視点の本描画に渡す。
    class LightShafts final
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_Output;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Result;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_LightView;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_Parameters;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_Lights;
        ComputeShader m_Shader;
        float m_Time = 0.0f;
        bool m_Enabled = true;

    public:
        void Init(int width, int height);
        void Unbind();
        void ResetTime();
        bool Compute(const DirectX::SimpleMath::Matrix& view,
            const DirectX::SimpleMath::Matrix& projection, const D3D11_VIEWPORT& viewport,
            const std::vector<ENVIRONMENT_POINT_LIGHT>& source, float deltaTime);
    };
}
