// ============================================================================
// ファイルの役割: プレイヤーの設定を、前の版と同じ形式（数字を空白で区切った1行）で保存・読み込みしている。
// 主な技術: 設定値の範囲チェック、ファイルへの保存、前の版のファイルとの互換
// ============================================================================

#include "GameSettings.h"

#include <fstream>
#include <sstream>

#include "utility.h"

namespace Core
{
    namespace
    {
        constexpr char SettingsFileName[] = "settings.txt";
    }

    // settings.txtを読み込んでいる。値は「明るさ 演出 感度 音量 目的表示 解像度」の順に並んでいる。
    // 値を1項目ずつ検証する。壊れた値や古い形式で欠けている項目は、現在値を使う。
    void GameSettings::Load()
    {
        std::ifstream settingsFile(utility::ResolveSaveFileForRead(SettingsFileName));
        const auto loadLevel = [&settingsFile](int& current, int maximum)
        {
            std::string token;
            if (settingsFile >> token)
            {
                std::istringstream value(token);
                int level = 0;
                if (value >> level && value.eof())
                {
                    TrySetLevel(current, level, maximum);
                }
            }
        };
        loadLevel(m_BrightnessLevel, MaxBrightnessLevel);
        loadLevel(m_EffectLevel, MaxEffectLevel);
        loadLevel(m_LookSensitivityLevel, MaxLookSensitivityLevel);
        loadLevel(m_VolumeLevel, MaxVolumeLevel);
        loadLevel(m_GuideLevel, MaxGuideLevel);
        loadLevel(m_ResolutionLevel, MaxResolutionLevel);
    }

    // settings.txtへ、全項目を1行で書き出している（フォルダが無ければ作っている）
    bool GameSettings::Save() const
    {
        std::ostringstream text;
        text << m_BrightnessLevel << ' '
            << m_EffectLevel << ' '
            << m_LookSensitivityLevel << ' '
            << m_VolumeLevel << ' '
            << m_GuideLevel << ' '
            << m_ResolutionLevel << "\r\n";
        return utility::WriteSaveFile(SettingsFileName, text.str());
    }
}
