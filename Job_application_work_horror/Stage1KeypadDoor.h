// ============================================================================
// ファイルの役割: 1面の暗証番号の扉（入力画面の操作、正解・不正解の反応、手がかりの数字の配置）を管理します。
// 主な技術: モーダル入力（操作中はプレイヤーを止める）、純粋な状態クラス（KeypadLock）への委譲、
//           プレイごとにランダムな番号と、懐中電灯で浮かぶ手がかり（FlashlightWriting）の連動
//
// 4桁の番号は施設の壁に「ひとつめ ３」のような文字で書かれていて、懐中電灯で照らすと読めます。
// 番号はプレイごとに変わるため、攻略情報を覚えるのではなく、その場で探して解く謎になります。
// 扉の先は任意探索の部屋で、クリアには必須ではありません。
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
    // 配置（Stage1Layout）から受け取る、扉・入力盤・手がかりの数字のObjectです。
    struct Parts
    {
        FuseBox* panel = nullptr;
        Door* door = nullptr;
        std::array<FlashlightWriting*, KeypadLock::DigitCount> digitWritings{};
    };

    // 番号を決め、手がかりの数字の画像をその番号に差し替えます。
    void Init(const Parts& parts);

    // 入力画面を開いている間は、キー入力をここで受け取り、プレイヤーの操作を止めます。
    // プレイヤーが操作できない間も呼ぶ必要があります（入力画面を閉じるため）。
    void Update(Player& player, float deltaTime);

    bool IsInputOpen() const { return m_InputOpen; }
    bool IsSolved() const { return m_Lock.IsSolved(); }

    void Draw(Hud& hud) const;

private:
    // 開いた直後の決定キー（入力盤を調べたE）を、入力として受け取らないための待ち時間です。
    static constexpr float InputDelay = 0.2f;
    static constexpr float WrongFeedbackSeconds = 0.7f;

    void Close(Player& player, bool solved);
    void HandleInput(Player& player);

    Parts m_Parts;
    KeypadLock m_Lock;
    bool m_InputOpen = false;
    float m_InputDelayTimer = 0.0f;
    float m_WrongTimer = 0.0f;
    std::mt19937 m_Random{ std::random_device{}() };
};
