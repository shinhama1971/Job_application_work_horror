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
        static constexpr int MaxGuideLevel = 1;
        // 描画解像度。0=自動（単体GPUは100%、内蔵GPUは67%）、1=100%、2=75%、3=67%。
        static constexpr int MaxResolutionLevel = 3;

    private:
        int m_BrightnessLevel = 2;
        int m_EffectLevel = 1;
        int m_LookSensitivityLevel = 2;
        int m_VolumeLevel = 3;
        // 1で目的表示・目的地ガイドを出し、0で何も説明しないP.T.寄りの遊び方にします。
        int m_GuideLevel = 1;
        // 描画用のテクスチャは起動時に作るため、変更は次回起動から反映されます。
        int m_ResolutionLevel = 0;

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
        // %LOCALAPPDATA%\SignalLost\settings.txt から読み込み・保存します。
        void Load();
        void Save() const;
        // Set系は範囲外の値や同じ値なら何もせずfalseを返します。trueのときだけ保存と反映を行います。

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

        bool SetGuideLevel(int level)
        {
            return TrySetLevel(m_GuideLevel, level, MaxGuideLevel);
        }

        bool SetResolutionLevel(int level)
        {
            return TrySetLevel(m_ResolutionLevel, level, MaxResolutionLevel);
        }

        int GetBrightnessLevel() const { return m_BrightnessLevel; }
        int GetEffectLevel() const { return m_EffectLevel; }
        int GetLookSensitivityLevel() const { return m_LookSensitivityLevel; }
        int GetVolumeLevel() const { return m_VolumeLevel; }
        int GetGuideLevel() const { return m_GuideLevel; }
        bool IsGuideEnabled() const { return m_GuideLevel > 0; }
        int GetResolutionLevel() const { return m_ResolutionLevel; }

        // 描画解像度の倍率です（画面の大きさに掛けます）。自動のときは0を返し、GPUに応じて決めてもらいます。
        float GetRenderScale() const
        {
            constexpr float renderScales[MaxResolutionLevel + 1] =
            {
                0.0f, 1.0f, 0.75f, 0.67f
            };
            return renderScales[m_ResolutionLevel];
        }

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
