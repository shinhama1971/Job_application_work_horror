// ============================================================================
// ファイルの役割: ポーズ画面の開け閉め、項目の選択、設定値の変更、メニュー操作の入力を扱っている。
// 主な技術: 押した瞬間の判定、設定値を段階で変える操作、実行する命令を戻り値で返して役割を分ける設計
// ============================================================================

#include "PauseMenu.h"

#include "GameSettings.h"
#include "input.h"

#include <algorithm>

namespace Core
{
    // 開いたときは、いつも一番上の項目（明るさ）を選んだ状態にしている
    void PauseMenu::Open()
    {
        m_IsOpen = true;
        m_SelectedItem = Item::Brightness;
    }

    // 上下で項目を選び、左右で設定を変え、変わったら小さく振動させている。最後に命令のボタンを読んでいる
    PauseMenu::Result PauseMenu::Update(GameSettings& settings)
    {
        UpdateSelection();

        Result result;
        int settingDelta = 0;
        if (Input::GetKeyTrigger(VK_LEFT) ||
            Input::GetButtonTrigger(XINPUT_LEFT))
        {
            settingDelta = -1;
        }
        else if (Input::GetKeyTrigger(VK_RIGHT) ||
            Input::GetButtonTrigger(XINPUT_RIGHT))
        {
            settingDelta = 1;
        }

        if (settingDelta != 0 && ChangeSelectedSetting(settings, settingDelta))
        {
            result.settingsChanged = true;
            result.changedItem = m_SelectedItem;
            Input::SetVibration(2, 0.045f);
        }

        result.command = ReadCommand();
        return result;
    }

    // 上下キー（十字キー）で選ぶ項目を動かしている（端で止まる）
    void PauseMenu::UpdateSelection()
    {
        int selectionDelta = 0;
        if (Input::GetKeyTrigger(VK_UP) ||
            Input::GetButtonTrigger(XINPUT_UP))
        {
            selectionDelta = -1;
        }
        else if (Input::GetKeyTrigger(VK_DOWN) ||
            Input::GetButtonTrigger(XINPUT_DOWN))
        {
            selectionDelta = 1;
        }
        if (selectionDelta == 0)
        {
            return;
        }

        constexpr int lastIndex = static_cast<int>(Item::Count) - 1;
        const int nextIndex = (std::clamp)(
            GetSelectedIndex() + selectionDelta, 0, lastIndex);
        m_SelectedItem = static_cast<Item>(nextIndex);
        Input::SetVibration(1, 0.03f);
    }

    bool PauseMenu::ChangeSelectedSetting(
        GameSettings& settings, int delta) const
    {
        // 範囲外はSet側ではじかれ、値が変わらない場合もfalseになる。
        switch (m_SelectedItem)
        {
        case Item::Brightness:
            return settings.SetBrightnessLevel(
                settings.GetBrightnessLevel() + delta);
        case Item::Effect:
            return settings.SetEffectLevel(
                settings.GetEffectLevel() + delta);
        case Item::LookSensitivity:
            return settings.SetLookSensitivityLevel(
                settings.GetLookSensitivityLevel() + delta);
        case Item::Volume:
            return settings.SetVolumeLevel(
                settings.GetVolumeLevel() + delta);
        case Item::Guide:
            return settings.SetGuideLevel(
                settings.GetGuideLevel() + delta);
        case Item::Resolution:
            return settings.SetResolutionLevel(
                settings.GetResolutionLevel() + delta);
        default:
            return false;
        }
    }

    // R（Y）でこの階をやり直す、T（B）でタイトルへ戻る、Q（BACK）でゲームを終える
    PauseMenu::Command PauseMenu::ReadCommand()
    {
        if (Input::GetKeyTrigger(VK_R) ||
            Input::GetButtonTrigger(XINPUT_Y))
        {
            return Command::RestartStage;
        }
        if (Input::GetKeyTrigger(VK_T) ||
            Input::GetButtonTrigger(XINPUT_B))
        {
            return Command::ReturnToTitle;
        }
        if (Input::GetKeyTrigger(VK_Q) ||
            Input::GetButtonTrigger(XINPUT_BACK))
        {
            return Command::Quit;
        }
        return Command::None;
    }
}
