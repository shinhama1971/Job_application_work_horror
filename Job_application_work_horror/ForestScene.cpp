// ============================================================================
// ファイルの役割: 外の森の場面の、作る・更新する・描く・片付ける処理。
// ForestScene.hで宣言した処理の中身をここで定義している。
// ============================================================================

#include "ForestScene.h"
#include "ShadowFigureGeometry.h"
#include "EventFeedback.h"
#include "ForestGrass.h"
#include "ForestLightningFigure.h"
#include "ForestRain.h"
#include "LabEntrance.h"
#include "Wall.h"
#include "SkyBackdrop.h"
#include "Application.h"
#include "CaptureMode.h"
#include "Game.h"
#include "Ground.h"
#include "Input.h"
#include "Player.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "SceneLayoutBuilder.h"
#include "Utility.h"

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 研究所の玄関の扉の位置（配置ファイルに LabDoor がないときに使う）と、調べる点を扉から手前へ出す距離
    const Vector3 DefaultLabDoorPosition(0.0f, -77.0f, 297.4f);
    constexpr float EntranceInFront = 2.0f;
    // 稲光を光らせる位置（扉から見た上空の斜め手前）、照らす範囲、一番明るいときの強さ
    const Vector3 LightningOffset(-55.0f, 190.0f, -62.0f);
    constexpr float LightningRange = 285.0f;
    constexpr float LightningStrength = 4.8f;
    // 稲光が起きる条件：研究所からこの距離の範囲にいて、研究所の方を向いている（向きとの内積がこれ以上）
    constexpr float LightningMinDistance = 60.0f;
    constexpr float LightningMaxDistance = 210.0f;
    constexpr float LightningFacing = 0.78f;
    // 草むらを通る「見えない何か」が草を押し倒す半径
    constexpr float WalkerGrassRadius = 10.0f;
    // 稲光の色（月明かりより青白い、少し冷たい白）
    const Vector3 LightningColor(0.67f, 0.82f, 1.0f);
    // 稲光の光り方：立ち上がる時間と、全体で光っている時間（立ち上がりだけ鋭く、後半は滑らかに消す）
    constexpr float LightningRiseSeconds = 0.05f;
    constexpr float LightningFlashSeconds = 0.46f;

    // --- 稲光の一瞬だけ浮かぶ人影
    // 稲光は2回。1回目は遠くの木の間に、2回目は、1回目が消えてからこの秒数の後に、もっと近くに人影が立つ
    constexpr int LightningStrikeCount = 2;
    constexpr float SecondStrikeDelaySeconds = 5.5f;
    // 2回目が起きる、研究所までの距離の下限（1回目より玄関に近くても起きるようにしている）
    constexpr float SecondStrikeMinDistance = 30.0f;
    // 人影を立たせる所：プレイヤーから研究所までの距離に掛ける割合と、その距離の最小・最大、小道からの横のずれ（1回目・2回目）
    constexpr float FigureAlongFraction[LightningStrikeCount] = { 0.55f, 0.30f };
    constexpr float FigureMinAlong[LightningStrikeCount] = { 50.0f, 24.0f };
    constexpr float FigureMaxAlong[LightningStrikeCount] = { 120.0f, 55.0f };
    constexpr float FigureSideOffset[LightningStrikeCount] = { 20.0f, 9.0f };
    // 横のずれを試す順番（ずれに掛ける割合。木に埋まるか木の陰になるなら次を試す）
    constexpr float FigureSideTries[] = { 1.0f, -1.0f, 0.5f, -0.5f, 0.0f };
    // 木などから離す距離（埋まらないように）と、視線を遮る木とみなす太さのゆとり、遮る物とみなす高さ（胸より高い物）
    constexpr float FigureClearance = 5.0f;
    constexpr float FigureSightMargin = 2.0f;
    // 稲光の強さ（0〜1）から人影の見え方にする倍率（光の後半で崩れ始めるように、1より大きくしている）
    constexpr float FigureVisibilityGain = 1.8f;
    // 人影を見せている間の恐怖の画面効果の強さ（1回目・2回目）
    constexpr float FigureFear[LightningStrikeCount] = { 0.35f, 0.70f };
    // 2回目の人影が近くに立ったときの、低い衝撃音（高さ・音量）と振動
    constexpr EventFeedback::SoundSpec SecondFigureSound{ 0.60f, 0.55f };
    constexpr EventFeedback::RumbleSpec SecondFigureRumble{ 10, 0.25f };
    // 向きを計算しないほど短いとみなす長さの2乗
    constexpr float MinimumLengthSquared = 0.0001f;

    // 夜の森の画面の見え方：少し暗めにし、ノイズと周辺減光を強めて、照明の下の光の筋をやや控えめに出している
    constexpr float ForestExposure = 0.92f;
    constexpr float ForestNoiseAmount = 0.20f;
    constexpr float ForestVignetteStrength = 0.62f;
    constexpr float ForestVolumetricIntensity = 0.42f;
    // 森を出るときに戻す、建物の中の見え方（PostProcess の初期値と同じ）
    constexpr float IndoorExposure = 1.0f;
    constexpr float IndoorNoiseAmount = 0.18f;
    constexpr float IndoorVignetteStrength = 0.55f;
    constexpr float IndoorVolumetricIntensity = 0.58f;

    // 雨の輪を広げる波の計算：マス目の数と、計算する範囲の一辺（マス目の大きさ 0.75 を変えずに、カメラから前後左右に約12メートル）
    constexpr int ForestRippleResolution = 768;
    constexpr float ForestRippleWorldSize = 576.0f;

    // 怖さがこれだけ急に上がったら、視界の乱れを一瞬強める
    constexpr float FearSpikeThreshold = 0.1f;
    // 無線の雑音の強さを、画面の電波の乱れと、何かがいる気配のちらつきに分ける割合
    constexpr float RadioNoiseToInterference = 0.7f;
    constexpr float RadioNoiseToPresenceStatic = 0.45f;
    // 玄関から入るときの暗転は、場面を切り替える少し前（この割合の時間）に真っ暗にしている
    constexpr float EnterFadeCompleteRatio = 0.85f;
}

