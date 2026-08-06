#pragma once

#include <d3d11.h>
#include <wrl/client.h>

class ComputeShader
{
private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_Shader;

public:
    bool Create(const char* fileName);
    void SetGPU() const;
    void Uninit();
};
