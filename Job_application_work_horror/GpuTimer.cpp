// ============================================================================
// ファイルの役割: 過去フレームのGPU Queryだけを非同期取得し、msへ変換します。
// 主な技術: TIMESTAMP、TIMESTAMP_DISJOINT、DONOTFLUSH、ComPtr
// ============================================================================

#include "GpuTimer.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace
{
    constexpr std::size_t ToIndex(GpuPass pass)
    {
        return static_cast<std::size_t>(pass);
    }

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
            // PostProcess QueryはCopyResourceから最終FullScreen描画までを囲みます。
            // 内側のBloom時間を除き、両項目を重複しない値として表示します。
            resolvedTimings[postProcessIndex].Milliseconds = (std::max)(
                0.0,
                resolvedTimings[postProcessIndex].Milliseconds -
                resolvedTimings[bloomIndex].Milliseconds);
        }
    }

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

    // 完了済みの過去フレームだけを取得します。S_FALSEなら待たずに次へ進みます。
    for (QueryFrame& frame : m_Frames)
    {
        TryResolveFrame(context, frame);
    }

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

void GpuTimer::SkipPass(GpuPass pass)
{
    const std::size_t passIndex = ToIndex(pass);
    m_CurrentStatuses[passIndex] = GpuTimingStatus::Skipped;
    if (m_CurrentFrame != nullptr && pass != GpuPass::Total)
    {
        m_CurrentFrame->Skipped[passIndex] = true;
    }
}

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