// 稲光の間だけ既存の点光源リストに光を足す。光源自体の形や影は描かない。
class ForestLightning final : public Object
{
public:
    void Init() override {}
    void Update() override {}
    void Draw(Camera*) override {}
    void Uninit() override {}

    void SetStrength(float strength) { m_Strength = strength; }

    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override
    {
        if (m_Strength > 0.0f)
        {
            lights.push_back({ Vector4(m_Position.x, m_Position.y, m_Position.z, LightningRange),
                Vector4(LightningColor.x, LightningColor.y, LightningColor.z, m_Strength) });
        }
    }

private:
    float m_Strength = 0.0f;
};

bool ForestScene::IsRequestedByCommandLine()
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr)
    {
        return false;
    }
    bool requested = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::wstring(arguments[index]) == L"--forest")
        {
            requested = true;
        }
    }
    LocalFree(arguments);
    return requested;
}

void ForestScene::ApplyAtmosphere()
{
    Core::Game* game = Core::Game::GetInstance();
    // 夜の森：電力の状態は使わない（灯りは配置ファイルの glow の付いた物だけ）。少し暗めにし、照明の下の霧の光の筋を出している
    game->SetPowerRestored(false);
    game->GetPostProcess()->SetCorridorTension(0.0f);
    game->GetPostProcess()->SetExposure(ForestExposure);
    game->GetPostProcess()->SetAtmosphere(ForestNoiseAmount, ForestVignetteStrength);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(ForestVolumetricIntensity);
    // 地面に建物の床の目地やひびが出ないよう、壁と床の古さは描かない。屋外なので、部屋の角の暗がりも使わない
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
    Renderer::SetLoopDepth(0.0f);
}

void ForestScene::RestoreAtmosphere()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    game->GetPostProcess()->SetAtmosphere(IndoorNoiseAmount, IndoorVignetteStrength);
    game->GetPostProcess()->SetExposure(IndoorExposure);
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(IndoorVolumetricIntensity);
}

