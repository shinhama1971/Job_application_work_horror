// ============================================================================
// ファイルの役割: プレイヤーが変えられる設定値と、その保存・読み込みを管理している。
// 主な技術: 設定値の範囲チェック、ファイルへの保存、段階の番号から実際の倍率への変換
// 描画・入力・音の仕組みには依存せず、設定値の範囲と、実際の倍率への変換だけを受け持っている。
// ============================================================================

#pragma once

namespace Core
{
    // ポーズメニューで変える設定（明るさ・演出の強さ・視点の感度・音量・目的表示・描画解像度）。値はどれも段階の番号で持っている。
    class GameSettings final
    {
    public:
        // 各設定の段階の最大値（明るさ0〜4、演出の強さ0〜2、視点の感度0〜4、音量0〜4、目的表示0〜1）
        static constexpr int MaxBrightnessLevel = 4;
        static constexpr int MaxEffectLevel = 2;
        static constexpr int MaxLookSensitivityLevel = 4;
        static constexpr int MaxVolumeLevel = 4;
        static constexpr int MaxGuideLevel = 1;
        // 描画解像度。0=自動（単体GPUは100%、内蔵GPUは67%）、1=100%、2=75%、3=67%。
        static constexpr int MaxResolutionLevel = 3;

    private:
        // 各設定の今の段階（初期値は真ん中あたり）
        int m_BrightnessLevel = 2;
        int m_EffectLevel = 1;
        int m_LookSensitivityLevel = 2;
        int m_VolumeLevel = 3;
        // 1で目的の文章・目的地の矢印を出し、0で何も説明しない、P.T.のような遊び方にしている。
        int m_GuideLevel = 1;
        // 描画用のテクスチャは起動時に作るため、変更は次回の起動から反映される。
        int m_ResolutionLevel = 0;

        // 範囲内で、今と違う値のときだけ書き換えてtrueを返している
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
        // %LOCALAPPDATA%\SignalLost\settings.txt から読み込み・保存している。
        void Load();
        void Save() const;
        // Set系は範囲外の値や同じ値なら何もせずfalseを返す。trueのときだけ、呼び出し側で保存と反映を行っている。

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

        // 各設定の今の段階を返している
        int GetBrightnessLevel() const { return m_BrightnessLevel; }
        int GetEffectLevel() const { return m_EffectLevel; }
        int GetLookSensitivityLevel() const { return m_LookSensitivityLevel; }
        int GetVolumeLevel() const { return m_VolumeLevel; }
        int GetGuideLevel() const { return m_GuideLevel; }
        bool IsGuideEnabled() const { return m_GuideLevel > 0; }
        int GetResolutionLevel() const { return m_ResolutionLevel; }

        // 描画解像度の倍率（画面の大きさに掛ける値）を返している。自動のときは0を返し、GPUの種類に応じて決めてもらっている。
        float GetRenderScale() const
        {
            constexpr float renderScales[MaxResolutionLevel + 1] =
            {
                0.0f, 1.0f, 0.75f, 0.67f
            };
            return renderScales[m_ResolutionLevel];
        }

        // 段階の番号を、各仕組みへ渡す倍率へ変換している。変換の式はここだけに置いている。
        // 明るさは真ん中(2)を0とし、1段階ごとに露出を0.055ずらしている。
        float GetBrightnessOffset() const
        {
            return static_cast<float>(m_BrightnessLevel - 2) * 0.055f;
        }

        // 演出の強さ：画面効果の強さに掛ける倍率（低0.70・中1.0・高1.25）
        float GetEffectScale() const
        {
            constexpr float effectScales[MaxEffectLevel + 1] =
            {
                0.70f, 1.0f, 1.25f
            };
            return effectScales[m_EffectLevel];
        }

        // 視点の感度：0.60倍から0.20ずつ上げ、最大1.40倍
        float GetLookSensitivityScale() const
        {
            return 0.60f + static_cast<float>(m_LookSensitivityLevel) * 0.20f;
        }

        // 音量：全体の音量に掛ける倍率（0は無音）
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
