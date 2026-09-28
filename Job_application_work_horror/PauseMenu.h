// ============================================================================
// ファイルの役割: ポーズ画面の開閉、項目選択、設定値の変更、メニュー操作の入力を扱います。
// 主な技術: 入力のエッジ検出、設定値の段階変更、コマンドの返却による責務分離
// 設定値を各システムへ反映する処理とシーン遷移はGameが行い、このクラスは持ちません。
// ============================================================================

#pragma once

namespace Core
{
    class GameSettings;

    class PauseMenu final
    {
    public:
        // 画面に並ぶ設定項目。HUDはGetSelectedIndex()の値でハイライト位置を決めます。
        enum class Item
        {
            Brightness,
            Effect,
            LookSensitivity,
            Volume,
            Guide,       // 目的表示・目的地ガイドの表示あり/なし
            Count
        };

        // Gameが実行するメニュー操作です。
        enum class Command
        {
            None,
            RestartStage,
            ReturnToTitle,
            Quit
        };

        struct Result
        {
            Command command = Command::None;
            bool settingsChanged = false;
            Item changedItem = Item::Count;
        };

        void Open();
        void Close() { m_IsOpen = false; }
        bool IsOpen() const { return m_IsOpen; }
        int GetSelectedIndex() const { return static_cast<int>(m_SelectedItem); }

        // ポーズ中に毎フレーム呼び、入力に応じて設定値を変更します。
        Result Update(GameSettings& settings);

    private:
        bool m_IsOpen = false;
        Item m_SelectedItem = Item::Brightness;

        void UpdateSelection();
        bool ChangeSelectedSetting(GameSettings& settings, int delta) const;
        static Command ReadCommand();
    };
}
