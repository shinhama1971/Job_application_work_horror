// ============================================================================
// ファイルの役割: 1面の西棟（浸水した機械室）の進み具合と、排水バルブの遊び、姿の見えない物音の演出を管理している。
// 主な技術: 有限状態機械（鍵を探す → 扉を開ける → バルブで水を抜く → 奥でヒューズを探す → 取った）、
//           長押しの操作、水が引いていく範囲の更新、乱数による物音の予定、立体音響
//
// ・2周目のループの後、右の倉庫に西棟の鍵が現れる。鍵を拾うと西側の壁の扉が開けられるようになる。
// ・西棟は柱の並ぶ広い部屋で、床一面が水に浸かっている。赤いランプの下にある3つの排水バルブを長押しで回すと、
//   1つ回すごとに水が奥のポンプ室の排水口へ引いていき、全部引くと、水の底に沈んでいた3本目のヒューズが見つかる。
//   バルブを回している間は手が離せず、回し終えるたびに背後で何かが水の中を歩く。
// ・影は出さず、水音・物音・暗さだけで怖がらせている。
// ・音を鳴らす処理はSceneが行い、このクラスは「いつ・どこで・どの音を鳴らすか」を決めて渡している。
// ============================================================================

#pragma once

#include "Stage1AmbientSounds.h"

#include <SimpleMath.h>
#include <array>
#include <random>
#include <string>
#include <string_view>
#include <vector>

class Door;
class DrainValve;
class Ground;
class Interactable;
class Item;
class KeyItem;
class Player;
class Wall;

class Stage1WestWing final
{
public:
    // 西棟の範囲（壁の内側）。最初は床が水に浸かり、足音が水音になる。
    static constexpr float MinX = -378.0f;
    static constexpr float MaxX = -222.0f;
    static constexpr float MinZ = -98.0f;
    static constexpr float MaxZ = 258.0f;
    // 排水バルブの数
    static constexpr int ValveCount = 3;

    // 配置（Stage1Layout）から受け取るObject。
    struct Parts
    {
        Door* door = nullptr;       // 西側の壁の扉（鍵で開く）
        KeyItem* key = nullptr;     // 右の倉庫に落ちている西棟の鍵
        Item* fuse = nullptr;       // ポンプ室の3本目のヒューズ（水が引くまで隠している）
        Ground* ground = nullptr;   // 水に浸かった範囲を持つ床
        std::array<DrainValve*, ValveCount> valves{};   // 排水バルブ
        std::array<Wall*, ValveCount> valveLamps{};     // バルブの上のランプ（回す前は赤、回したら緑）
    };

    // 進み具合。目的表示と目的地の矢印に使っている。
    enum class Step
    {
        Waiting,        // まだ鍵が出ていない（2周目のループの前）
        FindKey,        // 右の倉庫で鍵を探す
        OpenDoor,       // 鍵を拾った。西側の扉を開ける
        DrainWater,     // 扉が開いた。排水バルブを回して水を抜く
        FindFuse,       // 水が引いた。奥のポンプ室でヒューズを探す
        Done            // ヒューズを取った
    };

    // 受け取ったObjectを覚え、鍵とヒューズを隠し、扉に鍵をかけ、西棟を水で満たした状態から始めている
    void Init(const Parts& parts);
    // 2周目のループの後に呼び、右の倉庫に鍵を出している。
    void Activate();
    // 毎フレーム呼んでいる。進み具合を更新し、鳴らす物音を cues に追加している。
    // focusedは、今プレイヤーが調べようとしている対象（バルブを長押しで回しているかの判定に使っている）。
    void Update(const Player& player, float deltaTime,
        const DirectX::SimpleMath::Vector3& listenerForward,
        const Interactable* focused,
        std::vector<AmbientSoundCue>& cues);

    // 今の進み具合を返している
    Step GetStep() const { return m_Step; }
    // 回し終えたバルブの数
    int GetOpenedValveCount() const;
    // 目的地の矢印が指す場所を返している（鍵・扉・一番近いバルブ・ヒューズのどれか）。
    DirectX::SimpleMath::Vector3 GetGuideTarget() const;
    // 回している間の進み具合と、短い知らせの文章（どちらもなければ空）
    std::string_view GetObjectiveText() const;
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
    int m_WadingEventsPlayed = 0;   // 背後で水の中を歩く音を鳴らした回数（時間で鳴らす分。最大2回）
    // 一度でも入ったか（初めて入ったときの配管の音に使っている）
    bool m_EnteredOnce = false;
    // 最後に見たプレイヤーの位置（目的地の矢印で、一番近いバルブを選ぶため）
    DirectX::SimpleMath::Vector3 m_LastPlayerPosition;

    // --- 排水バルブと水位
    // 水の手前の端（z）。水は奥（MaxZ）の排水口へ向かって引いていき、MaxZに着くと水がなくなる。
    float m_WaterEdgeZ = MinZ;
    float m_GurgleTimer = 0.0f;     // 水が引いている間の、排水の音の間隔
    float m_TurnSoundTimer = 0.0f;  // バルブを回している間の、きしむ音の間隔
    float m_TurningTimer = 0.0f;    // 回しているときの文章を出す残り秒数（手を離しても少しだけ残す）
    float m_TurningProgress = 0.0f; // 最後に回していたバルブの進み具合（0〜1）
    std::string m_TurningText;      // 「バルブを回している 45%」
    std::string_view m_NoticeText;  // 短い知らせ
    float m_NoticeTimer = 0.0f;

    // minimum〜maximumの一様な乱数を返している
    float RandomRange(float minimum, float maximum);
    // 短い知らせを出している
    void ShowNotice(std::string_view text, float seconds);
    // 長押しでバルブを回し、回し終えたらランプを緑にして水の引く先を進めている
    void UpdateValves(const Player& player, float deltaTime,
        const DirectX::SimpleMath::Vector3& listenerForward,
        const Interactable* focused, std::vector<AmbientSoundCue>& cues);
    // 水の端を目標まで動かし、床の水に浸かった範囲を更新している。全部引いたらtrueを返している
    bool UpdateWaterLevel(float deltaTime, std::vector<AmbientSoundCue>& cues);
    // 床の水に浸かった範囲を、今の水の端に合わせている
    void ApplyFloodRegion();
    // 背後で水の中を歩く足音を予定している
    void ScheduleWading(const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    // 入口の扉を勝手に閉めている
    void SlamEntranceDoor();
};
