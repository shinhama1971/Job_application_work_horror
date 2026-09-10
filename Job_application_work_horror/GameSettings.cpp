// ============================================================================
// ファイルの役割: ユーザー設定を従来と同じ形式で保存・読み込みします。
// ============================================================================

#include "GameSettings.h"

#include <filesystem>
#include <fstream>

namespace Core
{
    void GameSettings::Load()
    {
        std::ifstream settingsFile("save/settings.txt");
        int brightnessLevel = 2;
        int effectLevel = 1;
        int lookSensitivityLevel = 2;
        int volumeLevel = 3;
        if (!(settingsFile >> brightnessLevel))
        {
            return;
        }
        if (brightnessLevel < 0 || brightnessLevel > 4)
        {
            return;
        }

        m_BrightnessLevel = brightnessLevel;
        if (settingsFile >> effectLevel &&
            effectLevel >= 0 && effectLevel <= 2)
        {
            m_EffectLevel = effectLevel;
        }
        if (settingsFile >> lookSensitivityLevel &&
            lookSensitivityLevel >= 0 && lookSensitivityLevel <= 4)
        {
            m_LookSensitivityLevel = lookSensitivityLevel;
        }
        if (settingsFile >> volumeLevel &&
            volumeLevel >= 0 && volumeLevel <= 4)
        {
            m_VolumeLevel = volumeLevel;
        }
    }

    void GameSettings::Save() const
    {
        std::error_code directoryError;
        std::filesystem::create_directories("save", directoryError);
        if (directoryError)
        {
            return;
        }

        std::ofstream settingsFile(
            "save/settings.txt", std::ios::trunc);
        if (!settingsFile)
        {
            return;
        }
        settingsFile << m_BrightnessLevel << ' '
            << m_EffectLevel << ' '
            << m_LookSensitivityLevel << ' '
            << m_VolumeLevel << '\n';
    }
}
