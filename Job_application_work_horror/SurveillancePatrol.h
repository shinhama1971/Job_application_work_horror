// ============================================================================
// ファイルの役割: 1面の監視カメラ巡回（映像で異常を見つけ、現地で確認する）の進行を管理します。
// 主な技術: 有限状態機械、ミス回数による捕獲判定、描画・入力に依存しない進行ロジック
// どの異常を出すか（乱数）と、プレイヤーが異常を見ているか（視線判定）はSceneが渡します。
// ============================================================================

#pragma once

#include <algorithm>

class SurveillancePatrol final
{
public:
    enum class State
    {
        Idle,       // 端末を操作していない
        Viewing,    // 端末で監視映像を確認中
        Dispatched, // 異常を報告し、現地へ確認に向かっている
        Completed   // 必要な回数の巡回を終えた
    };

    enum class AnomalyType
    {
        None,
        Figure,     // 人影が立っている
        LightOut,   // 照明が消えている
        DoorOpen    // 開かないはずの扉が開いている
    };

    struct Anomaly
    {
        int camera = -1;
        AnomalyType type = AnomalyType::None;

        bool Exists() const noexcept { return type != AnomalyType::None; }
    };

    enum class ReportResult
    {
        Ignored,         // 映像確認中ではない
        Dispatched,      // 正しいカメラを報告し、現地確認へ移った
        ClearedNoAnomaly,// 異常なしを正しく報告した
        Wrong,           // 判定を誤った
        Caught           // 誤りが上限に達した
    };

    enum class DispatchResult
    {
        None,
        Resolved,   // 現地で異常を確認した
        TimedOut,   // 制限時間を過ぎた
        Caught      // 時間切れで誤りが上限に達した
    };

    static constexpr int RequiredRounds = 3;
    static constexpr int MistakesUntilCaught = 2;
    static constexpr float DispatchTimeLimit = 45.0f;
    // 偶然視線が横切っただけで対処済みにならないよう、光を当て続ける時間を設けます。
    static constexpr float ConfirmSeconds = 1.2f;

private:
    State m_State = State::Idle;
    Anomaly m_Anomaly;
    int m_SelectedCamera = 0;
    int m_RoundsCleared = 0;
    int m_Mistakes = 0;
    float m_RemainingTime = 0.0f;
    float m_ConfirmTime = 0.0f;

    // 誤りを1回数え、上限に達したら捕獲として巡回中の異常を取り消します。
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
    void Reset() noexcept { *this = SurveillancePatrol{}; }

    // 端末を操作したときに呼びます。異常の有無と場所はSceneが乱数で決めて渡します。
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

    void SelectCamera(int delta, int cameraCount) noexcept
    {
        if (m_State != State::Viewing || cameraCount <= 0)
        {
            return;
        }
        m_SelectedCamera =
            ((m_SelectedCamera + delta) % cameraCount + cameraCount) % cameraCount;
    }

    // 選択中のカメラに異常がある、と報告します。
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

    // どのカメラにも異常がない、と報告します。
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

    // 現地確認中に毎フレーム呼びます。懐中電灯で照らし続けると対処完了になります。
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
