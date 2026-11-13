// ============================================================================
// ファイルの役割: 1面の隠し部屋（暗証番号の扉の先）に閉じ込められるイベントを管理している。
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
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 隠し部屋の広さ（壁の内側）。扉はx=45の壁（内側の面はx=47）にあり、部屋は東側に広がっている。
    // 経路探索の格子（Stage1HiddenRoomEvent.h の m_Path）も同じ範囲にしている。
    constexpr float RoomMinX = 47.0f;
    constexpr float RoomMaxX = 218.0f;
    constexpr float RoomMinZ = 42.0f;
    constexpr float RoomMaxZ = 178.0f;
    // この線より奥まで入ったら扉を閉めている（扉のすぐ前で閉めて、プレイヤーを挟まないため）。
    constexpr float TrapLineX = 64.0f;

    // 鍵が落ちている場所の候補。部屋は書類棚（配置ファイルの PropHiddenShelf）でジグザグの通路に区切ってあり、
    // どれも入口からは棚に隠れて見えない。順に、入口の通路の南の行き止まり（低い棚の裏）、
    // 真ん中の通路の北の端、一番奥の通路の北の行き止まり。保管灯（Stage1HiddenKeyLamp）もこの順に、近くの壁に付けている。
    const std::array<Vector3, Stage1HiddenRoomEvent::KeySpotCount> KeySpots =
    {
        Vector3(56.0f, -95.0f, 52.0f),
        Vector3(130.0f, -95.0f, 162.0f),
        Vector3(200.0f, -95.0f, 168.0f)
    };

    // 影が現れる場所の候補（各通路の端）。棚にめり込まない候補のうち、プレイヤーから一番遠い所を選んでいる。
    const std::array<Vector3, 5> ShadowSpawnPoints =
    {
        Vector3(64.0f, -99.0f, 160.0f),
        Vector3(100.0f, -99.0f, 58.0f),
        Vector3(130.0f, -99.0f, 58.0f),
        Vector3(160.0f, -99.0f, 165.0f),
        Vector3(198.0f, -99.0f, 92.0f)
    };

    // プレイヤーが部屋の奥（閉める線より奥）にいるか
    bool IsInsideRoom(const Vector3& position)
    {
        return position.x > TrapLineX && position.x < RoomMaxX &&
            position.z > RoomMinZ && position.z < RoomMaxZ;
    }
}

// 受け取ったObjectを覚え、鍵を隠して待機の状態から始めている
void Stage1HiddenRoomEvent::Init(const Parts& parts)
{
    m_Parts = parts;
    m_State = State::Waiting;
    m_ShadowTimer = 0.0f;
    m_ShadowWasActive = false;
    m_RecordRead = false;
    m_KeySpot = 0;
    m_KeyLampTime = 0.0f;
    m_NoticeText = {};
    m_NoticeTimer = 0.0f;
    if (m_Parts.key != nullptr)
    {
        m_Parts.key->SetActive(false);
    }
}

// 状態ごとの処理：暗証番号の扉を開けて奥まで入ったら閉じ込め、閉じ込められている間は鍵と影を見ている
void Stage1HiddenRoomEvent::Update(Player& player, float deltaTime, bool keypadSolved,
    const RoomPathGrid::ResolveCollision& resolveCollision)
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
            // 通れるマスの表は、棚が置かれた後の、閉じ込めた時に一度だけ作っている
            if (!m_Path.IsBuilt())
            {
                m_Path.Build(resolveCollision);
            }
            Trap(player);
        }
        break;
    case State::Trapped:
        UpdateTrapped(player, deltaTime, resolveCollision);
        break;
    case State::Escaped:
        break;
    }
}

