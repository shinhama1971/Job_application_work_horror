// ============================================================================
// ファイルの役割: 水たまりの水面の波を、GPUの波の式（Compute Shader）で計算している。
// 主な技術: 2次元の波動方程式、ピンポン、決まった間隔で進める計算、範囲をプレイヤーに合わせて動かす仕組み
// （説明はWaterRippleSimulation.hを参照）
// ============================================================================

#include "WaterRippleSimulation.h"

#include "Renderer.h"
#include "utility.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // shader/waterRippleCS.hlslのWaterRippleBufferと同じ並び（b0）
    struct RippleParams
    {
        Vector2 Origin;
        float TexelSize;
        float WaveSpeedSquared;
        int ShiftX;
        int ShiftZ;
        float Damping;
        int ImpulseCount;
        Vector4 FloodRect;
        Vector4 Impulses[WaterRippleSimulation::MaxImpulses];
        int ObstacleCount;
        int Padding[3];
        Vector4 Obstacles[WaterRippleSimulation::MaxObstacles];
    };
    static_assert(sizeof(RippleParams) == 48 + 16 * WaterRippleSimulation::MaxImpulses + 16 + 16 * WaterRippleSimulation::MaxObstacles,
        "RippleParams must match HLSL layout");

    // 1スレッドグループのマスの数（シェーダーのGROUP_SIZEと同じ）
    constexpr int GroupSize = 16;
    // (波の速さ × 1歩の秒数 ÷ マス目の長さ)²。0.5を超えると計算が発散するので、余裕を持たせている。
    // マス目0.75、1秒60歩なので、波の速さは約13.5（静かな水面に広がる、ゆっくりした輪にしている。前の0.35では速すぎた）
    constexpr float WaveSpeedSquared = 0.09f;
    // 1歩ごとに高さに掛ける値。約1.6秒で半分の高さになる（波が遅くなった分、少し長く残している）
    constexpr float Damping = 0.993f;
}

