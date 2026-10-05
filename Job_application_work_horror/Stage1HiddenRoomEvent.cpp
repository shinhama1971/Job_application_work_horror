// ============================================================================
// ファイルの役割: 1面の隠し部屋（暗証番号の扉の先）の閉じ込めイベントを管理します。
// 主な技術: 有限状態機械（待機 → 閉じ込め → 脱出）、ランダムな鍵の位置、ライトで追い払う追跡者
// ============================================================================

#include "Stage1HiddenRoomEvent.h"

#include "Door.h"
#include "FuseBox.h"
#include "Game.h"
#include "Input.h"
#include "KeyItem.h"
#include "Player.h"
#include "ShadowMan.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    // 隠し部屋の広さ（壁の内側）。扉はx=45の壁にあり、部屋は東側に広がります。
    constexpr float RoomMaxX = 218.0f;
    constexpr float RoomMinZ = 42.0f;
    constexpr float RoomMaxZ = 178.0f;
    // この線より奥まで入ったら扉を閉めます（扉のすぐ前で閉めて挟まないため）。
    constexpr float TrapLineX = 64.0f;

    // 鍵が落ちている場所の候補（暗い部屋の隅）。電池・壁の文字・配電箱と重ならない位置です。
    const std::array<Vector3, 3> KeySpots =
    {
        Vector3(205.0f, -95.0f, 55.0f),
        Vector3(70.0f, -95.0f, 168.0f),
        Vector3(172.0f, -95.0f, 164.0f)
    };

    // 影が現れる場所の候補（部屋の四隅）。プレイヤーから一番遠い隅を選びます。
    const std::array<Vector3, 4> ShadowCorners =
    {
        Vector3(208.0f, -99.0f, 52.0f),
        Vector3(208.0f, -99.0f, 168.0f),
        Vector3(60.0f, -99.0f, 52.0f),
        Vector3(60.0f, -99.0f, 168.0f)
    };

    bool IsInsideRoom(const Vector3& position)
    {
        return position.x > TrapLineX && position.x < RoomMaxX &&
            position.z > RoomMinZ && position.z < RoomMaxZ;
    }
}

void Stage1HiddenRoomEvent::Init(const Parts& parts)
{
    m_Parts = parts;
    m_State = State::Waiting;
    m_ShadowTimer = 0.0f;
    m_ShadowWasActive = false;
    m_RecordRead = false;
    m_NoticeText = {};
    m_NoticeTimer = 0.0f;
    if (m_Parts.key != nullptr)
    {
        m_Parts.key->SetActive(false);
    }
}

void Stage1HiddenRoomEvent::Update(Player& player, float deltaTime, bool keypadSolved)
{
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    if (m_Parts.door == nullptr || m_Parts.key == nullptr || m_Parts.shadow == nullptr)
    {
        return;
    }

    switch (m_State)
    {
    case State::Waiting:
        if (keypadSolved && m_Parts.door->IsOpen() && IsInsideRoom(player.GetPosition()))
        {
            Trap(player);
        }
        break;
    case State::Trapped:
        UpdateTrapped(player, deltaTime);
        break;
    case State::Escaped:
        break;
    }
}

// 部屋の奥へ入った瞬間、背後で扉が勢いよく閉まり、鍵がかかります。
void Stage1HiddenRoomEvent::Trap(Player& player)
{
    (void)player;
    m_State = State::Trapped;
    m_ShadowTimer = FirstShadowDelay;
    m_ShadowWasActive = false;

    Door* door = m_Parts.door;
    door->ResetClosed(0);
    door->SetLocked(true);

    std::uniform_int_distribution<std::size_t> pick(0, KeySpots.size() - 1);
    const Vector3& spot = KeySpots[pick(m_Random)];
    m_Parts.key->SetPosition(spot.x, spot.y, spot.z);
    m_Parts.key->SetActive(true);

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_DOOR, door->GetPosition(), 0.62f, 1.8f);
    game->PlayAudioCueAt(SOUND_CUE_SCARE, door->GetPosition(), 0.85f, 0.7f);
    game->GetPostProcess()->TriggerHorrorPulse(0.46f, 0.40f);
    Input::SetVibration(10, 0.26f);
    ShowNotice("扉が閉まった", 2.4f);
}

