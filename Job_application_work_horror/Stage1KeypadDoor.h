// ============================================================================
// ファイルの役割: 1面の暗証番号の扉（入力画面の操作、正解・不正解の反応、手がかりの数字の配置）を管理している。
// 主な技術: 入力画面を開いている間だけ操作を受け取る仕組み（その間プレイヤーは止める）、状態だけを持つクラス（KeypadLock）への委任、
//           プレイごとにランダムな番号と、懐中電灯で浮かぶ手がかり（FlashlightWriting）の連動
//
// 4桁の番号は施設の壁に「ひとつめ ３」のような文字で書かれていて、懐中電灯で照らすと読める。
// 番号はプレイごとに変わるため、攻略情報を覚えるのではなく、その場で探して解く謎にしている。
// 扉の先は任意で探索する部屋で、クリアには必須ではない。
// ============================================================================

#pragma once

#include "KeypadLock.h"

#include <array>
#include <random>

class Door;
class FuseBox;
class FlashlightWriting;
class Hud;
class Player;

class Stage1KeypadDoor final
{
public:
    // 配置（Stage1Layout）から受け取る、扉・入力盤・手がかりの数字のObject。
    struct Parts
    {
        // 入力盤（FuseBoxをスイッチとして使っている）、扉、各桁の数字を書いた壁の文字
        FuseBox* panel = nullptr;
        Door* door = nullptr;
        std::array<FlashlightWriting*, KeypadLock::DigitCount> digitWritings{};
    };

    // 番号を決め、手がかりの数字の画像をその番号に差し替えている。
    void Init(const Parts& parts);

    // 入力画面を開いている間は、キー入力をここで受け取り、プレイヤーの操作を止めている。
    // プレイヤーが操作できない間も呼ぶ必要がある（入力画面を閉じるため）。
    void Update(Player& player, float deltaTime);

    // 入力画面を開いているか、解けたか
    bool IsInputOpen() const { return m_InputOpen; }
    bool IsSolved() const { return m_Lock.IsSolved(); }

    // 入力画面を開いていれば、HUDで入力盤を描いている
    void Draw(Hud& hud) const;

private:
    // 開いた直後の決定キー（入力盤を調べたE）を、入力として受け取らないための待ち時間。
    static constexpr float InputDelay = 0.2f;
    // 不正解のとき、枠を赤く点滅させる秒数
    static constexpr float WrongFeedbackSeconds = 0.7f;

    // 入力画面を閉じている（解けたなら扉を開けている）
    void Close(Player& player, bool solved);
    // 桁の移動・数字の増減・決定・戻るの入力を処理している
    void HandleInput(Player& player);

    // 受け取ったObject、錠の状態、入力画面を開いているか、入力を受け付けるまでの待ち、不正解の表示の残り、乱数
    Parts m_Parts;
    KeypadLock m_Lock;
    bool m_InputOpen = false;
    float m_InputDelayTimer = 0.0f;
    float m_WrongTimer = 0.0f;
    std::mt19937 m_Random{ std::random_device{}() };
};
