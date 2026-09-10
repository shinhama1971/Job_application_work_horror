// ============================================================================
// ファイルの役割: ユーザー設定値と、その保存・読み込みを管理します。
// 描画・入力・音声システムには依存せず、設定値の範囲だけを保証します。
// ============================================================================

#pragma once

namespace Core
{
    class GameSettings final
    {
    private:
        int m_BrightnessLevel = 2;
        int m_EffectLevel = 1;
        int m_LookSensitivityLevel = 2;
        int m_VolumeLevel = 3;

    public:
        void Load();
        void Save() const;

        bool SetBrightnessLevel(int level)
        {
            if (level < 0 || level > 4 || level == m_BrightnessLevel)
            {
                return false;
            }
            m_BrightnessLevel = level;
            return true;
        }

        bool SetEffectLevel(int level)
        {
            if (level < 0 || level > 2 || level == m_EffectLevel)
            {
                return false;
            }
            m_EffectLevel = level;
            return true;
        }

        bool SetLookSensitivityLevel(int level)
        {
            if (level < 0 || level > 4 || level == m_LookSensitivityLevel)
            {
                return false;
            }
            m_LookSensitivityLevel = level;
            return true;
        }

        bool SetVolumeLevel(int level)
        {
            if (level < 0 || level > 4 || level == m_VolumeLevel)
            {
                return false;
            }
            m_VolumeLevel = level;
            return true;
        }

        int GetBrightnessLevel() const { return m_BrightnessLevel; }
        int GetEffectLevel() const { return m_EffectLevel; }
        int GetLookSensitivityLevel() const { return m_LookSensitivityLevel; }
        int GetVolumeLevel() const { return m_VolumeLevel; }
    };
}
