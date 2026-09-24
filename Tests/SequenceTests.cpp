// ============================================================================
// ファイルの役割: 時間とフェーズで進む演出シーケンスの単体テストです。
// 発火時刻、発火順序、重複発火しないこと、キャンセルを確認します。
// ============================================================================

#include "CppUnitTest.h"
#include "CaughtSequence.h"
#include "ExitOmenSequence.h"
#include "ScareLightSequence.h"
#include "StagePowerSequence.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SequenceTests
{
    TEST_CLASS(ScareLightSequenceTests)
    {
    public:
        TEST_METHOD(IsInactiveUntilStarted)
        {
            ScareLightSequence sequence;
            sequence.Advance(10.0f);

            Assert::IsFalse(sequence.IsActive());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());
        }

        TEST_METHOD(BeatsFireOnceInOrderAtTheirTimes)
        {
            ScareLightSequence sequence;
            sequence.Start();

            sequence.Advance(0.44f);
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());

            sequence.Advance(0.02f);
            Assert::AreEqual(0, sequence.ConsumePendingBeat());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());

            sequence.Advance(0.50f);
            Assert::AreEqual(1, sequence.ConsumePendingBeat());
        }

        TEST_METHOD(BecomesInactiveAfterLastBeat)
        {
            ScareLightSequence sequence;
            sequence.Start();
            sequence.Advance(3.0f);

            // 大きく時間が進んでも、1回の呼び出しで1拍ずつ順に返します。
            for (int expected = 0; expected < 5; ++expected)
            {
                Assert::AreEqual(expected, sequence.ConsumePendingBeat());
            }
            Assert::IsFalse(sequence.IsActive());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());
        }

        TEST_METHOD(CancelStopsRemainingBeats)
        {
            ScareLightSequence sequence;
            sequence.Start();
            sequence.Advance(0.5f);
            Assert::AreEqual(0, sequence.ConsumePendingBeat());

            sequence.Cancel();
            sequence.Advance(5.0f);

            Assert::IsFalse(sequence.IsActive());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());
        }

        TEST_METHOD(NoticeCountsDownIndependently)
        {
            ScareLightSequence sequence;
            sequence.Start();
            Assert::AreEqual(3.8f, sequence.GetNoticeTimer(), 1e-6f);

            sequence.UpdateNotice(1.0f);
            Assert::AreEqual(2.8f, sequence.GetNoticeTimer(), 1e-5f);

            sequence.ClearNotice();
            Assert::AreEqual(0.0f, sequence.GetNoticeTimer());
        }
    };

    TEST_CLASS(StagePowerSequenceTests)
    {
    public:
        TEST_METHOD(RestoreStartsOnlyOnRisingEdge)
        {
            StagePowerSequence sequence;

            Assert::IsFalse(sequence.ObservePowerState(false));
            Assert::IsTrue(sequence.ObservePowerState(true));
            Assert::IsFalse(sequence.ObservePowerState(true));
            Assert::IsTrue(sequence.IsRestoreActive());
        }

        TEST_METHOD(RestoreBeatsFireInOrder)
        {
            StagePowerSequence sequence;
            sequence.ObservePowerState(true);

            sequence.AdvanceRestore(0.37f);
            Assert::AreEqual(-1, sequence.ConsumeRestoreBeat());
            sequence.AdvanceRestore(0.02f);
            Assert::AreEqual(0, sequence.ConsumeRestoreBeat());
            sequence.AdvanceRestore(2.0f);
            Assert::AreEqual(1, sequence.ConsumeRestoreBeat());
            Assert::AreEqual(2, sequence.ConsumeRestoreBeat());
            Assert::AreEqual(-1, sequence.ConsumeRestoreBeat());
        }

        TEST_METHOD(ExitBeginsOnceAndCompletesAfterThreeBeats)
        {
            StagePowerSequence sequence;

            Assert::IsTrue(sequence.BeginExitIfNeeded());
            Assert::IsFalse(sequence.BeginExitIfNeeded());

            sequence.AdvanceExit(2.0f);
            Assert::AreEqual(0, sequence.ConsumeExitBeat());
            Assert::AreEqual(1, sequence.ConsumeExitBeat());
            Assert::IsFalse(sequence.IsExitComplete());
            Assert::AreEqual(2, sequence.ConsumeExitBeat());
            Assert::IsTrue(sequence.IsExitComplete());
            Assert::AreEqual(-1, sequence.ConsumeExitBeat());
        }

        TEST_METHOD(ResetAllowsRestoreToStartAgain)
        {
            StagePowerSequence sequence;
            sequence.ObservePowerState(true);

            sequence.Reset();

            Assert::IsFalse(sequence.IsRestoreActive());
            Assert::IsTrue(sequence.ObservePowerState(true));
        }
    };

    TEST_CLASS(ExitOmenSequenceTests)
    {
    public:
        TEST_METHOD(BeatsFireAsRemainingTimeDecreases)
        {
            ExitOmenSequence sequence;
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());

            sequence.Start();
            Assert::IsTrue(sequence.IsTriggered());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());

            sequence.Update(0.80f);   // 残り2.40秒
            Assert::AreEqual(0, sequence.ConsumePendingBeat());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());

            sequence.Update(1.10f);   // 残り1.30秒
            Assert::AreEqual(1, sequence.ConsumePendingBeat());
            Assert::AreEqual(-1, sequence.ConsumePendingBeat());
        }

        TEST_METHOD(TimerNeverGoesNegative)
        {
            ExitOmenSequence sequence;
            sequence.Start();

            sequence.Update(100.0f);

            Assert::AreEqual(0.0f, sequence.GetTimer());
        }
    };

    TEST_CLASS(CaughtSequenceTests)
    {
    public:
        TEST_METHOD(StartIsIgnoredWhileActive)
        {
            CaughtSequence sequence;

            Assert::IsTrue(sequence.Start(CaughtSequence::Reason::NoiseStalker));
            Assert::IsFalse(sequence.Start(CaughtSequence::Reason::FinalPursuit));
            Assert::IsTrue(sequence.WasNoiseStalker());
        }

        TEST_METHOD(RecoveryAndFadeFollowElapsedTime)
        {
            CaughtSequence sequence;
            sequence.Start(CaughtSequence::Reason::FinalPursuit);

            sequence.Advance(0.17f);
            Assert::AreEqual(0.5f, sequence.GetFadeRate(), 1e-5f);
            Assert::IsFalse(sequence.IsReadyToRecover());

            sequence.Advance(1.0f);
            Assert::AreEqual(1.0f, sequence.GetFadeRate());
            Assert::IsTrue(sequence.IsReadyToRecover());
        }

        TEST_METHOD(AdvanceWhileInactiveDoesNotStart)
        {
            CaughtSequence sequence;

            sequence.Advance(5.0f);

            Assert::IsFalse(sequence.IsActive());
            Assert::IsFalse(sequence.IsReadyToRecover());
        }

        TEST_METHOD(CompleteAllowsNextStart)
        {
            CaughtSequence sequence;
            sequence.Start(CaughtSequence::Reason::FinalPursuit);

            sequence.Complete();

            Assert::IsFalse(sequence.IsActive());
            Assert::IsTrue(sequence.Start(CaughtSequence::Reason::NoiseStalker));
        }
    };
}