PropLayout ForestScene::LoadLayout()
{
    PropLayout layout = PropLayoutFile::Load(PropLayoutPath);
    if (!layout.IsValid())
    {
        Utility::ReportFatalError("森の配置を読めませんでした（" + std::to_string(layout.ErrorLine) + "行目）: " + layout.Error);
    }
    return layout;
}

ForestScene::World ForestScene::BuildWorld(SceneLayoutBuilder& builder, const PropLayout& layout)
{
    World world;
    // 空の霧（地平線の線を消す）。手前に描く物を上書きしないよう、最初に作って最初に描いている
    builder.Create<SkyBackdrop>("ForestSky");
    world.PlayerObject = builder.Create<Player>("Player");
    PropLayoutFile::ApplyPlacement(*world.PlayerObject, PropLayoutFile::GetPlacement(layout, "Player"));

    // 地面（1面・2面と同じ濡れた床。水たまりは床全体のマス目に乱数で置かれ、雨のあとの夜の森になる）
    world.GroundObject = builder.Create<Ground>("ForestGround");
    PropLayoutFile::ApplyPlacement(*world.GroundObject, PropLayoutFile::GetPlacement(layout, "Ground"));

    // 木・岩・倒木・茂み・研究所・灯り（prop 行）
    world.Props = PropLayoutFile::Spawn(builder, layout);
    // 木や岩を置いた後で草を作り、その足元と小道を避けて植える。
    world.Grass = builder.Create<ForestGrass>("ForestGrass");
    PropLayoutFile::ApplyPlacement(*world.Grass, PropLayoutFile::GetPlacement(layout, "ForestGrass"));
    // 雨：加算合成で深度を書かないので、ほかの物を全部描いた後に描くよう、最後に作っている。
    // 水たまりには、同じ雨の輪を波のシミュレーションで広げている
    // （RainEnabled が false のときは作らない）
    if constexpr (RainEnabled)
    {
        ForestRain* rain = builder.Create<ForestRain>("ForestRain");
        rain->SetGroundHeight(world.GroundObject->GetPosition().y);
        rain->SetIntensity(RainStrength);
        world.GroundObject->SetRainIntensity(RainStrength);
        // 波を計算する範囲を広げ、少し離れた水たまりにも雨の輪が広がるようにしている
        world.GroundObject->SetRippleArea(ForestRippleResolution, ForestRippleWorldSize);
    }
    return world;
}

ForestScene::ForestScene()
{
    Core::Game* game = Core::Game::GetInstance();
    ApplyAtmosphere();
    const PropLayout layout = LoadLayout();
    SceneLayoutBuilder builder(*game, m_ObjectNames);
    World world = BuildWorld(builder, layout);
    m_Player = world.PlayerObject;
    m_GroundY = world.GroundObject->GetPosition().y;
    m_Grass = world.Grass;
    m_LayoutProps = std::move(world.Props);

    // 研究所の玄関：扉（配置ファイルの LabDoor）の手前に、「研究所に入る」と調べられる点を置いている
    m_Entrance = builder.Create<LabEntrance>("LabEntrance");
    Vector3 doorPosition = DefaultLabDoorPosition;
    for (const auto& [name, wall] : m_LayoutProps)
    {
        if (name == "LabDoor" && wall != nullptr)
        {
            doorPosition = wall->GetPosition();
        }
    }
    m_Entrance->SetPosition(doorPosition - Vector3(0.0f, 0.0f, EntranceInFront));
    m_Lightning = builder.Create<ForestLightning>("ForestLightning");
    // 上空の斜め手前から、正面と屋根の輪郭を一緒に照らす。
    m_Lightning->SetPosition(doorPosition + LightningOffset);
    m_LightningFigure = builder.Create<ForestLightningFigure>("ForestLightningFigure");

    // 配置を読み直したときに置き直す物（地面と草の範囲）
    Ground* ground = world.GroundObject;
    m_PlacementBindings.emplace_back("Ground", [ground](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*ground, placement);
    });
    m_PlacementBindings.emplace_back("ForestGrass", [this](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*m_Grass, placement);
    });

    // カメラの位置を最初から合わせるため、プレイヤーを一度更新している
    m_Player->Update();
    m_Hud.Init();
    // 森には記録（残された資料）を置いていないので、右上の「記録 0 / 4」は出さない
    m_Hud.SetEvidenceVisible(false);
}

