// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダーの作成と、GPUへの設定をまとめている。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、ComPtrによるCOMリソースの管理
// ============================================================================

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

// コンピュートシェーダー1つ分。タイルベースライティングの光源の振り分けや、ブルームのぼかしに使っている
class ComputeShader
{
private:
    // 作ったシェーダー（ComPtrなので、解放し忘れがない）
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_Shader;

public:
    // HLSLファイルをコンパイルして作っている
    bool Create(const char* fileName);
    // 次のDispatchで使うよう設定している
    void SetGPU() const;
    // 解放している
    void Uninit();
};
