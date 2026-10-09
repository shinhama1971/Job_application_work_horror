// ============================================================================
// ファイルの役割: 過去のフレームのGPUのクエリだけを、待たずに読み取ってミリ秒に変換している。
// 主な技術: TIMESTAMP、TIMESTAMP_DISJOINT、DONOTFLUSH（読むためにGPUの処理を急がせない）、ComPtr
// ============================================================================

#include "GpuTimer.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace
{
    // 段階の列挙値を、配列の添字に変えている
    constexpr std::size_t ToIndex(GpuPass pass)
    {
        return static_cast<std::size_t>(pass);
    }

    // 環境変数 GPU_TIMER_LOG_PATH があれば、測った結果をそのファイルへ書き出している（調査用）
    const std::string& GetDiagnosticLogPath()
    {
        static const std::string logPath = []
        {
            char path[32768]{};
            const DWORD pathLength = GetEnvironmentVariableA(
                "GPU_TIMER_LOG_PATH", path,
                static_cast<DWORD>(sizeof(path)));
            return pathLength > 0 && pathLength < sizeof(path)
                ? std::string(path, pathLength)
                : std::string();
        }();
        return logPath;
    }

    // 1フレーム分の結果を、カンマ区切りの1行として追記している
    void WriteDiagnosticSample(
        std::uint64_t frameNumber,
        const std::array<GpuTiming,
            static_cast<std::size_t>(GpuPass::Count)>& timings)
    {
        const std::string& logPath = GetDiagnosticLogPath();
        if (logPath.empty())
        {
            return;
        }

        std::ofstream output(logPath, std::ios::app);
        if (!output)
        {
            return;
        }

        output << frameNumber;
        for (const GpuTiming& timing : timings)
        {
            output << ',' << static_cast<int>(timing.Status) << ','
                << timing.Milliseconds;
        }
        output << '\n';
    }
}

// 4フレーム分のクエリ（時計の確認用と、各段階の始まり・終わりの時刻）を作っている
bool GpuTimer::Init(ID3D11Device* device)
{
    Uninit();
    if (device == nullptr)
    {
        return false;
    }

    D3D11_QUERY_DESC disjointDescription{};
    disjointDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    D3D11_QUERY_DESC timestampDescription{};
    timestampDescription.Query = D3D11_QUERY_TIMESTAMP;

    for (QueryFrame& frame : m_Frames)
    {
        if (FAILED(device->CreateQuery(
            &disjointDescription,
            frame.Disjoint.ReleaseAndGetAddressOf())))
        {
            Uninit();
            return false;
        }

        for (std::size_t passIndex = 0; passIndex < PassCount; ++passIndex)
        {
            if (FAILED(device->CreateQuery(
                &timestampDescription,
                frame.Start[passIndex].ReleaseAndGetAddressOf())) ||
                FAILED(device->CreateQuery(
                    &timestampDescription,
                    frame.End[passIndex].ReleaseAndGetAddressOf())))
            {
                Uninit();
                return false;
            }
        }
    }

    m_CurrentStatuses.fill(GpuTimingStatus::Waiting);
    m_Initialized = true;
    return true;
}

// クエリをすべて解放し、状態を最初に戻している
void GpuTimer::Uninit()
{
    m_CurrentFrame = nullptr;
    for (QueryFrame& frame : m_Frames)
    {
        frame.Disjoint.Reset();
        for (auto& query : frame.Start)
        {
            query.Reset();
        }
        for (auto& query : frame.End)
        {
            query.Reset();
        }
        frame = {};
    }
    m_LatestTimings = {};
    m_CurrentStatuses.fill(GpuTimingStatus::Waiting);
    m_NextSlot = 0;
    m_FrameNumber = 0;
    m_LatestResolvedFrame = 0;
    m_HasResolvedFrame = false;
    m_Initialized = false;
}

