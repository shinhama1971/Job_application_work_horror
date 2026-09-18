// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダー生成とGPU設定を安全にまとめます。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、COMリソース管理
// ============================================================================

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
