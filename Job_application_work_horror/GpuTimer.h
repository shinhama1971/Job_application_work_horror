// ============================================================================
// ファイルの役割: DirectX 11 Timestamp Queryで描画パス別GPU時間を計測します。
// 主な技術: TIMESTAMP、TIMESTAMP_DISJOINT、Queryリング、非同期GetData
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

enum class GpuPass : std::size_t
{
    Total,
    Shadow,
    Reflection,
    MainScene,
    Bloom,
    PostProcess,
    Count
};

enum class GpuTimingStatus
{
    Waiting,
    Available,
    Skipped,
    Invalid
};

struct GpuTiming
{
    double Milliseconds = 0.0;
    GpuTimingStatus Status = GpuTimingStatus::Waiting;
};

class GpuTimer final
{
private:
    static constexpr std::size_t PassCount =
        static_cast<std::size_t>(GpuPass::Count);
    static constexpr std::size_t QueryFrameCount = 4;

    struct QueryFrame
    {
        Microsoft::WRL::ComPtr<ID3D11Query> Disjoint;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, PassCount> Start;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, PassCount> End;
        std::array<bool, PassCount> Issued{};
        std::array<bool, PassCount> Started{};
        std::array<bool, PassCount> Skipped{};
        std::uint64_t FrameNumber = 0;
        bool Pending = false;
    };

    std::array<QueryFrame, QueryFrameCount> m_Frames;
    std::array<GpuTiming, PassCount> m_LatestTimings;
    std::array<GpuTimingStatus, PassCount> m_CurrentStatuses;
    QueryFrame* m_CurrentFrame = nullptr;
    std::size_t m_NextSlot = 0;
    std::uint64_t m_FrameNumber = 0;
    std::uint64_t m_LatestResolvedFrame = 0;
    bool m_HasResolvedFrame = false;
    bool m_Initialized = false;

    bool TryResolveFrame(
        ID3D11DeviceContext* context,
        QueryFrame& frame);

public:
    bool Init(ID3D11Device* device);
    void Uninit();
    void BeginFrame(ID3D11DeviceContext* context);
    void EndFrame(ID3D11DeviceContext* context);
    void BeginPass(GpuPass pass, ID3D11DeviceContext* context);
    void EndPass(GpuPass pass, ID3D11DeviceContext* context);
    void SkipPass(GpuPass pass);
    GpuTiming GetTiming(GpuPass pass) const;
    static constexpr std::size_t GetQueryFrameCount()
    {
        return QueryFrameCount;
    }
};
