// ============================================================================
// ファイルの役割: 音量設定とクリア記録の保存・読み込みを管理します。
// 主な技術: ファイルI/O、値の検証、失敗時の既定値復旧
// ============================================================================

#include "Game.h"
#include "CaptureMode.h"

#include <filesystem>
#include <fstream>

#include "utility.h"

namespace Core
{
    void Game::ApplyAudioVolume(bool paused)
    {
        if (!m_SoundReady)
        {
            return;
        }

        const float pauseScale = paused ? 0.42f : 1.0f;
        // 自動撮影モードは裏で動かすため、音を出しません。
        const float captureScale = Tools::CaptureMode::IsActive() ? 0.0f : 1.0f;
        m_Sound.SetMasterVolume(
            m_Settings.GetVolumeScale() * pauseScale * captureScale);
    }

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
