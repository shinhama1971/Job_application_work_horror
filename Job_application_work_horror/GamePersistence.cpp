// ============================================================================
// ファイルの役割: 音量設定とクリア記録の保存・読み込みを管理します。
// ============================================================================

#include "Game.h"

#include <filesystem>
#include <fstream>

namespace Core
{
    void Game::ApplyAudioVolume(bool paused)
    {
        if (!m_SoundReady)
        {
            return;
        }

        constexpr float volumeScales[] =
        {
            0.0f, 0.28f, 0.52f, 0.76f, 1.0f
        };
        const float pauseScale = paused ? 0.42f : 1.0f;
        m_Sound.SetMasterVolume(
            volumeScales[m_VolumeLevel] * pauseScale);
    }

    void Game::LoadBestRecord()
    {
        std::ifstream recordFile("save/best_record.txt");
        float clearTime = 0.0f;
        int caughtCount = 0;
        if (!(recordFile >> clearTime >> caughtCount))
        {
            return;
        }
        if (clearTime <= 0.0f || clearTime > 86400.0f ||
            caughtCount < 0 || caughtCount > 999)
        {
            return;
        }

        m_BestClearTimeSeconds = clearTime;
        m_BestCaughtCount = caughtCount;
        m_HasClearRecord = true;
    }

    void Game::SaveBestRecord() const
    {
        std::error_code directoryError;
        std::filesystem::create_directories("save", directoryError);
        if (directoryError)
        {
            return;
        }

        std::ofstream recordFile(
            "save/best_record.txt", std::ios::trunc);
        if (!recordFile)
        {
            return;
        }
        recordFile << m_BestClearTimeSeconds << ' '
            << m_BestCaughtCount << '\n';
    }

    void Game::LoadSettings()
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

    void Game::SaveSettings() const
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