// 部屋の奥へ入った瞬間、背後で扉が勢いよく閉まり、鍵がかかる。
void Stage1HiddenRoomEvent::Trap(Player& player)
{
    (void)player;
    m_State = State::Trapped;
    m_ShadowTimer = FirstShadowDelay;
    m_ShadowWasActive = false;

    Door* door = m_Parts.door;
    door->ResetClosed(0);
    door->SetLocked(true);

    // 鍵を3か所の候補からランダムに選んで置いている
    std::uniform_int_distribution<std::size_t> pick(0, KeySpots.size() - 1);
    m_KeySpot = pick(m_Random);
    const Vector3& spot = KeySpots[m_KeySpot];
    m_Parts.key->SetPosition(spot.x, spot.y, spot.z);
    m_Parts.key->SetActive(true);

    // 扉の閉まる大きな音と驚かせる音、画面の乱れ、振動
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_DOOR, door->GetPosition(), 0.62f, 1.8f);
    game->PlayAudioCueAt(SOUND_CUE_SCARE, door->GetPosition(), 0.85f, 0.7f);
    game->GetPostProcess()->TriggerHorrorPulse(0.46f, 0.40f);
    Input::SetVibration(10, 0.26f);
    ShowNotice("扉が閉まった", 2.4f);
    UpdateKeyLamps(0.0f);
}

void Stage1HiddenRoomEvent::UpdateTrapped(Player& player, float deltaTime,
    const RoomPathGrid::ResolveCollision& resolveCollision)
{
    Core::Game* game = Core::Game::GetInstance();

    // 記録端末（任意）。読むと、施設で起きたことを少しだけ伝えている。
    FuseBox* record = m_Parts.record;
    if (!m_RecordRead && record != nullptr && record->IsActivated())
    {
        m_RecordRead = true;
        game->RegisterEvidenceCollected();
        ShowNotice("巡回記録：天井裏の足音は止まらない　保管灯が鍵の場所を示す", 4.0f);
    }

    // 鍵を拾ったら、扉の鍵を外して開け、脱出にしている。
    if (m_Parts.key->IsCollected())
    {
        m_State = State::Escaped;
        UpdateKeyLamps(0.0f);
        game->RegisterHiddenRoomEscaped();
        m_Parts.shadow->SetActive(false);
        Door* door = m_Parts.door;
        door->SetLocked(false);
        door->Interact(player);
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.24f);
        ShowNotice("鍵で扉が開いた", 2.8f);
        return;
    }

    UpdateKeyLamps(deltaTime);

    ShadowMan* shadow = m_Parts.shadow;
    if (!shadow->IsActive())
    {
        // ライトで追い払った（または触れられた）直後は、しばらく間を空けてから次を出している。
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

    // 影は棚を回り込んで近づいてくる
    MoveShadow(player, deltaTime, resolveCollision);

    // 影に触れられると、懐中電灯の電池を奪われる（5%は残し、完全には空にしない）。
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

// 端末を読んだら順に点灯し、その後は鍵の保管場所だけ2回ずつ点滅する。
void Stage1HiddenRoomEvent::UpdateKeyLamps(float deltaTime)
{
    constexpr float ScanStepSeconds = 0.65f;
    constexpr float ScanSeconds = ScanStepSeconds * KeySpotCount;
    constexpr float PulseCycleSeconds = 2.4f;
    constexpr float StandbyBrightness = 0.14f;

    if (m_RecordRead)
    {
        m_KeyLampTime += deltaTime;
    }
    const bool scanning = m_KeyLampTime < ScanSeconds;
    const std::size_t scanLamp = static_cast<std::size_t>(
        (std::min)(m_KeyLampTime, ScanSeconds) / ScanStepSeconds);
    const float pulseTime = std::fmod((std::max)(0.0f, m_KeyLampTime - ScanSeconds), PulseCycleSeconds);
    const bool pulseOn = pulseTime < 0.30f || (pulseTime >= 0.55f && pulseTime < 0.85f);

    for (std::size_t index = 0; index < m_Parts.keyLamps.size(); ++index)
    {
        Wall* lamp = m_Parts.keyLamps[index];
        if (lamp == nullptr)
        {
            continue;
        }
        const bool trapped = m_State == State::Trapped;
        const bool lit = m_RecordRead && (scanning ? index == scanLamp : index == m_KeySpot && pulseOn);
        lamp->SetEmissionScale(trapped ? (lit ? 1.0f : StandbyBrightness) : 0.0f);
    }
}

// 通路の端の候補のうち、棚にめり込まず、プレイヤーから一番遠い所に影を出している。
// 動きは MoveShadow が受け持ち、ライトを当て続けると消える（ShadowManの見られたときの反応）。
void Stage1HiddenRoomEvent::SpawnShadow(const Player& player)
{
    const Vector3 playerPosition = player.GetPosition();
    const Vector3* farthest = nullptr;
    for (const Vector3& point : ShadowSpawnPoints)
    {
        if (m_Path.IsBuilt() && !m_Path.IsWalkable(point))
        {
            continue;
        }
        if (farthest == nullptr ||
            Vector3::DistanceSquared(point, playerPosition) >
            Vector3::DistanceSquared(*farthest, playerPosition))
        {
            farthest = &point;
        }
    }
    if (farthest == nullptr)
    {
        farthest = &ShadowSpawnPoints[0];
    }

    ShadowMan* shadow = m_Parts.shadow;
    shadow->SetPosition(farthest->x, farthest->y, farthest->z);
    shadow->SetActive(true);
    shadow->EnableGazeScare(60.0f);
    m_ShadowStride = 0.0f;
    m_ShadowWasActive = true;

    // 影が出た場所の天井近くで、低く配管の音を鳴らして気配を知らせている
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, *farthest + Vector3(0.0f, 20.0f, 0.0f), 0.55f, 1.4f);
    ShowNotice("何かが部屋にいる ライトを向ける", 2.8f);
}

