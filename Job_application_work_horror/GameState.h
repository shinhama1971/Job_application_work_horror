// ============================================================================
// ファイルの役割: 1プレイ中の進行統計と、保存対象のベスト記録を保持します。
// ObjectやSceneを所有せず、描画・入力・サウンドにも依存しません。
// ============================================================================

#pragma once

namespace Core
{
    class GameState final
    {
    private:
        int m_ItemCount = 0;
        bool m_PowerRestored = false;
        float m_RunTimeSeconds = 0.0f;
        float m_LastClearTimeSeconds = 0.0f;
        int m_CaughtCount = 0;
        int m_AnomaliesHandled = 0;
        int m_PuzzleMistakes = 0;
        int m_ChargersUsed = 0;
        int m_EvidenceCollected = 0;

        float m_BestClearTimeSeconds = 0.0f;
        int m_BestCaughtCount = 0;
        bool m_HasClearRecord = false;
        bool m_LastRunBestTime = false;
        bool m_LastRunBestCaught = false;

    public:
        // 新しい1面を開始するときだけ、今回のプレイ結果を初期化します。
        // 保存済みのベスト記録は維持します。
        void BeginRun()
        {
            m_ItemCount = 0;
            m_PowerRestored = false;
            m_RunTimeSeconds = 0.0f;
            m_CaughtCount = 0;
            m_AnomaliesHandled = 0;
            m_PuzzleMistakes = 0;
            m_ChargersUsed = 0;
            m_EvidenceCollected = 0;
            m_LastRunBestTime = false;
            m_LastRunBestCaught = false;
        }

        // 2面開始時の値は従来どおり、ヒューズ3個・電源復旧済みに揃えます。
        void EnterStage2()
        {
            m_ItemCount = 3;
            m_PowerRestored = true;
        }

        // リザルトへ入る瞬間に一度だけ今回の結果とベスト記録を確定します。
        void CompleteRun()
        {
            m_LastClearTimeSeconds = m_RunTimeSeconds;
            m_LastRunBestTime = !m_HasClearRecord ||
                m_LastClearTimeSeconds < m_BestClearTimeSeconds;
            m_LastRunBestCaught = !m_HasClearRecord ||
                m_CaughtCount < m_BestCaughtCount;
            if (m_LastRunBestTime)
            {
                m_BestClearTimeSeconds = m_LastClearTimeSeconds;
            }
            if (m_LastRunBestCaught)
            {
                m_BestCaughtCount = m_CaughtCount;
            }
            m_HasClearRecord = true;
        }

        void LoadBestRecord(float clearTimeSeconds, int caughtCount)
        {
            m_BestClearTimeSeconds = clearTimeSeconds;
            m_BestCaughtCount = caughtCount;
            m_HasClearRecord = true;
        }

        void AddRunTime(float seconds) { m_RunTimeSeconds += seconds; }
        void AddItem() { ++m_ItemCount; }
        void SetPowerRestored(bool restored) { m_PowerRestored = restored; }
        void RegisterCaught() { ++m_CaughtCount; }
        void RegisterAnomalyHandled() { ++m_AnomaliesHandled; }
        void RegisterPuzzleMistake() { ++m_PuzzleMistakes; }
        void RegisterChargerUsed() { ++m_ChargersUsed; }
        void RegisterEvidenceCollected() { ++m_EvidenceCollected; }

        int GetItemCount() const { return m_ItemCount; }
        bool IsPowerRestored() const { return m_PowerRestored; }
        float GetRunTimeSeconds() const { return m_RunTimeSeconds; }
        float GetLastClearTimeSeconds() const { return m_LastClearTimeSeconds; }
        int GetCaughtCount() const { return m_CaughtCount; }
        int GetAnomaliesHandled() const { return m_AnomaliesHandled; }
        int GetPuzzleMistakes() const { return m_PuzzleMistakes; }
        int GetChargersUsed() const { return m_ChargersUsed; }
        int GetEvidenceCollected() const { return m_EvidenceCollected; }
        float GetBestClearTimeSeconds() const { return m_BestClearTimeSeconds; }
        int GetBestCaughtCount() const { return m_BestCaughtCount; }
        bool HasClearRecord() const { return m_HasClearRecord; }
        bool IsLastRunBestTime() const { return m_LastRunBestTime; }
        bool IsLastRunBestCaught() const { return m_LastRunBestCaught; }
    };
}
