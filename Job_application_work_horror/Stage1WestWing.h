// ============================================================================
// ファイルの役割: 1面の西棟（浸水した機械室）の進み具合と、姿の見えない物音の演出を管理している。
// 主な技術: 有限状態機械（鍵を探す → 扉を開ける → 奥でヒューズを探す → 取った）、乱数による物音の予定、立体音響
//
// ・2周目のループの後、右の倉庫に西棟の鍵が現れる。鍵を拾うと西側の壁の扉が開けられるようになり、
//   一番奥のポンプ室に3本目のヒューズがある（西棟に入らないと先へ進めない）。
// ・影は出さず、水音・物音・暗さだけで怖がらせている。
//   水の滴る音、背後で何かが水の中を歩く音、ヒューズを取った瞬間に入口の扉が閉まる音を鳴らしている。
// ・音を鳴らす処理はSceneが行い、このクラスは「いつ・どこで・どの音を鳴らすか」を決めて渡している。
// ============================================================================

#pragma once

#include "Stage1AmbientSounds.h"

#include <SimpleMath.h>
#include <random>
#include <string_view>
#include <vector>

class Door;
class Item;
class KeyItem;
class Player;

class Stage1WestWing final
{
public:
    // 西棟の範囲（壁の内側）。床が水に浸かり、足音が水音になる。
    static constexpr float MinX = -378.0f;
    static constexpr float MaxX = -222.0f;
    static constexpr float MinZ = -98.0f;
    static constexpr float MaxZ = 258.0f;

    // 配置（Stage1Layout）から受け取るObject。
    struct Parts
    {
        Door* door = nullptr;       // 西側の壁の扉（鍵で開く）
        KeyItem* key = nullptr;     // 右の倉庫に落ちている西棟の鍵
        Item* fuse = nullptr;       // ポンプ室の3本目のヒューズ
    };

    // 進み具合。目的表示と目的地の矢印に使っている。
    enum class Step
    {
        Waiting,        // まだ鍵が出ていない（2周目のループの前）
        FindKey,        // 右の倉庫で鍵を探す
        OpenDoor,       // 鍵を拾った。西側の扉を開ける
        FindFuse,       // 扉が開いた。奥のポンプ室でヒューズを探す
        Done            // ヒューズを取った
    };

    // 受け取ったObjectを覚え、鍵を隠し、扉に鍵をかけた状態から始めている
    void Init(const Parts& parts);
    // 2周目のループの後に呼び、右の倉庫に鍵を出している。
    void Activate();
    // 毎フレーム呼んでいる。進み具合を更新し、鳴らす物音を cues に追加している。
    void Update(const Player& player, float deltaTime,
        const DirectX::SimpleMath::Vector3& listenerForward,
        std::vector<AmbientSoundCue>& cues);

    // 今の進み具合を返している
    Step GetStep() const { return m_Step; }
    // 目的地の矢印が指す場所を返している（鍵・扉・ヒューズのどれか）。
    DirectX::SimpleMath::Vector3 GetGuideTarget() const;
    // その位置が西棟の中かを返している
    static bool IsInside(const DirectX::SimpleMath::Vector3& position);

private:
    // 時間をずらして鳴らす1音（水の中を歩く足音を、少しずつ間を空けて鳴らすため）。
    struct ScheduledCue
    {
        float Delay = 0.0f;
        AmbientSoundCue Cue;
    };

    // 受け取ったObject、進み具合、乱数、予定している音、次に水が滴るまでの秒数
    Parts m_Parts;
    Step m_Step = Step::Waiting;
    std::mt19937 m_Random{ std::random_device{}() };
    std::vector<ScheduledCue> m_Scheduled;
    float m_DripTimer = 0.0f;
    float m_InsideSeconds = 0.0f;   // 西棟に入ってからの合計秒数
    int m_WadingEventsPlayed = 0;   // 背後で水の中を歩く音を鳴らした回数（最大2回）
    // 一度でも入ったか（初めて入ったときの配管の音に使っている）
    bool m_EnteredOnce = false;

    // minimum〜maximumの一様な乱数を返している
    float RandomRange(float minimum, float maximum);
    // 背後で水の中を歩く足音を予定している
    void ScheduleWading(const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    // 入口の扉を勝手に閉めている
    void SlamEntranceDoor();
};
