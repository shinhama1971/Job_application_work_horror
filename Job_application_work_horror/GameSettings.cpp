// ============================================================================
// ファイルの役割: ユーザー設定を従来と同じ形式で保存・読み込みします。
// 主な技術: 設定値の正規化、永続化、実行時反映
// ============================================================================

#include "GameSettings.h"

#include <filesystem>
#include <fstream>

#include "utility.h"

namespace Core
{
    void GameSettings::Load()
    {
        std::ifstream settingsFile(
            utility::ResolveSaveFileForRead("settings.txt"));
        int brightnessLevel = 2;
        int effectLevel = 1;
        int lookSensitivityLevel = 2;
        int volumeLevel = 3;
        if (!(settingsFile >> brightnessLevel))
        {
            return;
        }
        if (brightnessLevel < 0 || brightnessLevel > MaxBrightnessLevel)
        {
            return;
        }

        m_BrightnessLevel = brightnessLevel;
        if (settingsFile >> effectLevel &&
            effectLevel >= 0 && effectLevel <= MaxEffectLevel)
        {
            m_EffectLevel = effectLevel;
        }
        if (settingsFile >> lookSensitivityLevel &&
            lookSensitivityLevel >= 0 && lookSensitivityLevel <= MaxLookSensitivityLevel)
        {
            m_LookSensitivityLevel = lookSensitivityLevel;
        }
        if (settingsFile >> volumeLevel &&
            volumeLevel >= 0 && volumeLevel <= MaxVolumeLevel)
        {
            m_VolumeLevel = volumeLevel;
        }
    }

    void GameSettings::Save() const
    {
        std::error_code directoryError;
        const std::filesystem::path saveDirectory = utility::GetSaveDirectory();
        std::filesystem::create_directories(saveDirectory, directoryError);
        if (directoryError)
        {
            return;
        }

        std::ofstream settingsFile(
            saveDirectory / "settings.txt", std::ios::trunc);
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
