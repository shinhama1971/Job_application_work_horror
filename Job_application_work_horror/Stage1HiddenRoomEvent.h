// ============================================================================
// ファイルの役割: 1面の隠し部屋（暗証番号の扉の先）の閉じ込めイベントを管理します。
// 主な技術: 有限状態機械（待機 → 閉じ込め → 脱出）、ランダムな鍵の位置、ライトで追い払う追跡者
//
// ・部屋の奥まで入ると、背後で扉が閉まって鍵がかかります。
// ・暗い部屋のどこかに鍵が落ちています（位置は毎回ランダム）。拾うと扉が開きます。
// ・しばらく見つけられないと影が現れて近づいてきます。ライトを当て続けると消えますが、
//   触れられると懐中電灯の電池を奪われます。
// ・部屋の記録端末を読むと、施設で起きたことが少し分かります（任意）。
// ============================================================================

#pragma once

#include <array>
#include <random>
#include <string_view>

class Door;
class FuseBox;
class KeyItem;
class Player;
class ShadowMan;

class Stage1HiddenRoomEvent final
{
public:
    // 配置（Stage1Layout）から受け取るObjectです。扉は暗証番号の扉と同じものです。
    struct Parts
    {
        Door* door = nullptr;
        KeyItem* key = nullptr;
        FuseBox* record = nullptr;
        ShadowMan* shadow = nullptr;
    };

    void Init(const Parts& parts);

    // keypadSolved: 暗証番号の扉が開けられたか（開く前は何も起きません）。
    void Update(Player& player, float deltaTime, bool keypadSolved);

    bool IsTrapped() const { return m_State == State::Trapped; }

    // 目的表示に出す文章です。何も出さないときは空です。
    std::string_view GetObjectiveText() const;

private:
    enum class State
    {
        Waiting,    // 扉が開くのを待っている
        Trapped,    // 閉じ込められて鍵を探している
        Escaped     // 鍵で脱出した（以後は何も起きません）
    };

    // 閉じ込めてから最初に影が出るまでの秒数と、追い払った・触れられた後に次が出るまでの秒数です。
    static constexpr float FirstShadowDelay = 22.0f;
    static constexpr float NextShadowDelay = 14.0f;
    static constexpr float ShadowSpeed = 15.0f;         // 歩く速さ（30）の半分
    static constexpr float ShadowCatchDistance = 20.0f;
    static constexpr float BatteryPenalty = 25.0f;

    void Trap(Player& player);
    void UpdateTrapped(Player& player, float deltaTime);
    void SpawnShadow(const Player& player);
    void ShowNotice(std::string_view text, float seconds);

    Parts m_Parts;
    State m_State = State::Waiting;
    float m_ShadowTimer = 0.0f;
    bool m_ShadowWasActive = false;
    bool m_RecordRead = false;
    std::string_view m_NoticeText;
    float m_NoticeTimer = 0.0f;
    std::mt19937 m_Random{ std::random_device{}() };
};
