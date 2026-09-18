// ============================================================================
// ファイルの役割: Direct3D 11のコンピュートシェーダー生成とGPU設定を安全にまとめます。
// 主な技術: Direct3D 11 Compute Shader、CSSetShader、COMリソース管理
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "ComputeShader.h"

#include "Renderer.h"

// 処理内容: 必要なCPU/GPUリソースを生成します。
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

// 処理内容: 外部から受け取った値を検証して状態へ反映します。
void ComputeShader::SetGPU() const
{
    Renderer::GetDeviceContext()->CSSetShader(m_Shader.Get(), nullptr, 0);
}

// 処理内容: 所有するリソースを依存関係の逆順で解放します。
void ComputeShader::Uninit()
{
    m_Shader.Reset();
}
