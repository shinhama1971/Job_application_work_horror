// ============================================================================
// ファイルの役割: 1プレイの進行値とベスト記録（GameState）の単体テストです。
// ============================================================================

#include "CppUnitTest.h"
#include "GameState.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameStateTests
{
    TEST_CLASS(RunProgressTests)
    {
    public:
        TEST_METHOD(BeginRun_ResetsProgressButKeepsBestRecord)
        {
            Core::GameState state;
            state.LoadBestRecord(120.0f, 2);
            state.AddRunTime(30.0f);
            state.AddItem();
            state.RegisterCaught();
            state.RegisterEvidenceCollected();

            state.BeginRun();

            Assert::AreEqual(0.0f, state.GetRunTimeSeconds());
            Assert::AreEqual(0, state.GetItemCount());
            Assert::AreEqual(0, state.GetCaughtCount());
            Assert::AreEqual(0, state.GetEvidenceCollected());
            Assert::IsTrue(state.HasClearRecord());
            Assert::AreEqual(120.0f, state.GetBestClearTimeSeconds());
            Assert::AreEqual(2, state.GetBestCaughtCount());
        }

        TEST_METHOD(EnterStage2_SetsFusesAndPower)
        {
            Core::GameState state;
            state.BeginRun();

            state.EnterStage2();

            Assert::AreEqual(3, state.GetItemCount());
            Assert::IsTrue(state.IsPowerRestored());
        }

        // 2面のやり直しでボーナスを稼げた不具合の回帰テストです。
        TEST_METHOD(RestoreStage2Start_DiscardsProgressMadeInStage2)
        {
            Core::GameState state;
            state.BeginRun();
            state.AddRunTime(100.0f);
            state.RegisterCaught();
            state.RegisterEvidenceCollected();
            state.EnterStage2();

            state.AddRunTime(50.0f);
            state.RegisterCaught();
            state.RegisterAnomalyHandled();
            state.RegisterPuzzleMistake();
            state.RegisterChargerUsed();
            state.RegisterEvidenceCollected();
            state.RegisterEvidenceCollected();

            state.RestoreStage2Start();

            Assert::AreEqual(100.0f, state.GetRunTimeSeconds());
            Assert::AreEqual(1, state.GetCaughtCount());
            Assert::AreEqual(0, state.GetAnomaliesHandled());
            Assert::AreEqual(0, state.GetPuzzleMistakes());
            Assert::AreEqual(0, state.GetChargersUsed());
            Assert::AreEqual(1, state.GetEvidenceCollected());
            Assert::AreEqual(3, state.GetItemCount());
            Assert::IsTrue(state.IsPowerRestored());
        }

        TEST_METHOD(RestoreStage2Start_CanBeRepeated)
        {
            Core::GameState state;
            state.BeginRun();
            state.EnterStage2();

            for (int retry = 0; retry < 3; ++retry)
            {
                state.RegisterEvidenceCollected();
                state.RestoreStage2Start();
                state.EnterStage2();
            }

            Assert::AreEqual(0, state.GetEvidenceCollected());
        }
    };

    TEST_CLASS(BestRecordTests)
    {
    public:
        TEST_METHOD(CompleteRun_FirstClearBecomesBest)
        {
            Core::GameState state;
            state.BeginRun();
            state.AddRunTime(200.0f);
            state.RegisterCaught();

            state.CompleteRun();

            Assert::IsTrue(state.HasClearRecord());
            Assert::IsTrue(state.IsLastRunBestTime());
            Assert::IsTrue(state.IsLastRunBestCaught());
            Assert::AreEqual(200.0f, state.GetLastClearTimeSeconds());
            Assert::AreEqual(200.0f, state.GetBestClearTimeSeconds());
            Assert::AreEqual(1, state.GetBestCaughtCount());
        }

        TEST_METHOD(CompleteRun_SlowerRunKeepsPreviousBestTime)
        {
            Core::GameState state;
            state.LoadBestRecord(150.0f, 3);
            state.BeginRun();
            state.AddRunTime(180.0f);

            state.CompleteRun();

            Assert::IsFalse(state.IsLastRunBestTime());
            Assert::AreEqual(150.0f, state.GetBestClearTimeSeconds());
            Assert::AreEqual(180.0f, state.GetLastClearTimeSeconds());
            // 捕獲0回は3回より良いので、時間とは独立して更新されます。
            Assert::IsTrue(state.IsLastRunBestCaught());
            Assert::AreEqual(0, state.GetBestCaughtCount());
        }

        TEST_METHOD(CompleteRun_FasterRunUpdatesBestTime)
        {
            Core::GameState state;
            state.LoadBestRecord(150.0f, 0);
            state.BeginRun();
            state.AddRunTime(90.0f);
            state.RegisterCaught();

            state.CompleteRun();

            Assert::IsTrue(state.IsLastRunBestTime());
            Assert::AreEqual(90.0f, state.GetBestClearTimeSeconds());
            Assert::IsFalse(state.IsLastRunBestCaught());
            Assert::AreEqual(0, state.GetBestCaughtCount());
        }
    };
}
