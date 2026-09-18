// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダー生成とGPU設定を安全にまとめます。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、COMリソース管理
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
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