// 影を、経路探索で次に向かう点へ進めている。棚や壁には押し戻しでめり込まないようにし、
// 一定の歩幅ごとに影の足元から足音を鳴らしている（棚の向こうにいても、立体音響でどこから来るかが分かる）。
void Stage1HiddenRoomEvent::MoveShadow(const Player& player, float deltaTime,
    const RoomPathGrid::ResolveCollision& resolveCollision)
{
    ShadowMan* shadow = m_Parts.shadow;
    const Vector3 before = shadow->GetPosition();
    const Vector3 playerPosition = player.GetPosition();
    Vector3 toPlayer = playerPosition - before;
    toPlayer.y = 0.0f;
    const float playerDistance = toPlayer.Length();
    if (playerDistance < 0.001f)
    {
        return;
    }

    Vector3 direction = m_Path.GetSteeringTarget(before, playerPosition) - before;
    direction.y = 0.0f;
    const float targetDistance = direction.Length();
    if (targetDistance < 0.001f)
    {
        return;
    }
    direction /= targetDistance;
    Vector3 next = before + direction * (std::min)(ShadowSpeed * deltaTime, playerDistance);
    if (resolveCollision)
    {
        resolveCollision(next, ShadowBodyRadius);
    }
    next.x = (std::clamp)(next.x, RoomMinX + ShadowBodyRadius, RoomMaxX - ShadowBodyRadius);
    next.z = (std::clamp)(next.z, RoomMinZ + ShadowBodyRadius, RoomMaxZ - ShadowBodyRadius);
    shadow->SetPosition(next.x, before.y, next.z);

    // 実際に進んだ距離で足音を鳴らしている（押し戻されて止まっている間は鳴らない）
    m_ShadowStride += Vector3(next.x - before.x, 0.0f, next.z - before.z).Length();
    if (m_ShadowStride >= ShadowStrideLength)
    {
        m_ShadowStride -= ShadowStrideLength;
        Core::Game::GetInstance()->PlayAudioCueAt(
            SOUND_CUE_FOOTSTEP, Vector3(next.x, before.y + 4.0f, next.z), 0.74f, 1.7f);
    }
}

// 知らせの文章と、表示する秒数を決めている
void Stage1HiddenRoomEvent::ShowNotice(std::string_view text, float seconds)
{
    m_NoticeText = text;
    m_NoticeTimer = seconds;
}

// 知らせがあればそれを、閉じ込められている間は今やるべきことを返している
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
            : (m_RecordRead
                ? "2回点滅する保管灯の近くで鍵を探す"
                : "鍵を探す　部屋の奥の記録端末も調べられる");
    }
    return {};
}