ForestScene::~ForestScene()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    m_Radio.Stop();
    RestoreAtmosphere();
    game->GetPostProcess()->SetSignalInterference(0.0f);
    for (const std::string& name : m_ObjectNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}

void ForestScene::ReloadPropLayout()
{
    const PropLayout layout = PropLayoutFile::Load(PropLayoutPath);
    if (!layout.IsValid())
    {
        m_Notice = "配置のファイルの" + std::to_string(layout.ErrorLine) + "行目が読めない " + layout.Error;
        m_NoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return;
    }
    SceneLayoutBuilder builder(*Core::Game::GetInstance(), m_ObjectNames);
    if (!PropLayoutFile::Reload(builder, layout, m_LayoutProps, m_Notice))
    {
        m_NoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return;
    }
    // プレイヤーの位置は、歩いている途中の状態なので変えない
    const int placed = PropLayoutFile::ReapplyPlacements(layout, m_PlacementBindings);
    if (m_Grass != nullptr) m_Grass->Rebuild();
    m_Notice = "配置のファイルを読み直した（小物 " + std::to_string(layout.Props.size()) + "個・置き場所 " +
        std::to_string(placed) + "個）";
    m_NoticeTimer = PropLayoutFile::LayoutSuccessNoticeSeconds;
}

void ForestScene::UpdateLightning(float deltaTime, const Vector3& forward)
{
    constexpr float flashSeconds = LightningFlashSeconds;
    m_LightningFear = 0.0f;
    if (!RainEnabled || m_EnterTimer >= 0.0f || Tools::CaptureMode::IsBenchmark())
    {
        m_Lightning->SetStrength(0.0f);
        m_LightningFigure->Hide();
        return;
    }
    if (m_LightningTime < 0.0f)
    {
        // 光っていない間：決まった回数まで、次に光れる時間になるのを待つ
        if (m_LightningStrikes >= LightningStrikeCount)
        {
            return;
        }
        m_LightningCooldown = (std::max)(0.0f, m_LightningCooldown - deltaTime);
        if (m_LightningCooldown > 0.0f)
        {
            return;
        }
        const Vector3 offset = m_Entrance->GetInteractionPosition() - m_Player->GetPosition();
        const Vector2 towardLab(offset.x, offset.z);
        const float distanceSquared = towardLab.LengthSquared();
        const Vector2 look(forward.x, forward.z);
        const float minDistance = m_LightningStrikes == 0 ? LightningMinDistance : SecondStrikeMinDistance;
        // 小道から研究所を向いたときに起きる。足元や背後を見ている間は待つ。
        if (offset.z <= 0.0f || distanceSquared < minDistance * minDistance ||
            distanceSquared > LightningMaxDistance * LightningMaxDistance ||
            look.Dot(towardLab) < LightningFacing * std::sqrt(distanceSquared))
        {
            return;
        }
        m_LightningTime = 0.0f;
        ShowLightningFigure(m_LightningStrikes);
        ++m_LightningStrikes;
    }
    else
    {
        m_LightningTime = (std::min)(m_LightningTime + deltaTime, flashSeconds);
    }

    // 立ち上がりだけ鋭く、後半は滑らかに消す。1回の光の中では点滅させない。
    constexpr float riseSeconds = LightningRiseSeconds;
    const float rise = (std::min)(1.0f, m_LightningTime / riseSeconds);
    const float tail = (std::min)(1.0f, (flashSeconds - m_LightningTime) / (flashSeconds - riseSeconds));
    const float flash = rise * tail * tail;
    m_Lightning->SetStrength(LightningStrength * flash);

    // 人影は、稲光が明るい間だけ逆光で浮かび、光が弱まると崩れて消える
    m_LightningFigure->SetLightning((std::min)(1.0f, flash * FigureVisibilityGain),
        m_Lightning->GetPosition(), LightningColor, flash);
    const int shownStrike = (std::max)(0, m_LightningStrikes - 1);
    m_LightningFear = FigureFear[shownStrike] * flash;

    if (m_LightningTime >= flashSeconds)
    {
        // 光り終わった：人影を消し、次の稲光まで待つ
        m_LightningTime = -1.0f;
        m_LightningCooldown = SecondStrikeDelaySeconds;
        m_Lightning->SetStrength(0.0f);
        m_LightningFigure->Hide();
    }
}

