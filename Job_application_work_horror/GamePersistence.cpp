// ============================================================================
// ファイルの役割: 音量の反映と、ベスト記録（クリアタイム・捕まった回数）の保存・読み込みを管理している。
// 主な技術: ファイルの入出力、読んだ値の検証、読めないときは既定のまま続ける
// ============================================================================

#include "Game.h"
#include "CaptureMode.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "utility.h"

namespace Core
{
    namespace
    {
        constexpr char BestRecordFileName[] = "best_record.txt";
        constexpr float PausedVolumeScale = 0.42f;
        constexpr float MaxRecordTimeSeconds = 24.0f * 60.0f * 60.0f;
        constexpr int MaxRecordedCaughtCount = 999;

        bool IsValidBestRecord(float clearTime, int caughtCount)
        {
            return std::isfinite(clearTime) && clearTime > 0.0f && clearTime <= MaxRecordTimeSeconds &&
                caughtCount >= 0 && caughtCount <= MaxRecordedCaughtCount;
        }
    }

    // 設定の音量に、ポーズ中は42%、自動撮影中は0をかけて全体の音量にしている
    void Game::ApplyAudioVolume(bool paused)
    {
        if (!m_SoundReady)
        {
            return;
        }

        const float pauseScale = paused ? PausedVolumeScale : 1.0f;
        // 自動撮影モードは裏で動かすため、音を出していない。
        const float captureScale = Tools::CaptureMode::IsActive() ? 0.0f : 1.0f;
        m_Sound.SetMasterVolume(
            m_Settings.GetVolumeScale() * pauseScale * captureScale);
    }

    // ベスト記録を読み込んでいる。ファイルが無い・壊れている・ありえない値（0秒以下や24時間超など）のときは読まずに続けている
    void Game::LoadBestRecord()
    {
        std::ifstream recordFile(
            utility::ResolveSaveFileForRead(BestRecordFileName));
        float clearTime = 0.0f;
        int caughtCount = 0;
        if (!(recordFile >> clearTime >> caughtCount) || !IsValidBestRecord(clearTime, caughtCount))
        {
            return;
        }

        m_State.LoadBestRecord(clearTime, caughtCount);
    }

    // ベスト記録を %LOCALAPPDATA%\SignalLost\best_record.txt に書き出している（フォルダが無ければ作っている）
    bool Game::SaveBestRecord() const
    {
        const float clearTime = m_State.GetBestClearTimeSeconds();
        const int caughtCount = m_State.GetBestCaughtCount();
        if (!m_State.HasClearRecord() || !IsValidBestRecord(clearTime, caughtCount))
        {
            return true;
        }

        // 読み直したときに、同じ float に戻る桁数で保存する。
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<float>::max_digits10)
            << clearTime << ' ' << caughtCount << "\r\n";
        return utility::WriteSaveFile(BestRecordFileName, text.str());
    }

}
