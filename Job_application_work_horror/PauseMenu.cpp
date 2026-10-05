// ============================================================================
// ファイルの役割: ポーズ画面の開閉、項目選択、設定値の変更、メニュー操作の入力を扱います。
// 主な技術: 入力のエッジ検出、設定値の段階変更、コマンドの返却による責務分離
// ============================================================================

#include "PauseMenu.h"

#include "GameSettings.h"
#include "input.h"

#include <algorithm>

namespace Core
{
    void PauseMenu::Open()
    {
        m_IsOpen = true;
        m_SelectedItem = Item::Brightness;
    }

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
        // 範囲外はSet側で弾かれ、値が変わらない場合もfalseになります。
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
