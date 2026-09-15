// ============================================================================
// ファイルの役割: 音量設定とクリア記録の保存・読み込みを管理します。
// 主な技術: ファイルI/O、値の検証、失敗時の既定値復旧
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "Game.h"

#include <filesystem>
#include <fstream>

namespace Core
{
    // 処理内容: 計算済みの設定や効果を対象へ反映します。
    void Game::ApplyAudioVolume(bool paused)
    {
        if (!m_SoundReady)
        {
            return;
        }

        constexpr float volumeScales[] =
        {
            0.0f, 0.28f, 0.52f, 0.76f, 1.0f
        };
        const float pauseScale = paused ? 0.42f : 1.0f;
        m_Sound.SetMasterVolume(
            volumeScales[m_Settings.GetVolumeLevel()] * pauseScale);
    }

    // 処理内容: ファイルからデータを読み込み、利用可能な形へ変換します。
    void Game::LoadBestRecord()
    {
        std::ifstream recordFile("save/best_record.txt");
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

    // 処理内容: 現在の状態を次回復元できる形式で保存します。
    void Game::SaveBestRecord() const
    {
        std::error_code directoryError;
        std::filesystem::create_directories("save", directoryError);
        if (directoryError)
        {
            return;
        }

        std::ofstream recordFile(
            "save/best_record.txt", std::ios::trunc);
        if (!recordFile)
        {
            return;
        }
        recordFile << m_State.GetBestClearTimeSeconds() << ' '
            << m_State.GetBestCaughtCount() << '\n';
    }

}