// GPUからそのフレームの結果が届いていれば読み取り、各段階の時間をミリ秒にしている。
// まだ届いていなければ（S_FALSE）、待たずにfalseを返している
bool GpuTimer::TryResolveFrame(
    ID3D11DeviceContext* context,
    QueryFrame& frame)
{
    if (!frame.Pending)
    {
        return true;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    const HRESULT disjointResult = context->GetData(
        frame.Disjoint.Get(),
        &disjointData,
        sizeof(disjointData),
        D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (disjointResult == S_FALSE)
    {
        return false;
    }

    std::array<GpuTiming, PassCount> resolvedTimings{};
    // 測っている途中でGPUの時計が変わった（Disjoint）ときは、そのフレームの値は使えない
    if (FAILED(disjointResult) || disjointData.Disjoint ||
        disjointData.Frequency == 0)
    {
        for (std::size_t passIndex = 0; passIndex < PassCount; ++passIndex)
        {
            resolvedTimings[passIndex].Status = frame.Skipped[passIndex]
                ? GpuTimingStatus::Skipped
                : GpuTimingStatus::Invalid;
        }
    }
    else
    {
        // 各段階の終わりの時刻から始まりの時刻を引き、GPUの時計の周波数で割ってミリ秒にしている
        for (std::size_t passIndex = 0; passIndex < PassCount; ++passIndex)
        {
            if (frame.Skipped[passIndex])
            {
                resolvedTimings[passIndex].Status = GpuTimingStatus::Skipped;
                continue;
            }
            if (!frame.Issued[passIndex])
            {
                resolvedTimings[passIndex].Status = GpuTimingStatus::Invalid;
                continue;
            }

            std::uint64_t startTimestamp = 0;
            std::uint64_t endTimestamp = 0;
            const HRESULT startResult = context->GetData(
                frame.Start[passIndex].Get(),
                &startTimestamp,
                sizeof(startTimestamp),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const HRESULT endResult = context->GetData(
                frame.End[passIndex].Get(),
                &endTimestamp,
                sizeof(endTimestamp),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (startResult == S_FALSE || endResult == S_FALSE)
            {
                return false;
            }
            if (FAILED(startResult) || FAILED(endResult) ||
                endTimestamp < startTimestamp)
            {
                resolvedTimings[passIndex].Status = GpuTimingStatus::Invalid;
                continue;
            }

            resolvedTimings[passIndex].Milliseconds =
                static_cast<double>(endTimestamp - startTimestamp) * 1000.0 /
                static_cast<double>(disjointData.Frequency);
            resolvedTimings[passIndex].Status = GpuTimingStatus::Available;
        }

        const std::size_t postProcessIndex = ToIndex(GpuPass::PostProcess);
        const std::size_t bloomIndex = ToIndex(GpuPass::Bloom);
        if (resolvedTimings[postProcessIndex].Status ==
                GpuTimingStatus::Available &&
            resolvedTimings[bloomIndex].Status == GpuTimingStatus::Available)
        {
            // PostProcessのクエリは、画面の取り込み（CopyResource）から最後の全画面の描画までを囲んでいる。
            // その中に含まれるブルームの時間を引き、2つの項目が重ならない値として表示している。
            resolvedTimings[postProcessIndex].Milliseconds = (std::max)(
                0.0,
                resolvedTimings[postProcessIndex].Milliseconds -
                resolvedTimings[bloomIndex].Milliseconds);
        }
    }

    // 届いた順番が前後しても、より新しいフレームの結果だけを残している
    if (!m_HasResolvedFrame || frame.FrameNumber > m_LatestResolvedFrame)
    {
        m_LatestTimings = resolvedTimings;
        m_LatestResolvedFrame = frame.FrameNumber;
        m_HasResolvedFrame = true;
        WriteDiagnosticSample(frame.FrameNumber, resolvedTimings);
    }
    frame.Pending = false;
    return true;
}

void GpuTimer::BeginFrame(ID3D11DeviceContext* context)
{
    m_CurrentFrame = nullptr;
    m_CurrentStatuses.fill(GpuTimingStatus::Waiting);
    if (!m_Initialized || context == nullptr)
    {
        return;
    }

    // 描き終わった過去のフレームの結果だけを読み取っている。S_FALSEなら待たずに次へ進んでいる。
    for (QueryFrame& frame : m_Frames)
    {
        TryResolveFrame(context, frame);
    }

    // 結果を読み終えたクエリの組を探して、このフレームに使っている。全部使用中なら、このフレームは測らない
    QueryFrame* availableFrame = nullptr;
    for (std::size_t offset = 0; offset < QueryFrameCount; ++offset)
    {
        const std::size_t slot = (m_NextSlot + offset) % QueryFrameCount;
        if (!m_Frames[slot].Pending)
        {
            availableFrame = &m_Frames[slot];
            m_NextSlot = (slot + 1) % QueryFrameCount;
            break;
        }
    }

    ++m_FrameNumber;
    if (availableFrame == nullptr)
    {
        return;
    }

    // このフレームの記録を始め、フレーム全体の始まりの時刻を記録している
    availableFrame->Issued.fill(false);
    availableFrame->Started.fill(false);
    availableFrame->Skipped.fill(false);
    availableFrame->FrameNumber = m_FrameNumber;
    context->Begin(availableFrame->Disjoint.Get());

    const std::size_t totalIndex = ToIndex(GpuPass::Total);
    context->End(availableFrame->Start[totalIndex].Get());
    availableFrame->Issued[totalIndex] = true;
    availableFrame->Started[totalIndex] = true;
    m_CurrentFrame = availableFrame;
}

// フレーム全体の終わりの時刻を記録し、結果が届くのを待つ状態にしている
void GpuTimer::EndFrame(ID3D11DeviceContext* context)
{
    if (m_CurrentFrame == nullptr || context == nullptr)
    {
        return;
    }

    const std::size_t totalIndex = ToIndex(GpuPass::Total);
    context->End(m_CurrentFrame->End[totalIndex].Get());
    m_CurrentFrame->Started[totalIndex] = false;
    context->End(m_CurrentFrame->Disjoint.Get());
    m_CurrentFrame->Pending = true;
    m_CurrentFrame = nullptr;
}

// 段階の始まりの時刻を記録している（同じ段階を二重に始めないようにしている）
void GpuTimer::BeginPass(GpuPass pass, ID3D11DeviceContext* context)
{
    const std::size_t passIndex = ToIndex(pass);
    if (m_CurrentFrame == nullptr || context == nullptr ||
        pass == GpuPass::Total || m_CurrentFrame->Started[passIndex])
    {
        return;
    }

    context->End(m_CurrentFrame->Start[passIndex].Get());
    m_CurrentFrame->Issued[passIndex] = true;
    m_CurrentFrame->Started[passIndex] = true;
}

// 段階の終わりの時刻を記録している
void GpuTimer::EndPass(GpuPass pass, ID3D11DeviceContext* context)
{
    const std::size_t passIndex = ToIndex(pass);
    if (m_CurrentFrame == nullptr || context == nullptr ||
        pass == GpuPass::Total || !m_CurrentFrame->Started[passIndex])
    {
        return;
    }

    context->End(m_CurrentFrame->End[passIndex].Get());
    m_CurrentFrame->Started[passIndex] = false;
}

// このフレームではその段階を描かなかったことを記録している（表示では「省いた」になる）
void GpuTimer::SkipPass(GpuPass pass)
{
    const std::size_t passIndex = ToIndex(pass);
    m_CurrentStatuses[passIndex] = GpuTimingStatus::Skipped;
    if (m_CurrentFrame != nullptr && pass != GpuPass::Total)
    {
        m_CurrentFrame->Skipped[passIndex] = true;
    }
}

// そのフレームで省いた段階は「省いた」、それ以外は最後に測れた結果を返している
GpuTiming GpuTimer::GetTiming(GpuPass pass) const
{
    const std::size_t passIndex = ToIndex(pass);
    if (m_CurrentStatuses[passIndex] == GpuTimingStatus::Skipped)
    {
        return { 0.0, GpuTimingStatus::Skipped };
    }
    if (!m_Initialized)
    {
        return { 0.0, GpuTimingStatus::Invalid };
    }
    return m_LatestTimings[passIndex];
}
