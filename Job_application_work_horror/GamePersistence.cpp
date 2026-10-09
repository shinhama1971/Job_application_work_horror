// ============================================================================
// ファイルの役割: 音量の反映と、ベスト記録（クリアタイム・捕まった回数）の保存・読み込みを管理している。
// 主な技術: ファイルの入出力、読んだ値の検証、読めないときは既定のまま続ける
// ============================================================================

#include "Game.h"
#include "CaptureMode.h"

#include <filesystem>
#include <fstream>

#include "utility.h"

namespace Core
{
    // 設定の音量に、ポーズ中は42%、自動撮影中は0をかけて全体の音量にしている
    void Game::ApplyAudioVolume(bool paused)
    {
        if (!m_SoundReady)
        {
            return;
        }

        const float pauseScale = paused ? 0.42f : 1.0f;
        // 自動撮影モードは裏で動かすため、音を出していない。
        const float captureScale = Tools::CaptureMode::IsActive() ? 0.0f : 1.0f;
        m_Sound.SetMasterVolume(
            m_Settings.GetVolumeScale() * pauseScale * captureScale);
    }

    // ベスト記録を読み込んでいる。ファイルが無い・壊れている・ありえない値（0秒以下や24時間超など）のときは読まずに続けている
    void Game::LoadBestRecord()
    {
        std::ifstream recordFile(
            utility::ResolveSaveFileForRead("best_record.txt"));
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

        m_State.LoadBestRecord(clearTime, caughtCount);
    }

    // ベスト記録を %LOCALAPPDATA%\SignalLost\best_record.txt に書き出している（フォルダが無ければ作っている）
    void Game::SaveBestRecord() const
    {
        std::error_code directoryError;
        const std::filesystem::path saveDirectory = utility::GetSaveDirectory();
        std::filesystem::create_directories(saveDirectory, directoryError);
        if (directoryError)
        {
            return;
        }

        std::ofstream recordFile(
            saveDirectory / "best_record.txt", std::ios::trunc);
        if (!recordFile)
        {
            return;
        }
        recordFile << m_State.GetBestClearTimeSeconds() << ' '
            << m_State.GetBestCaughtCount() << '\n';
    }

}
