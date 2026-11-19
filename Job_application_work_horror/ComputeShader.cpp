// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダーの作成と、GPUへの設定をまとめている。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、ComPtrによるCOMリソースの管理
// ============================================================================

#include "ComputeShader.h"

#include "Renderer.h"

// HLSLファイルをcs_5_0としてコンパイルし（入口はmain関数）、コンピュートシェーダーを作っている。失敗したらfalseを返している
bool ComputeShader::Create(const char* fileName)
{
    return SUCCEEDED(Renderer::CreateComputeShader(m_Shader.ReleaseAndGetAddressOf(), fileName));
}

// このコンピュートシェーダーを、次のDispatchで使うよう設定している
void ComputeShader::SetGPU() const
{
    Renderer::GetDeviceContext()->CSSetShader(m_Shader.Get(), nullptr, 0);
}

// シェーダーを解放している
void ComputeShader::Uninit()
{
    m_Shader.Reset();
}
