// ============================================================================
// ファイルの役割: 1面の監視カメラの巡回（映像で異常を見つけ、現地で確かめる）の進み具合を管理している。
// 主な技術: 有限状態機械、間違えた回数で捕まる判定、描画・入力に依存しない進行の処理
// どの異常を出すか（乱数）と、プレイヤーが異常を見ているか（視線の判定）はSceneが渡している。
// ============================================================================

#pragma once

#include <algorithm>

class SurveillancePatrol final
{
public:
    // 巡回の状態
    enum class State
    {
        Idle,       // 端末を操作していない
        Viewing,    // 端末で監視映像を確認している
        Dispatched, // 異常を報告し、現地へ確かめに向かっている
        Completed   // 必要な回数の巡回を終えた
    };

    // 異常の種類
    enum class AnomalyType
    {
        None,       // 異常なし
        Figure,     // 人影が立っている
        LightOut,   // 照明が消えている
        DoorOpen    // 開かないはずの扉が開いている
    };

    // どのカメラに、どの種類の異常があるか
    struct Anomaly
    {
        int camera = -1;
        AnomalyType type = AnomalyType::None;

        bool Exists() const noexcept { return type != AnomalyType::None; }
    };

    enum class ReportResult
    {
        Ignored,         // 映像を確認していない
        Dispatched,      // 正しいカメラを報告し、現地確認へ移った
        ClearedNoAnomaly,// 異常なしを正しく報告した
        Wrong,           // 判定を間違えた
        Caught           // 間違いが上限に達した
    };

    // 現地確認の結果
    enum class DispatchResult
    {
        None,       // 何も起きていない
        Resolved,   // 現地で異常を確かめた
        TimedOut,   // 制限時間を過ぎた
        Caught      // 時間切れで間違いが上限に達した
    };

    // 必要な巡回の回数、捕まるまでの間違いの回数、現地確認の制限時間（秒）
    static constexpr int RequiredRounds = 3;
    static constexpr int MistakesUntilCaught = 2;
    static constexpr float DispatchTimeLimit = 45.0f;
    // たまたま視線が横切っただけで対処したことにならないよう、光を当て続ける時間を設けている。
    static constexpr float ConfirmSeconds = 1.2f;

private:
    // 今の状態、今の異常、選んでいるカメラ、終えた巡回の数、間違えた回数、現地確認の残り秒数、光を当て続けた秒数
    State m_State = State::Idle;
    Anomaly m_Anomaly;
    int m_SelectedCamera = 0;
    int m_RoundsCleared = 0;
    int m_Mistakes = 0;
    float m_RemainingTime = 0.0f;
    float m_ConfirmTime = 0.0f;

    // 間違いを1回数え、上限に達したら捕まったことにして、巡回中の異常を取り消している。
    bool RegisterMistake() noexcept
    {
        ++m_Mistakes;
        if (m_Mistakes < MistakesUntilCaught)
        {
            return false;
        }
        m_Mistakes = 0;
        m_Anomaly = Anomaly{};
        m_State = State::Idle;
        return true;
    }

    // 巡回を1回終え、必要な回数に達したら完了にしている
    void FinishRound() noexcept
    {
        ++m_RoundsCleared;
        m_Anomaly = Anomaly{};
        m_ConfirmTime = 0.0f;
        m_State = m_RoundsCleared >= RequiredRounds
            ? State::Completed
            : State::Idle;
    }

public:
    // 最初の状態に戻している
    void Reset() noexcept { *this = SurveillancePatrol{}; }

    // 端末を操作したときに呼んでいる。異常の有無と場所はSceneが乱数で決めて渡している。
    bool BeginViewing(Anomaly anomaly) noexcept
    {
        if (m_State != State::Idle)
        {
            return false;
        }
        m_State = State::Viewing;
        m_Anomaly = anomaly;
        m_SelectedCamera = 0;
        return true;
    }

    // カメラを左右に切り替えている（端では反対側へ回り込む）
    void SelectCamera(int delta, int cameraCount) noexcept
    {
        if (m_State != State::Viewing || cameraCount <= 0)
        {
            return;
        }
        m_SelectedCamera =
            ((m_SelectedCamera + delta) % cameraCount + cameraCount) % cameraCount;
    }

    // 選んでいるカメラに異常がある、と報告している。
    ReportResult ReportSelectedCamera() noexcept
    {
        if (m_State != State::Viewing)
        {
            return ReportResult::Ignored;
        }
        if (m_Anomaly.Exists() && m_Anomaly.camera == m_SelectedCamera)
        {
            m_State = State::Dispatched;
            m_RemainingTime = DispatchTimeLimit;
            m_ConfirmTime = 0.0f;
            return ReportResult::Dispatched;
        }
        return RegisterMistake() ? ReportResult::Caught : ReportResult::Wrong;
    }

    // どのカメラにも異常がない、と報告している。
    ReportResult ReportNoAnomaly() noexcept
    {
        if (m_State != State::Viewing)
        {
            return ReportResult::Ignored;
        }
        if (!m_Anomaly.Exists())
        {
            FinishRound();
            return ReportResult::ClearedNoAnomaly;
        }
        return RegisterMistake() ? ReportResult::Caught : ReportResult::Wrong;
    }

    // 現地確認の間に毎フレーム呼んでいる。懐中電灯で照らし続けると対処したことになる。
    DispatchResult UpdateDispatch(float deltaTime, bool illuminatingAnomaly) noexcept
    {
        if (m_State != State::Dispatched)
        {
            return DispatchResult::None;
        }

        m_ConfirmTime = illuminatingAnomaly
            ? m_ConfirmTime + deltaTime
            : 0.0f;
        if (m_ConfirmTime >= ConfirmSeconds)
        {
            FinishRound();
            return DispatchResult::Resolved;
        }

        m_RemainingTime -= deltaTime;
        if (m_RemainingTime > 0.0f)
        {
            return DispatchResult::None;
        }

        m_RemainingTime = 0.0f;
        m_Anomaly = Anomaly{};
        m_State = State::Idle;
        return RegisterMistake() ? DispatchResult::Caught : DispatchResult::TimedOut;
    }

    // 状態・異常・選んでいるカメラ・終えた巡回の数・間違えた回数・残り秒数・照らした割合・完了したかを返している
    State GetState() const noexcept { return m_State; }
    const Anomaly& GetAnomaly() const noexcept { return m_Anomaly; }
    int GetSelectedCamera() const noexcept { return m_SelectedCamera; }
    int GetRoundsCleared() const noexcept { return m_RoundsCleared; }
    int GetMistakes() const noexcept { return m_Mistakes; }
    float GetRemainingTime() const noexcept { return m_RemainingTime; }
    float GetConfirmRate() const noexcept
    {
        return (std::clamp)(m_ConfirmTime / ConfirmSeconds, 0.0f, 1.0f);
    }
    bool IsCompleted() const noexcept { return m_State == State::Completed; }
};
