// ============================================================================
// ファイルの役割: ユーザー設定を従来と同じ形式で保存・読み込みします。
// 主な技術: 設定値の正規化、永続化、実行時反映
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "GameSettings.h"

#include <filesystem>
#include <fstream>

namespace Core
{
    // 処理内容: ファイルからデータを読み込み、利用可能な形へ変換します。
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

    // 処理内容: 現在の状態を次回復元できる形式で保存します。
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