void ForestScene::ShowLightningFigure(int strikeIndex)
{
    const Vector3 player = m_Player->GetPosition();
    const Vector3 door = m_Entrance->GetInteractionPosition();
    Vector2 toDoor(door.x - player.x, door.z - player.z);
    const float distance = toDoor.Length();
    if (distance * distance < MinimumLengthSquared)
    {
        return;
    }
    toDoor /= distance;
    const Vector2 side(toDoor.y, -toDoor.x);
    const float along = std::clamp(distance * FigureAlongFraction[strikeIndex],
        FigureMinAlong[strikeIndex], FigureMaxAlong[strikeIndex]);

    // 研究所の方へ進んだ所から、横にずらした場所を順に試し、木に埋まらず、木の陰にもならない所を選んでいる
    Vector3 chosen = Vector3::Zero;
    for (std::size_t index = 0; index < std::size(FigureSideTries); ++index)
    {
        const float sideTry = FigureSideTries[index];
        const Vector2 spot = Vector2(player.x, player.z) + toDoor * along + side * (FigureSideOffset[strikeIndex] * sideTry);
        const Vector3 candidate(spot.x, m_GroundY, spot.y);
        if (index == 0)
        {
            // どこも塞がっていたときは、最初に試した場所を使う
            chosen = candidate;
        }
        if (!IsFigureSpotBlocked(candidate))
        {
            chosen = candidate;
            break;
        }
    }

    const Vector3 toPlayer = player - chosen;
    const float facing = std::atan2(toPlayer.x, toPlayer.z);
    m_LightningFigure->Show(chosen, facing);

    // 2回目は近くに立つので、低い衝撃音と振動で気づかせている（1回目は音を出さず、見えたかどうか分からないくらいにしている）
    if (strikeIndex > 0)
    {
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_SCARE, chosen,
            SecondFigureSound.Pitch, SecondFigureSound.Volume);
        EventFeedback::Rumble(SecondFigureRumble);
    }
}

bool ForestScene::IsFigureSpotBlocked(const Vector3& spot) const
{
    const Vector2 eye(m_Player->GetPosition().x, m_Player->GetPosition().z);
    const Vector2 target(spot.x, spot.z);
    const Vector2 sight = target - eye;
    const float sightLengthSquared = (std::max)(sight.LengthSquared(), MinimumLengthSquared);
    for (const auto& [name, wall] : m_LayoutProps)
    {
        (void)name;
        if (wall == nullptr || !wall->IsCollisionEnabled())
        {
            continue;
        }
        const Vector3 position = wall->GetPosition();
        const Vector3 half = wall->GetScale() * 0.5f;
        // 低い物（岩・倒木・茂み）は人影を隠さないので、胸より高い物だけを調べている
        if (position.y + std::fabs(half.y) < m_GroundY + ShadowFigureGeometry::ChestHeight)
        {
            continue;
        }
        // 上から見た太さを、回した向きを気にしない円とみなしている
        const float radius = (std::max)(std::fabs(half.x), std::fabs(half.z));
        const Vector2 center(position.x, position.z);
        if ((center - target).LengthSquared() < (radius + FigureClearance) * (radius + FigureClearance))
        {
            return true;
        }
        // 目から人影への線分に、一番近い点までの距離
        const float t = std::clamp((center - eye).Dot(sight) / sightLengthSquared, 0.0f, 1.0f);
        const Vector2 closest = eye + sight * t;
        if ((center - closest).LengthSquared() < (radius + FigureSightMargin) * (radius + FigureSightMargin))
        {
            return true;
        }
    }
    return false;
}

void ForestScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    if (m_Player == nullptr)
    {
        return;
    }
    // 街灯のちらつき・後ろの街灯が消える・研究所の窓の明かりが消える・草むらを何かが通る（ForestEvents）
    // 稲光の人影の恐怖（前のフレームの値）と、強い方を使っている
    const float fear = (std::max)(m_Events.Update(deltaTime, m_Player->GetPosition(), m_LayoutProps), m_LightningFear);
    game->GetPostProcess()->SetFearVision(fear, fear > m_LastFear + FearSpikeThreshold);
    m_LastFear = fear;
    // 草むらを通る「見えない何か」の足元の草を、草の Compute Shader で押し倒している
    if (m_Grass != nullptr)
    {
        m_Grass->SetUnseenWalker(m_Events.GetWalkerPosition(), WalkerGrassRadius, m_Events.GetWalkerStrength());
    }

    // 研究棟からの無線：小道を進むと、受信記録が1行ずつ届き、研究所が見えてくると途絶える。行が届く瞬間に画面も乱れる
    const float radioNoise = m_EnterTimer < 0.0f ? m_Radio.Update(deltaTime, m_Player->GetPosition()) : 0.0f;
    game->GetPostProcess()->SetSignalInterference(radioNoise * RadioNoiseToInterference);
    game->GetPostProcess()->SetPresenceStatic(radioNoise * RadioNoiseToPresenceStatic);

    if (m_EnterTimer < 0.0f)
    {
        // 視線の先の調べられる物（研究所の玄関）を選び、E（A）で調べている
        m_InteractionSystem.Update(*m_Player);
        if (m_Entrance != nullptr && m_Entrance->WasEntered())
        {
            // 玄関を開けて入る：扉の音を鳴らし、暗転してから、目的の画面（IntroScene）を挟んで1面を始める
            m_EnterTimer = 0.0f;
            m_Radio.Stop();
            game->PlayAudioCue(SOUND_CUE_DOOR);
        }
    }
    else
    {
        m_EnterTimer += deltaTime;
        if (m_EnterTimer >= EnterFadeSeconds && !m_SceneChangeRequested)
        {
            m_SceneChangeRequested = true;
            game->RequestSceneChange(SceneName::Intro);
        }
    }
    UpdateLightning(deltaTime, game->GetCamera()->GetForward());
#if defined(_DEBUG)
    // Debug版だけ：F5 を押したとき、または配置のファイルが書き換わったとき（Unity で書き出したときなど）に読み直している
    if (Input::GetKeyTrigger(VK_F5) || m_LayoutWatcher.HasChanged(PropLayoutPath, deltaTime))
    {
        ReloadPropLayout();
    }
#endif
}

void ForestScene::Draw(Camera* camera)
{
    (void)camera;
    if (m_Player == nullptr)
    {
        return;
    }
    // 目的：無線が途絶えるまでは研究所へ向かい、途絶えたら玄関を調べる
    std::string_view objective = m_Radio.IsSignalLost() ? "研究所の玄関を調べて、中に入る" : "小道を北へ進み、研究所へ向かう";
    if (m_NoticeTimer > 0.0f)
    {
        objective = m_Notice;
    }
    m_Hud.Draw(*m_Player, -1, m_EnterTimer < 0.0f ? m_InteractionSystem.GetPrompt() : std::string_view{}, objective);
    Hud::RadioView radio;
    m_Radio.FillView(radio);
    m_Hud.DrawRadio(radio);
    // 玄関から入るときの暗転
    if (m_EnterTimer >= 0.0f)
    {
        const float fade = (std::min)(1.0f, m_EnterTimer / (EnterFadeSeconds * EnterFadeCompleteRatio));
        m_Hud.DrawBlink(fade, 1.0f);
    }
    // ポーズ中はポーズメニューを重ねている（ESC・P・START で開くのは Game::Update。ここで描かないと、止まっただけに見える）
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetResolutionLevel(),
            game->GetPauseSettingIndex(),
            0,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}
