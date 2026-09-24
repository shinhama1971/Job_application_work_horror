// ============================================================================
// ファイルの役割: ユーザー設定値と、その保存・読み込みを管理します。
// 主な技術: 設定値の正規化、永続化、実行時反映
// 描画・入力・音声システムには依存せず、設定値の範囲と実際の倍率への変換だけを保証します。
// ============================================================================

#pragma once

namespace Core
{
    class GameSettings final
    {
    public:
        static constexpr int MaxBrightnessLevel = 4;
        static constexpr int MaxEffectLevel = 2;
        static constexpr int MaxLookSensitivityLevel = 4;
        static constexpr int MaxVolumeLevel = 4;

    private:
        int m_BrightnessLevel = 2;
        int m_EffectLevel = 1;
        int m_LookSensitivityLevel = 2;
        int m_VolumeLevel = 3;

        static bool TrySetLevel(int& current, int level, int maxLevel)
        {
            if (level < 0 || level > maxLevel || level == current)
            {
                return false;
            }
            current = level;
            return true;
        }

    public:
        void Load();
        void Save() const;

        bool SetBrightnessLevel(int level)
        {
            return TrySetLevel(m_BrightnessLevel, level, MaxBrightnessLevel);
        }

        bool SetEffectLevel(int level)
        {
            return TrySetLevel(m_EffectLevel, level, MaxEffectLevel);
        }

        bool SetLookSensitivityLevel(int level)
        {
            return TrySetLevel(
                m_LookSensitivityLevel, level, MaxLookSensitivityLevel);
        }

        bool SetVolumeLevel(int level)
        {
            return TrySetLevel(m_VolumeLevel, level, MaxVolumeLevel);
        }

        int GetBrightnessLevel() const { return m_BrightnessLevel; }
        int GetEffectLevel() const { return m_EffectLevel; }
        int GetLookSensitivityLevel() const { return m_LookSensitivityLevel; }
        int GetVolumeLevel() const { return m_VolumeLevel; }

        // 段階値を各システムへ渡す倍率へ変換します。変換式はここだけに置きます。
        // 明るさは中央(2)を0とし、1段階ごとに露出を0.055ずらします。
        float GetBrightnessOffset() const
        {
            return static_cast<float>(m_BrightnessLevel - 2) * 0.055f;
        }

        float GetEffectScale() const
        {
            constexpr float effectScales[MaxEffectLevel + 1] =
            {
                0.70f, 1.0f, 1.25f
            };
            return effectScales[m_EffectLevel];
        }

        float GetLookSensitivityScale() const
        {
            return 0.60f + static_cast<float>(m_LookSensitivityLevel) * 0.20f;
        }

        float GetVolumeScale() const
        {
            constexpr float volumeScales[MaxVolumeLevel + 1] =
            {
                0.0f, 0.28f, 0.52f, 0.76f, 1.0f
            };
            return volumeScales[m_VolumeLevel];
        }
    };
}
