// ============================================================================
// ファイルの役割: 1プレイ中の進行統計と、保存対象のベスト記録を保持します。
// 主な技術: 描画非依存のデータモデル、列挙型、状態の一元管理
// ObjectやSceneを所有せず、描画・入力・サウンドにも依存しません。
// ============================================================================

#pragma once

namespace Core
{
    class GameState final
    {
    private:
        // 1プレイ分の進行値。階のやり直しで丸ごと巻き戻せるようにまとめています。
        struct RunProgress
        {
            int ItemCount = 0;
            bool PowerRestored = false;
            float RunTimeSeconds = 0.0f;
            int CaughtCount = 0;
            int AnomaliesHandled = 0;
            int PuzzleMistakes = 0;
            int ChargersUsed = 0;
            int EvidenceCollected = 0;
        };

        RunProgress m_Run;
        // 2面へ入った瞬間の値。2面のやり直しはこの時点へ戻します。
        RunProgress m_Stage2Start;
        float m_LastClearTimeSeconds = 0.0f;

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
            m_Run = RunProgress{};
            m_Stage2Start = RunProgress{};
            m_LastRunBestTime = false;
            m_LastRunBestCaught = false;
        }

        // 2面開始時の値は従来どおり、ヒューズ3個・電源復旧済みに揃えます。
        void EnterStage2()
        {
            m_Run.ItemCount = 3;
            m_Run.PowerRestored = true;
            m_Stage2Start = m_Run;
        }

        // 2面をやり直す前に呼び、2面で加算された時間・捕獲数・各カウントを取り消します。
        // 取り消さないと、やり直すたびに記録回収や異変対処のボーナスを稼げてしまいます。
        void RestoreStage2Start()
        {
            m_Run = m_Stage2Start;
        }

        // リザルトへ入る瞬間に一度だけ今回の結果とベスト記録を確定します。
        void CompleteRun()
        {
            m_LastClearTimeSeconds = m_Run.RunTimeSeconds;
            m_LastRunBestTime = !m_HasClearRecord ||
                m_LastClearTimeSeconds < m_BestClearTimeSeconds;
            m_LastRunBestCaught = !m_HasClearRecord ||
                m_Run.CaughtCount < m_BestCaughtCount;
            if (m_LastRunBestTime)
            {
                m_BestClearTimeSeconds = m_LastClearTimeSeconds;
            }
            if (m_LastRunBestCaught)
            {
                m_BestCaughtCount = m_Run.CaughtCount;
            }
            m_HasClearRecord = true;
        }

        void LoadBestRecord(float clearTimeSeconds, int caughtCount)
        {
            m_BestClearTimeSeconds = clearTimeSeconds;
            m_BestCaughtCount = caughtCount;
            m_HasClearRecord = true;
        }

        void AddRunTime(float seconds) { m_Run.RunTimeSeconds += seconds; }
        void AddItem() { ++m_Run.ItemCount; }
        void SetPowerRestored(bool restored) { m_Run.PowerRestored = restored; }
        void RegisterCaught() { ++m_Run.CaughtCount; }
        void RegisterAnomalyHandled() { ++m_Run.AnomaliesHandled; }
        void RegisterPuzzleMistake() { ++m_Run.PuzzleMistakes; }
        void RegisterChargerUsed() { ++m_Run.ChargersUsed; }
        void RegisterEvidenceCollected() { ++m_Run.EvidenceCollected; }

        int GetItemCount() const { return m_Run.ItemCount; }
        bool IsPowerRestored() const { return m_Run.PowerRestored; }
        float GetRunTimeSeconds() const { return m_Run.RunTimeSeconds; }
        float GetLastClearTimeSeconds() const { return m_LastClearTimeSeconds; }
        int GetCaughtCount() const { return m_Run.CaughtCount; }
        int GetAnomaliesHandled() const { return m_Run.AnomaliesHandled; }
        int GetPuzzleMistakes() const { return m_Run.PuzzleMistakes; }
        int GetChargersUsed() const { return m_Run.ChargersUsed; }
        int GetEvidenceCollected() const { return m_Run.EvidenceCollected; }
        float GetBestClearTimeSeconds() const { return m_BestClearTimeSeconds; }
        int GetBestCaughtCount() const { return m_BestCaughtCount; }
        bool HasClearRecord() const { return m_HasClearRecord; }
        bool IsLastRunBestTime() const { return m_LastRunBestTime; }
        bool IsLastRunBestCaught() const { return m_LastRunBestCaught; }
    };
}
