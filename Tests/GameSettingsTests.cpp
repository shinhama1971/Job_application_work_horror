// ============================================================================
// ファイルの役割: 設定値の範囲チェックと倍率変換（GameSettings）の単体テストです。
// ファイル入出力（Load/Save）は対象外とし、メモリ上の振る舞いだけを確認します。
// ============================================================================

#include "CppUnitTest.h"
#include "GameSettings.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameSettingsTests
{
    TEST_CLASS(LevelTests)
    {
    public:
        TEST_METHOD(Defaults_AreMiddleValues)
        {
            const Core::GameSettings settings;

            Assert::AreEqual(2, settings.GetBrightnessLevel());
            Assert::AreEqual(1, settings.GetEffectLevel());
            Assert::AreEqual(2, settings.GetLookSensitivityLevel());
            Assert::AreEqual(3, settings.GetVolumeLevel());
        }

        TEST_METHOD(Set_RejectsOutOfRange)
        {
            Core::GameSettings settings;

            Assert::IsFalse(settings.SetBrightnessLevel(-1));
            Assert::IsFalse(settings.SetBrightnessLevel(
                Core::GameSettings::MaxBrightnessLevel + 1));
            Assert::IsFalse(settings.SetEffectLevel(
                Core::GameSettings::MaxEffectLevel + 1));
            Assert::IsFalse(settings.SetLookSensitivityLevel(-1));
            Assert::IsFalse(settings.SetVolumeLevel(
                Core::GameSettings::MaxVolumeLevel + 1));

            Assert::AreEqual(2, settings.GetBrightnessLevel());
            Assert::AreEqual(1, settings.GetEffectLevel());
            Assert::AreEqual(2, settings.GetLookSensitivityLevel());
            Assert::AreEqual(3, settings.GetVolumeLevel());
        }

        TEST_METHOD(Set_ReturnsFalseWhenValueIsUnchanged)
        {
            Core::GameSettings settings;

            // falseなら保存や効果音を鳴らさない、という呼び出し側の前提を確認します。
            Assert::IsFalse(settings.SetBrightnessLevel(2));
            Assert::IsTrue(settings.SetBrightnessLevel(3));
            Assert::AreEqual(3, settings.GetBrightnessLevel());
        }

        TEST_METHOD(Set_AcceptsBoundaryValues)
        {
            Core::GameSettings settings;

            Assert::IsTrue(settings.SetVolumeLevel(0));
            Assert::IsTrue(settings.SetVolumeLevel(
                Core::GameSettings::MaxVolumeLevel));
            Assert::IsTrue(settings.SetEffectLevel(0));
            Assert::IsTrue(settings.SetEffectLevel(
                Core::GameSettings::MaxEffectLevel));
        }
    };

    TEST_CLASS(ScaleTests)
    {
    public:
        TEST_METHOD(BrightnessOffset_IsZeroAtMiddleAndSymmetric)
        {
            Core::GameSettings settings;
            Assert::AreEqual(0.0f, settings.GetBrightnessOffset(), 1e-6f);

            settings.SetBrightnessLevel(0);
            const float darkest = settings.GetBrightnessOffset();
            settings.SetBrightnessLevel(4);
            const float brightest = settings.GetBrightnessOffset();

            Assert::AreEqual(-0.11f, darkest, 1e-6f);
            Assert::AreEqual(0.11f, brightest, 1e-6f);
        }

        TEST_METHOD(EffectScale_MatchesEachLevel)
        {
            Core::GameSettings settings;
            settings.SetEffectLevel(0);
            Assert::AreEqual(0.70f, settings.GetEffectScale(), 1e-6f);
            settings.SetEffectLevel(1);
            Assert::AreEqual(1.0f, settings.GetEffectScale(), 1e-6f);
            settings.SetEffectLevel(2);
            Assert::AreEqual(1.25f, settings.GetEffectScale(), 1e-6f);
        }

        TEST_METHOD(LookSensitivityScale_IncreasesByStep)
        {
            Core::GameSettings settings;
            settings.SetLookSensitivityLevel(0);
            Assert::AreEqual(0.60f, settings.GetLookSensitivityScale(), 1e-6f);
            settings.SetLookSensitivityLevel(4);
            Assert::AreEqual(1.40f, settings.GetLookSensitivityScale(), 1e-6f);
        }

        TEST_METHOD(VolumeScale_IsMutedAtZeroAndFullAtMax)
        {
            Core::GameSettings settings;
            settings.SetVolumeLevel(0);
            Assert::AreEqual(0.0f, settings.GetVolumeScale());
            settings.SetVolumeLevel(Core::GameSettings::MaxVolumeLevel);
            Assert::AreEqual(1.0f, settings.GetVolumeScale());
        }
    };
}
