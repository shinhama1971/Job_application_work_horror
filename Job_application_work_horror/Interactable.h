// ============================================================================
// ファイルの役割: プレイヤーが調べられるObjectに共通のインターフェース。
// 主な技術: 抽象インターフェース、仮想関数、ポリモーフィズム（InteractionSystemが型を問わず扱える）
// ============================================================================

#pragma once

#include <SimpleMath.h>

class Player;

// 扉・アイテム・配電盤などがこれを継承し、InteractionSystemが視線の先の1つを選んで調べている。
class Interactable
{
public:
    virtual ~Interactable() = default;

    // 今調べられる状態か（拾った後・開いた後などはfalse）
    virtual bool IsInteractionEnabled() const = 0;
    // 視線と距離の判定に使う位置
    virtual DirectX::SimpleMath::Vector3 GetInteractionPosition() const = 0;
    // 画面に出す操作の説明（例：「ドアを開ける」）
    virtual const char* GetInteractionPrompt() const = 0;
    // 調べたときの処理
    virtual void Interact(Player& player) = 0;
};
