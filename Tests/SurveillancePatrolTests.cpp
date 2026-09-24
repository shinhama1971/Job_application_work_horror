// ============================================================================
// ファイルの役割: 1面の監視カメラ巡回（SurveillancePatrol）の単体テストです。
// ============================================================================

#include "CppUnitTest.h"
#include "SurveillancePatrol.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SurveillancePatrolTests
{
    using Patrol = SurveillancePatrol;

    Patrol::Anomaly MakeAnomaly(int camera, Patrol::AnomalyType type)
    {
        Patrol::Anomaly anomaly;
        anomaly.camera = camera;
        anomaly.type = type;
        return anomaly;
    }

    TEST_CLASS(ViewingTests)
    {
    public:
        TEST_METHOD(BeginViewingOnlyFromIdle)
        {
            Patrol patrol;

            Assert::IsTrue(patrol.BeginViewing(Patrol::Anomaly{}));
            Assert::IsFalse(patrol.BeginViewing(Patrol::Anomaly{}));
            Assert::IsTrue(patrol.GetState() == Patrol::State::Viewing);
        }

        TEST_METHOD(CameraSelectionWrapsAround)
        {
            Patrol patrol;
            patrol.BeginViewing(Patrol::Anomaly{});

            patrol.SelectCamera(-1, 4);
            Assert::AreEqual(3, patrol.GetSelectedCamera());
            patrol.SelectCamera(1, 4);
            Assert::AreEqual(0, patrol.GetSelectedCamera());
        }

        TEST_METHOD(ReportingCorrectCameraDispatches)
        {
            Patrol patrol;
            patrol.BeginViewing(MakeAnomaly(2, Patrol::AnomalyType::LightOut));
            patrol.SelectCamera(2, 4);

            Assert::IsTrue(patrol.ReportSelectedCamera() ==
                Patrol::ReportResult::Dispatched);
            Assert::IsTrue(patrol.GetState() == Patrol::State::Dispatched);
            Assert::AreEqual(Patrol::DispatchTimeLimit, patrol.GetRemainingTime());
        }

        TEST_METHOD(ReportingWrongCameraIsMistake)
        {
            Patrol patrol;
            patrol.BeginViewing(MakeAnomaly(2, Patrol::AnomalyType::Figure));

            Assert::IsTrue(patrol.ReportSelectedCamera() ==
                Patrol::ReportResult::Wrong);
            Assert::AreEqual(1, patrol.GetMistakes());
            Assert::IsTrue(patrol.GetState() == Patrol::State::Viewing);
        }

        TEST_METHOD(ReportingAnomalyWhenNoneExistsIsMistake)
        {
            Patrol patrol;
            patrol.BeginViewing(Patrol::Anomaly{});

            Assert::IsTrue(patrol.ReportSelectedCamera() ==
                Patrol::ReportResult::Wrong);
        }

        TEST_METHOD(ReportingNoAnomalyCorrectlyClearsRound)
        {
            Patrol patrol;
            patrol.BeginViewing(Patrol::Anomaly{});

            Assert::IsTrue(patrol.ReportNoAnomaly() ==
                Patrol::ReportResult::ClearedNoAnomaly);
            Assert::AreEqual(1, patrol.GetRoundsCleared());
            Assert::IsTrue(patrol.GetState() == Patrol::State::Idle);
        }

        TEST_METHOD(SecondMistakeIsCaughtAndResetsMistakes)
        {
            Patrol patrol;
            patrol.BeginViewing(MakeAnomaly(1, Patrol::AnomalyType::DoorOpen));

            patrol.ReportNoAnomaly();
            Assert::IsTrue(patrol.ReportNoAnomaly() ==
                Patrol::ReportResult::Caught);

            Assert::AreEqual(0, patrol.GetMistakes());
            Assert::IsTrue(patrol.GetState() == Patrol::State::Idle);
            Assert::IsFalse(patrol.GetAnomaly().Exists());
        }
    };

    TEST_CLASS(DispatchTests)
    {
    public:
        static Patrol MakeDispatched()
        {
            Patrol patrol;
            patrol.BeginViewing(MakeAnomaly(0, Patrol::AnomalyType::Figure));
            patrol.ReportSelectedCamera();
            return patrol;
        }

        TEST_METHOD(LookingLongEnoughResolves)
        {
            Patrol patrol = MakeDispatched();

            Assert::IsTrue(patrol.UpdateDispatch(0.3f, true) ==
                Patrol::DispatchResult::None);
            Assert::IsTrue(patrol.UpdateDispatch(0.35f, true) ==
                Patrol::DispatchResult::Resolved);
            Assert::AreEqual(1, patrol.GetRoundsCleared());
            Assert::IsTrue(patrol.GetState() == Patrol::State::Idle);
        }

        TEST_METHOD(LookingAwayResetsConfirmation)
        {
            Patrol patrol = MakeDispatched();
            patrol.UpdateDispatch(0.5f, true);

            patrol.UpdateDispatch(0.1f, false);

            Assert::AreEqual(0.0f, patrol.GetConfirmRate());
            Assert::IsTrue(patrol.UpdateDispatch(0.5f, true) ==
                Patrol::DispatchResult::None);
        }

        TEST_METHOD(TimeoutIsMistakeAndReturnsToIdle)
        {
            Patrol patrol = MakeDispatched();

            Assert::IsTrue(patrol.UpdateDispatch(
                Patrol::DispatchTimeLimit + 1.0f, false) ==
                Patrol::DispatchResult::TimedOut);
            Assert::AreEqual(1, patrol.GetMistakes());
            Assert::IsTrue(patrol.GetState() == Patrol::State::Idle);
            Assert::AreEqual(0, patrol.GetRoundsCleared());
        }

        TEST_METHOD(TimeoutAfterEarlierMistakeIsCaught)
        {
            Patrol patrol;
            patrol.BeginViewing(MakeAnomaly(1, Patrol::AnomalyType::LightOut));
            patrol.ReportSelectedCamera();       // カメラ0を報告して1回目の誤り
            patrol.SelectCamera(1, 4);
            patrol.ReportSelectedCamera();

            Assert::IsTrue(patrol.UpdateDispatch(
                Patrol::DispatchTimeLimit + 1.0f, false) ==
                Patrol::DispatchResult::Caught);
        }

        TEST_METHOD(CompletesAfterRequiredRounds)
        {
            Patrol patrol;
            for (int round = 0; round < Patrol::RequiredRounds; ++round)
            {
                patrol.BeginViewing(Patrol::Anomaly{});
                patrol.ReportNoAnomaly();
            }

            Assert::IsTrue(patrol.IsCompleted());
            Assert::IsFalse(patrol.BeginViewing(Patrol::Anomaly{}));
        }
    };
}
