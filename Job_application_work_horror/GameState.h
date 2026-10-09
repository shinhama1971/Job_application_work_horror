// ============================================================================
// ファイルの役割: 1回のプレイ中の進行と成績、保存するベスト記録を持っている。
// 主な技術: 描画に依存しないデータの型、プレイの状態を1か所にまとめる設計
// ObjectやSceneを所有せず、描画・入力・音にも依存していない。
// ============================================================================

#pragma once

namespace Core
{
    class GameState final
    {
    public:
        // 残された記録の総数（1面の監視カメラの巡回・1面の隠し部屋の記録端末・2面の端末2台）。
        static constexpr int TotalEvidenceCount = 4;
        // 1面の懐中電灯で浮かぶ壁の文字のうち、読んだ数を数える対象の数（Stage1WallWritings::Count）。
        static constexpr int TotalWallWritingCount = 4;

    private:
        // 1回のプレイ分の進行の値。面をやり直すとき丸ごと巻き戻せるよう、1つにまとめている。
        struct RunProgress
        {
            int ItemCount = 0;           // 1面で拾ったヒューズの数
            bool PowerRestored = false;  // 1面の配電盤で電力を戻したか
            float RunTimeSeconds = 0.0f; // ポーズ中を除いたプレイ時間
            int CaughtCount = 0;         // 捕まった回数
            int AnomaliesHandled = 0;    // 1面・2面で対処した異変の数
            int PuzzleMistakes = 0;      // 2面の信号盤パズルや、1面の監視カメラの確認での失敗回数
            int ChargersUsed = 0;        // 1面・2面で懐中電灯の充電器を使った回数
            int EvidenceCollected = 0;   // 1面・2面で回収した記録の数（TotalEvidenceCount個まで）
            int WallWritingsRead = 0;    // 1面で読んだ壁の文字の数
            bool HiddenRoomEscaped = false; // 1面の隠し部屋から鍵で脱出したか
            int Stage2FirstAnomaly = 0;  // 2面の1周目・2周目に出た異変（Stage2Anomalyの値）
            // （2周目の異変）
            int Stage2SecondAnomaly = 0;
        };

        RunProgress m_Run;
        // 2面に入った瞬間の値。2面のやり直しはこの時点へ戻している。
        RunProgress m_Stage2Start;
        // 前回クリアしたときのタイム
        float m_LastClearTimeSeconds = 0.0f;

        // ベストタイム、ベストの捕まった回数、クリアの記録があるか、前回のプレイでベストを更新したか
        float m_BestClearTimeSeconds = 0.0f;
        int m_BestCaughtCount = 0;
        bool m_HasClearRecord = false;
        bool m_LastRunBestTime = false;
        bool m_LastRunBestCaught = false;

    public:
        // 新しく1面を始めるときだけ、今回のプレイの結果を初期化している。
        // 保存してあるベスト記録はそのまま残している。
        void BeginRun()
        {
            m_Run = RunProgress{};
            m_Stage2Start = RunProgress{};
            m_LastRunBestTime = false;
            m_LastRunBestCaught = false;
        }

        // 2面を始めるときの値は、前の版と同じく、ヒューズ3個・電力が戻った状態にそろえている。
        void EnterStage2()
        {
            m_Run.ItemCount = 3;
            m_Run.PowerRestored = true;
            m_Stage2Start = m_Run;
        }

        // 2面をやり直す前に呼び、2面で増えた時間・捕まった回数・各カウントを取り消している。
        // 取り消さないと、やり直すたびに記録の回収や異変の対処の数を稼げてしまう。
        void RestoreStage2Start()
        {
            m_Run = m_Stage2Start;
        }

        // リザルト画面に入る瞬間に一度だけ、今回の結果とベスト記録を確定している。
        void CompleteRun()
        {
            // クリアタイムが短い・捕まった回数が少ない方をベストとして残している（初めてのクリアなら両方ベスト）
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

        // 保存ファイルから読んだベスト記録を反映している。
        void LoadBestRecord(float clearTimeSeconds, int caughtCount)
        {
            m_BestClearTimeSeconds = clearTimeSeconds;
            m_BestCaughtCount = caughtCount;
            m_HasClearRecord = true;
        }

        // プレイ中の値を増やしたり記録したりする関数
        void AddRunTime(float seconds) { m_Run.RunTimeSeconds += seconds; }
        void AddItem() { ++m_Run.ItemCount; }
        void SetPowerRestored(bool restored) { m_Run.PowerRestored = restored; }
        void RegisterCaught() { ++m_Run.CaughtCount; }
        void RegisterAnomalyHandled() { ++m_Run.AnomaliesHandled; }
        void RegisterPuzzleMistake() { ++m_Run.PuzzleMistakes; }
        void RegisterChargerUsed() { ++m_Run.ChargersUsed; }
        void RegisterEvidenceCollected() { ++m_Run.EvidenceCollected; }
        // 読んだ数は減らないため、多い方だけを残している。
        void SetWallWritingsRead(int count)
        {
            m_Run.WallWritingsRead = count > m_Run.WallWritingsRead ? count : m_Run.WallWritingsRead;
        }
        void RegisterHiddenRoomEscaped() { m_Run.HiddenRoomEscaped = true; }
        void SetStage2Anomalies(int first, int second)
        {
            m_Run.Stage2FirstAnomaly = first;
            m_Run.Stage2SecondAnomaly = second;
        }

        // プレイ中の値とベスト記録を返す関数
        int GetItemCount() const { return m_Run.ItemCount; }
        bool IsPowerRestored() const { return m_Run.PowerRestored; }
        float GetRunTimeSeconds() const { return m_Run.RunTimeSeconds; }
        float GetLastClearTimeSeconds() const { return m_LastClearTimeSeconds; }
        int GetCaughtCount() const { return m_Run.CaughtCount; }
        int GetAnomaliesHandled() const { return m_Run.AnomaliesHandled; }
        int GetPuzzleMistakes() const { return m_Run.PuzzleMistakes; }
        int GetChargersUsed() const { return m_Run.ChargersUsed; }
        int GetEvidenceCollected() const { return m_Run.EvidenceCollected; }
        int GetWallWritingsRead() const { return m_Run.WallWritingsRead; }
        bool IsHiddenRoomEscaped() const { return m_Run.HiddenRoomEscaped; }
        int GetStage2FirstAnomaly() const { return m_Run.Stage2FirstAnomaly; }
        int GetStage2SecondAnomaly() const { return m_Run.Stage2SecondAnomaly; }
        float GetBestClearTimeSeconds() const { return m_BestClearTimeSeconds; }
        int GetBestCaughtCount() const { return m_BestCaughtCount; }
        bool HasClearRecord() const { return m_HasClearRecord; }
        bool IsLastRunBestTime() const { return m_LastRunBestTime; }
        bool IsLastRunBestCaught() const { return m_LastRunBestCaught; }
    };
}
