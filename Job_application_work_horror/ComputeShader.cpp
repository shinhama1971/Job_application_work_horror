// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダーの作成と、GPUへの設定をまとめている。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、ComPtrによるCOMリソースの管理
// ============================================================================

#include "ComputeShader.h"

#include "Renderer.h"

// HLSLファイルをcs_5_0としてコンパイルし（入口はmain関数）、コンピュートシェーダーを作っている。失敗したらfalseを返している
bool ComputeShader::Create(const char* fileName)
{
    std::vector<unsigned char> shaderObject;

    const HRESULT compileResult = Renderer::CompileShader(
        fileName, "main", "cs_5_0", shaderObject);
    if (FAILED(compileResult))
    {
        return false;
    }

    const HRESULT createResult = Renderer::GetDevice()->CreateComputeShader(
        shaderObject.data(),
        shaderObject.size(),
        nullptr,
        m_Shader.ReleaseAndGetAddressOf());

    return SUCCEEDED(createResult);
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
