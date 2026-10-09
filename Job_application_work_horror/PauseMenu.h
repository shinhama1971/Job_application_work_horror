// ============================================================================
// ファイルの役割: ポーズ画面の開け閉め、項目の選択、設定値の変更、メニュー操作の入力を扱っている。
// 主な技術: 押した瞬間の判定、設定値を段階で変える操作、実行する命令を戻り値で返して役割を分ける設計
// 設定値を各仕組みへ反映する処理とシーンの切り替えはGameが行い、このクラスは持っていない。
// ============================================================================

#pragma once

namespace Core
{
    class GameSettings;

    class PauseMenu final
    {
    public:
        // 画面に並ぶ設定の項目。HUDはGetSelectedIndex()の値で、強調して表示する位置を決めている。
        enum class Item
        {
            // 明るさ・画面効果・視点の速さ・音量
            Brightness,
            Effect,
            LookSensitivity,
            Volume,
            Guide,       // 目的表示・目的地の矢印の表示あり/なし
            Resolution,  // 描画解像度（次回の起動から反映）
            // 項目の数
            Count
        };

        // Gameが実行するメニュー操作（なし・この階をやり直す・タイトルへ戻る・ゲームを終える）。
        enum class Command
        {
            None,
            RestartStage,
            ReturnToTitle,
            Quit
        };

        // 1フレーム分の結果（実行する命令、設定が変わったか、どの項目が変わったか）
        struct Result
        {
            Command command = Command::None;
            bool settingsChanged = false;
            Item changedItem = Item::Count;
        };

        // 開いている・閉じている・開いているか・選んでいる項目の番号
        void Open();
        void Close() { m_IsOpen = false; }
        bool IsOpen() const { return m_IsOpen; }
        int GetSelectedIndex() const { return static_cast<int>(m_SelectedItem); }

        // ポーズ中に毎フレーム呼び、入力に応じて設定値を変えている。
        Result Update(GameSettings& settings);

    private:
        // 開いているか、選んでいる項目
        bool m_IsOpen = false;
        Item m_SelectedItem = Item::Brightness;

        // 上下で選ぶ項目を動かしている
        void UpdateSelection();
        // 選んでいる項目の設定を、左右で1段階変えている
        bool ChangeSelectedSetting(GameSettings& settings, int delta) const;
        // やり直し・タイトルへ・終了のボタンを読んでいる
        static Command ReadCommand();
    };
}