void Stage1HiddenRoomEvent::UpdateTrapped(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();

    // 記録端末（任意）。読むと、施設で起きたことを少しだけ伝えます。
    FuseBox* record = m_Parts.record;
    if (!m_RecordRead && record != nullptr && record->IsActivated())
    {
        m_RecordRead = true;
        game->RegisterEvidenceCollected();
        ShowNotice("記録: 巡回員は3日目から戻らない 天井裏の足音は今も続いている", 6.0f);
    }

    // 鍵を拾ったら扉を開けて脱出です。
    if (m_Parts.key->IsCollected())
    {
        m_State = State::Escaped;
        game->RegisterHiddenRoomEscaped();
        m_Parts.shadow->SetActive(false);
        Door* door = m_Parts.door;
        door->SetLocked(false);
        door->Interact(player);
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.24f);
        ShowNotice("鍵で扉が開いた", 2.8f);
        return;
    }

    ShadowMan* shadow = m_Parts.shadow;
    if (!shadow->IsActive())
    {
        // ライトで追い払った（または触れられた）直後は、しばらく間を空けてから次を出します。
        if (m_ShadowWasActive)
        {
            m_ShadowWasActive = false;
            m_ShadowTimer = NextShadowDelay;
            ShowNotice("影が消えた 今のうちに鍵を探す", 2.6f);
        }
        m_ShadowTimer -= deltaTime;
        if (m_ShadowTimer <= 0.0f)
        {
            SpawnShadow(player);
        }
        return;
    }

    // 影に触れられると、懐中電灯の電池を奪われます（完全には空にしません）。
    Vector3 toPlayer = player.GetPosition() - shadow->GetPosition();
    toPlayer.y = 0.0f;
    if (toPlayer.Length() < ShadowCatchDistance)
    {
        const float taken = (std::min)(BatteryPenalty, (std::max)(0.0f, player.GetBattery() - 5.0f));
        player.AddBattery(-taken);
        shadow->SetActive(false);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.9f);
        game->GetPostProcess()->TriggerHorrorPulse(0.70f, 0.55f);
        Input::SetVibration(14, 0.36f);
        m_ShadowWasActive = false;
        m_ShadowTimer = NextShadowDelay;
        ShowNotice("影に触れられた 電池を奪われた", 2.8f);
    }
}

// 部屋の四隅のうち、プレイヤーから一番遠い隅に影を出し、ゆっくり近づけます。
// ライトを当て続けると消えます（ShadowManの視線反応）。
void Stage1HiddenRoomEvent::SpawnShadow(const Player& player)
{
    const Vector3 playerPosition = player.GetPosition();
    const Vector3* farthest = &ShadowCorners[0];
    for (const Vector3& corner : ShadowCorners)
    {
        if (Vector3::DistanceSquared(corner, playerPosition) >
            Vector3::DistanceSquared(*farthest, playerPosition))
        {
            farthest = &corner;
        }
    }

    ShadowMan* shadow = m_Parts.shadow;
    shadow->SetPosition(farthest->x, farthest->y, farthest->z);
    shadow->SetActive(true);
    shadow->EnableGazeScare(60.0f);
    shadow->EnableChase(ShadowSpeed, 8.0f);
    m_ShadowWasActive = true;

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, *farthest + Vector3(0.0f, 20.0f, 0.0f), 0.55f, 1.4f);
    ShowNotice("何かが部屋にいる ライトを向ける", 2.8f);
}

void Stage1HiddenRoomEvent::ShowNotice(std::string_view text, float seconds)
{
    m_NoticeText = text;
    m_NoticeTimer = seconds;
}

std::string_view Stage1HiddenRoomEvent::GetObjectiveText() const
{
    if (m_NoticeTimer > 0.0f)
    {
        return m_NoticeText;
    }
    if (m_State == State::Trapped)
    {
        return m_ShadowWasActive
            ? "影にライトを当てて追い払う"
            : "閉じ込められた 暗い部屋で鍵を探す";
    }
    return {};
}
