// ============================================================================
// ファイルの役割: プレイヤーの設定を、前の版と同じ形式（数字を空白で区切った1行）で保存・読み込みしている。
// 主な技術: 設定値の範囲チェック、ファイルへの保存、前の版のファイルとの互換
// ============================================================================

#include "GameSettings.h"

#include <filesystem>
#include <fstream>

#include "utility.h"

namespace Core
{
    // settings.txtを読み込んでいる。値は「明るさ 演出 感度 音量 目的表示 解像度」の順に並んでいる。
    // 範囲外の値は読まずに既定値のままにし、最初の明るさが読めなければ何も変えていない
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
        // 古い形式の設定ファイルには無い項目なので、読めなければ既定値（表示あり）のままにしている。
        int guideLevel = 1;
        if (settingsFile >> guideLevel &&
            guideLevel >= 0 && guideLevel <= MaxGuideLevel)
        {
            m_GuideLevel = guideLevel;
        }
        // 描画解像度も後から追加した項目なので、読めなければ自動のままにしている
        int resolutionLevel = 0;
        if (settingsFile >> resolutionLevel &&
            resolutionLevel >= 0 && resolutionLevel <= MaxResolutionLevel)
        {
            m_ResolutionLevel = resolutionLevel;
        }
    }

    // settings.txtへ、全項目を1行で書き出している（フォルダが無ければ作っている）
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
            << m_VolumeLevel << ' '
            << m_GuideLevel << ' '
            << m_ResolutionLevel << '\n';
    }
}