void WaterRippleSimulation::Init(int resolution, float worldSize)
{
    // スレッドグループ（16x16）で割り切れる数にそろえている
    m_Resolution = (std::max)(GroupSize, resolution / GroupSize * GroupSize);
    m_WorldSize = worldSize;
    for (Graphics::RenderTexture& state : m_States)
    {
        state.Init(m_Resolution, m_Resolution, DXGI_FORMAT_R32G32_FLOAT, true);
        state.Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }
    m_CurrentState = 0;

    if (!m_Shader.Create("shader/waterRippleCS.hlsl"))
    {
        utility::ReportFatalError("水たまりの波のシェーダーの作成に失敗しました。");
    }

    D3D11_BUFFER_DESC paramsDesc{};
    paramsDesc.ByteWidth = sizeof(RippleParams);
    paramsDesc.Usage = D3D11_USAGE_DEFAULT;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(Renderer::GetDevice()->CreateBuffer(
        &paramsDesc, nullptr, m_ParamsBuffer.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("水たまりの波の定数バッファの作成に失敗しました。");
    }

    m_HasOrigin = false;
    m_Accumulator = 0.0f;
    m_PendingImpulses.clear();
}

void WaterRippleSimulation::Uninit()
{
    for (Graphics::RenderTexture& state : m_States)
    {
        state.Uninit();
    }
    m_Shader.Uninit();
    m_ParamsBuffer.Reset();
    m_PendingImpulses.clear();
}

void WaterRippleSimulation::AddImpulse(const Vector2& positionXZ, float radius, float strength)
{
    // 1回に足せる数を超えた分は、古いものから捨てている
    if (m_PendingImpulses.size() >= static_cast<size_t>(MaxImpulses))
    {
        m_PendingImpulses.erase(m_PendingImpulses.begin());
    }
    m_PendingImpulses.emplace_back(positionXZ.x, positionXZ.y, radius, strength);
}

ID3D11ShaderResourceView* WaterRippleSimulation::GetHeightSRV()
{
    return m_States[m_CurrentState].GetSRV();
}

void WaterRippleSimulation::Update(float deltaTime, const Vector2& centerXZ,
    ID3D11Buffer* puddleCellBuffer, const Vector4& floodRect, const std::vector<Vector4>& obstacles)
{
    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    if (context == nullptr || m_ParamsBuffer == nullptr)
    {
        return;
    }

    // 決まった間隔（1/60秒）で進め、余った時間は次のフレームへ持ち越している
    m_Accumulator = (std::min)(m_Accumulator + deltaTime, StepSeconds * MaxStepsPerFrame);
    int stepCount = static_cast<int>(m_Accumulator / StepSeconds);
    if (stepCount <= 0)
    {
        return;
    }
    m_Accumulator -= StepSeconds * static_cast<float>(stepCount);

    // 範囲の最小のx・zを、マス目の単位にそろえて決めている（ずれた分はマス目の数で中身をずらす）
    const float texelSize = m_WorldSize / static_cast<float>(m_Resolution);
    const Vector2 snappedOrigin(
        std::floor((centerXZ.x - m_WorldSize * 0.5f) / texelSize) * texelSize,
        std::floor((centerXZ.y - m_WorldSize * 0.5f) / texelSize) * texelSize);
    int shiftX = 0;
    int shiftZ = 0;
    if (m_HasOrigin)
    {
        shiftX = static_cast<int>(std::lround((snappedOrigin.x - m_Origin.x) / texelSize));
        shiftZ = static_cast<int>(std::lround((snappedOrigin.y - m_Origin.y) / texelSize));
    }
    m_Origin = snappedOrigin;
    m_HasOrigin = true;

    // 前のフレームで床・壁のシェーダーに渡した高さのテクスチャを、書き込む前に外している（読み書きの重なりを防ぐ）
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(9, 1, &nullSRV);

    m_Shader.SetGPU();
    ID3D11Buffer* paramsBuffer = m_ParamsBuffer.Get();
    context->CSSetConstantBuffers(0, 1, &paramsBuffer);
    context->CSSetConstantBuffers(13, 1, &puddleCellBuffer);

    // 波を跳ね返す物のうち、計算する範囲に掛かっている物だけを、最大数まで選んでいる
    Vector4 selectedObstacles[MaxObstacles]{};
    int obstacleCount = 0;
    for (const Vector4& obstacle : obstacles)
    {
        if (obstacleCount >= MaxObstacles)
        {
            break;
        }
        const bool overlaps =
            obstacle.x + obstacle.z > m_Origin.x && obstacle.x - obstacle.z < m_Origin.x + m_WorldSize &&
            obstacle.y + obstacle.w > m_Origin.y && obstacle.y - obstacle.w < m_Origin.y + m_WorldSize;
        if (overlaps)
        {
            selectedObstacles[obstacleCount++] = obstacle;
        }
    }

    const UINT groupCount = static_cast<UINT>(m_Resolution / GroupSize);
    for (int step = 0; step < stepCount; ++step)
    {
        // 範囲のずれとおもりは、最初の1歩でだけ使っている
        RippleParams params{};
        params.ObstacleCount = obstacleCount;
        std::copy(selectedObstacles, selectedObstacles + obstacleCount, params.Obstacles);
        params.Origin = m_Origin;
        params.TexelSize = texelSize;
        params.WaveSpeedSquared = WaveSpeedSquared;
        params.ShiftX = step == 0 ? shiftX : 0;
        params.ShiftZ = step == 0 ? shiftZ : 0;
        params.Damping = Damping;
        params.FloodRect = floodRect;
        if (step == 0)
        {
            params.ImpulseCount = static_cast<int>(m_PendingImpulses.size());
            std::copy(m_PendingImpulses.begin(), m_PendingImpulses.end(), params.Impulses);
            m_PendingImpulses.clear();
        }
        context->UpdateSubresource(m_ParamsBuffer.Get(), 0, nullptr, &params, 0, 0);

        // 今の状態を読み、もう1枚へ次の状態を書いて、役目を入れ替えている
        const int nextState = 1 - m_CurrentState;
        ID3D11ShaderResourceView* input = m_States[m_CurrentState].GetSRV();
        ID3D11UnorderedAccessView* output = m_States[nextState].GetUAV();
        context->CSSetShaderResources(0, 1, &input);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->Dispatch(groupCount, groupCount, 1);
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        context->CSSetShaderResources(0, 1, &nullSRV);
        m_CurrentState = nextState;
    }

    ID3D11Buffer* nullBuffer = nullptr;
    context->CSSetConstantBuffers(13, 1, &nullBuffer);
    context->CSSetShader(nullptr, nullptr, 0);
}
