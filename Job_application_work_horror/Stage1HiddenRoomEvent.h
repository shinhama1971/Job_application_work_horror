// ============================================================================
// ファイルの役割: 1面の隠し部屋（暗証番号の扉の先）に閉じ込められるイベントを管理している。
// 主な技術: 有限状態機械（待機 → 閉じ込め → 脱出）、ランダムな鍵の位置、ライトで追い払う追跡者
//
// ・部屋の奥まで入ると、背後で扉が閉まって鍵がかかる。
// ・部屋は書類棚で区切ったジグザグの通路になっていて、鍵は3か所の行き止まりのどれか（毎回ランダム）の棚の陰に落ちている。
//   入口からは見えないので、通路を奥へ進んで探す必要がある。拾うと扉が開く。
// ・しばらく見つけられないと影が現れ、格子の経路探索（RoomPathGrid）で棚を回り込んで近づいてくる。
//   ライトを当て続けると消えるが、触れられると懐中電灯の電池を奪われる。
// ・部屋の記録端末を読むと、施設で起きたことが少し分かり、鍵のある行き止まりの保管灯が点滅する（任意）。
// ============================================================================

#pragma once

#include "RoomPathGrid.h"

#include <array>
#include <random>
#include <string_view>

class Door;
class FuseBox;
class KeyItem;
class Player;
class ShadowMan;
class Wall;

class Stage1HiddenRoomEvent final
{
public:
    static constexpr std::size_t KeySpotCount = 3;

    // 配置（Stage1Layout）から受け取るObject。扉は暗証番号の扉と同じもの。
    struct Parts
    {
        // 扉、鍵、記録端末（FuseBoxをスイッチとして使っている）、追ってくる影
        Door* door = nullptr;
        KeyItem* key = nullptr;
        FuseBox* record = nullptr;
        ShadowMan* shadow = nullptr;
        std::array<Wall*, KeySpotCount> keyLamps{};
    };

    // 受け取ったObjectを覚え、鍵を隠して待機の状態から始めている
    void Init(const Parts& parts);

    // keypadSolved: 暗証番号の扉が開けられたか（開く前は何も起きない）。
    // resolveCollision: 影を棚・壁にめり込ませない押し戻し（通れるマスの表を作るのにも使う）
    void Update(Player& player, float deltaTime, bool keypadSolved,
        const RoomPathGrid::ResolveCollision& resolveCollision);

    // 閉じ込められている最中か
    bool IsTrapped() const { return m_State == State::Trapped; }

    // 目的表示に出す文章を返している。何も出さないときは空。
    std::string_view GetObjectiveText() const;

private:
    enum class State
    {
        Waiting,    // 扉が開くのを待っている
        Trapped,    // 閉じ込められて鍵を探している
        Escaped     // 鍵で脱出した（その後は何も起きない）
    };

    // 閉じ込めてから最初に影が出るまでの秒数と、追い払った・触れられた後に次が出るまでの秒数。
    static constexpr float FirstShadowDelay = 22.0f;
    static constexpr float NextShadowDelay = 14.0f;
    // 歩く速さ（30）より遅いが、棚を回り込んで来るので、通路で鉢合わせると逃げ場が少ない
    static constexpr float ShadowSpeed = 18.0f;
    // この距離まで近づかれたら触れられたとしている、触れられたときに奪われる電池の量（%）
    static constexpr float ShadowCatchDistance = 20.0f;
    static constexpr float BatteryPenalty = 25.0f;
    // 影の体の半径（棚との押し戻しと、通れるマスの判定に使う）と、足音1歩の長さ
    static constexpr float ShadowBodyRadius = 5.0f;
    static constexpr float ShadowStrideLength = 22.0f;

    // 閉じ込めている／閉じ込められている間の処理／影を出している／影を棚を回り込ませて近づけている／知らせの文章を出している
    void Trap(Player& player);
    void UpdateTrapped(Player& player, float deltaTime, const RoomPathGrid::ResolveCollision& resolveCollision);
    void SpawnShadow(const Player& player);
    void MoveShadow(const Player& player, float deltaTime, const RoomPathGrid::ResolveCollision& resolveCollision);
    void UpdateKeyLamps(float deltaTime);
    void ShowNotice(std::string_view text, float seconds);

    // 受け取ったObject、今の状態、次の影までの秒数、前フレームで影が出ていたか、記録を読んだか
    Parts m_Parts;
    State m_State = State::Waiting;
    float m_ShadowTimer = 0.0f;
    bool m_ShadowWasActive = false;
    bool m_RecordRead = false;
    std::size_t m_KeySpot = 0;
    float m_KeyLampTime = 0.0f;
    // 部屋の経路探索の格子（壁の内側、4単位のマス、体の半径、影の足元の高さ）と、影の足音までの距離
    RoomPathGrid m_Path{ 47.0f, 218.0f, 42.0f, 178.0f, 4.0f, ShadowBodyRadius, -99.0f };
    float m_ShadowStride = 0.0f;
    // 知らせの文章とその残り秒数、乱数
    std::string_view m_NoticeText;
    float m_NoticeTimer = 0.0f;
    std::mt19937 m_Random{ std::random_device{}() };
};
