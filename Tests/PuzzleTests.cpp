// ============================================================================
// ファイルの役割: 2面の信号盤パズル・失敗通知・静止による回復判定の単体テストです。
// ============================================================================

#include "CppUnitTest.h"
#include "PuzzleFeedback.h"
#include "QuietRecovery.h"
#include "SignalPuzzle.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PuzzleTests
{
    TEST_CLASS(SignalPuzzleTests)
    {
    public:
        TEST_METHOD(CorrectOrderCompletesPuzzle)
        {
            SignalPuzzle puzzle;

            Assert::IsTrue(puzzle.Accept(0) == SignalPuzzle::AcceptResult::Accepted);
            Assert::IsTrue(puzzle.Accept(1) == SignalPuzzle::AcceptResult::Accepted);
            Assert::IsTrue(puzzle.Accept(2) == SignalPuzzle::AcceptResult::Completed);

            Assert::IsTrue(puzzle.IsComplete());
            Assert::AreEqual(3, puzzle.GetStep());
        }

        TEST_METHOD(WrongOrderResetsProgress)
        {
            SignalPuzzle puzzle;
            puzzle.Accept(0);

            Assert::IsTrue(puzzle.Accept(2) == SignalPuzzle::AcceptResult::WrongOrder);

            Assert::AreEqual(0, puzzle.GetStep());
            Assert::IsFalse(puzzle.IsAccepted(0));
            Assert::IsFalse(puzzle.IsComplete());
        }

        TEST_METHOD(OutOfRangeSignalIsTreatedAsWrongOrder)
        {
            SignalPuzzle puzzle;

            Assert::IsTrue(puzzle.Accept(-1) == SignalPuzzle::AcceptResult::WrongOrder);
            Assert::IsTrue(puzzle.Accept(3) == SignalPuzzle::AcceptResult::WrongOrder);
            Assert::IsFalse(puzzle.IsAccepted(3));
        }

        TEST_METHOD(ForceCompleteMarksAllSignals)
        {
            SignalPuzzle puzzle;

            puzzle.ForceComplete();

            Assert::IsTrue(puzzle.IsComplete());
            for (int signal = 0; signal < 3; ++signal)
            {
                Assert::IsTrue(puzzle.IsAccepted(signal));
            }
        }
    };

    TEST_CLASS(PuzzleFeedbackTests)
    {
    public:
        TEST_METHOD(MistakeIsIgnoredWhileNoticeIsVisible)
        {
            PuzzleFeedback feedback;

            Assert::IsTrue(feedback.TryRegisterMistake(1));
            Assert::IsFalse(feedback.TryRegisterMistake(2));
            Assert::AreEqual(1, feedback.GetType());
            Assert::AreEqual(1, feedback.GetMistakeCount());

            feedback.Update(2.3f);
            Assert::IsFalse(feedback.IsVisible());
            Assert::IsTrue(feedback.TryRegisterMistake(2));
            Assert::AreEqual(2, feedback.GetMistakeCount());
        }

        TEST_METHOD(MistakeCountIsCappedAtThree)
        {
            PuzzleFeedback feedback;
            for (int i = 0; i < 5; ++i)
            {
                feedback.TryRegisterMistake(1);
                feedback.Clear();
            }

            Assert::AreEqual(3, feedback.GetMistakeCount());
        }
    };

    TEST_CLASS(QuietRecoveryTests)
    {
    public:
        static bool StepFrames(QuietRecovery& recovery, int frames,
            bool stationary = true, bool lightOff = true, bool nearbyThreat = false)
        {
            // 1フレーム0.05秒で進め、途中で成功したらtrueを返します。
            // 浮動小数点の累積誤差に依存しないよう、境界はフレーム数に余裕を持たせて判定します。
            bool succeeded = false;
            for (int frame = 0; frame < frames; ++frame)
            {
                if (recovery.Update(0.05f, true, stationary, lightOff, nearbyThreat))
                {
                    succeeded = true;
                }
            }
            return succeeded;
        }

        TEST_METHOD(SucceedsAfterStayingStillInTheDarkForTwoSeconds)
        {
            QuietRecovery recovery;

            Assert::IsFalse(StepFrames(recovery, 38));   // 1.9秒
            Assert::IsTrue(StepFrames(recovery, 4));     // 合計2.1秒
            // 成功直後のフレームでクールダウンが始まっています。
            Assert::IsTrue(recovery.cooldown > QuietRecovery::CooldownSeconds - 0.2f);
        }

        TEST_METHOD(MovingResetsProgress)
        {
            QuietRecovery recovery;
            StepFrames(recovery, 30);

            recovery.Update(0.05f, true, false, true, false);

            Assert::AreEqual(0.0f, recovery.progress);
        }

        TEST_METHOD(NearbyThreatBlocksRecoveryAndSetsTooClose)
        {
            QuietRecovery recovery;

            Assert::IsFalse(StepFrames(recovery, 60, true, true, true));
            Assert::IsTrue(recovery.tooClose);
        }

        TEST_METHOD(CooldownPreventsImmediateSecondSuccess)
        {
            QuietRecovery recovery;
            Assert::IsTrue(StepFrames(recovery, 42));

            Assert::IsFalse(StepFrames(recovery, 100));   // 5秒はクールダウン10秒の途中
        }

        TEST_METHOD(LargeFrameTimeIsClampedToAvoidInstantSuccess)
        {
            QuietRecovery recovery;

            // 停止復帰などで1フレームが3秒でも、0.1秒分しか進みません。
            Assert::IsFalse(recovery.Update(3.0f, true, true, true, false));
            Assert::AreEqual(0.1f, recovery.progress, 1e-6f);
        }
    };
}
