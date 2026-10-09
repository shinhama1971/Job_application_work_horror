// ============================================================================
// ファイルの役割: DirectX 11のタイムスタンプのクエリで、描画の段階ごとのGPU時間を測っている。
// 主な技術: TIMESTAMP、TIMESTAMP_DISJOINT、4フレーム分のクエリを順に使い回す仕組み、待たずに結果を取るGetData
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

// 測る描画の段階（フレーム全体・影・水面の反射・本描画・ブルーム・その他の画面効果）
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

// 測った結果の状態（まだ届いていない・測れた・そのフレームは描かなかった・測れなかった）
enum class GpuTimingStatus
{
    Waiting,
    Available,
    Skipped,
    Invalid
};

// 1つの段階の測定結果（ミリ秒と状態）
struct GpuTiming
{
    double Milliseconds = 0.0;
    GpuTimingStatus Status = GpuTimingStatus::Waiting;
};

// GPUの処理はCPUより遅れて進むため、結果は数フレーム後に届く。
// 4フレーム分のクエリを順番に使い、届いた古いフレームの結果だけを読むことで、GPUの完了を待たずに測っている。
class GpuTimer final
{
private:
    // 測る段階の数と、同時に使うフレームの数
    static constexpr std::size_t PassCount =
        static_cast<std::size_t>(GpuPass::Count);
    static constexpr std::size_t QueryFrameCount = 4;

    // 1フレーム分のクエリ：GPUの時計が途中で変わらなかったかを調べるクエリと、各段階の始まり・終わりの時刻
    struct QueryFrame
    {
        Microsoft::WRL::ComPtr<ID3D11Query> Disjoint;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, PassCount> Start;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, PassCount> End;
        // 各段階で、始まりの時刻を記録したか・測っている途中か・描くのを省いたか
        std::array<bool, PassCount> Issued{};
        std::array<bool, PassCount> Started{};
        std::array<bool, PassCount> Skipped{};
        // 何フレーム目のクエリか、結果を読み終えていないか
        std::uint64_t FrameNumber = 0;
        bool Pending = false;
    };

    // クエリの組、最後に読めた各段階の結果、このフレームで省いた段階
    std::array<QueryFrame, QueryFrameCount> m_Frames;
    std::array<GpuTiming, PassCount> m_LatestTimings;
    std::array<GpuTimingStatus, PassCount> m_CurrentStatuses;
    // 今記録しているフレーム、次に使う場所、フレームの通し番号、最後に結果を読めたフレーム、結果を読めたことがあるか、初期化したか
    QueryFrame* m_CurrentFrame = nullptr;
    std::size_t m_NextSlot = 0;
    std::uint64_t m_FrameNumber = 0;
    std::uint64_t m_LatestResolvedFrame = 0;
    bool m_HasResolvedFrame = false;
    bool m_Initialized = false;

    // 1フレーム分の結果がGPUから届いていれば読み取っている（届いていなければfalse）
    bool TryResolveFrame(
        ID3D11DeviceContext* context,
        QueryFrame& frame);

public:
    // クエリを作る・解放する
    bool Init(ID3D11Device* device);
    void Uninit();
    // フレームの始まりと終わり（フレーム全体の時間を測っている）
    void BeginFrame(ID3D11DeviceContext* context);
    void EndFrame(ID3D11DeviceContext* context);
    // 描画の段階の始まりと終わりの時刻を記録している
    void BeginPass(GpuPass pass, ID3D11DeviceContext* context);
    void EndPass(GpuPass pass, ID3D11DeviceContext* context);
    // このフレームではその段階を描かなかったことを記録している
    void SkipPass(GpuPass pass);
    // 最後に測れたその段階の結果を返している
    GpuTiming GetTiming(GpuPass pass) const;
    // 同時に使うフレームの数を返している
    static constexpr std::size_t GetQueryFrameCount()
    {
        return QueryFrameCount;
    }
};
