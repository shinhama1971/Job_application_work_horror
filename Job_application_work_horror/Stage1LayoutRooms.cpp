// ============================================================================
// ファイルの役割: 1面の壁の文字、暗証番号の先の部屋、驚かせる演出に使う物を配置する。
// ============================================================================

#include "Stage1LayoutBuilder.h"
#include "BatteryItem.h"
#include "CaptureMode.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FlashlightWriting.h"
#include "FuseBox.h"
#include "KeyItem.h"
#include "ScareTrigger.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"
#include "VoiceRecorder.h"
#include "Wall.h"

#include <algorithm>
#include <array>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // 出口（最後のドアのすぐ先）の位置
    constexpr float ExitTriggerOffsetX = 4.0f;
    constexpr float ExitTriggerY = -80.0f;
    // 隠し部屋の鍵の候補の近くの、琥珀色の保管灯
    const Color KeyLampDiffuse(0.21f, 0.13f, 0.06f, 1.0f);
    const Color KeyLampEmission(1.0f, 0.45f, 0.08f, 1.0f);
    constexpr float KeyLampShininess = 12.0f;
    constexpr float KeyLampGlowRange = 46.0f;
    constexpr float KeyLampGlowIntensity = 2.5f;
    // 暗証番号の手がかり：「ひとつめ」などの文字の大きさ、その右に書く数字の大きさと、文字の中心からの距離
    constexpr float DigitClueLabelWidth = 40.0f;
    constexpr float DigitClueLabelHeight = 10.0f;
    constexpr float DigitClueDigitSize = 20.0f;
    constexpr float DigitClueDigitOffset = 30.0f;
    // 驚かせる瞬間に重ねる画面のノイズの強さ
    constexpr float ScareNoisePower = 0.7f;
}

namespace Stage1Layout
{
    // 進む道筋に沿って、懐中電灯で浮かぶ手がかりと消灯時の異変を置く。
    void Stage1Builder::BuildWallWritings()
    {
        m_Objects.writings[Stage1WallWritings::Warning] = CreateWriting(
            "Stage1WritingWarning", "assets/texture/writing_warning.png", "");
        m_Objects.writings[Stage1WallWritings::Three] = CreateWriting(
            "Stage1WritingThree", "assets/texture/writing_three.png", "");
        m_Objects.writings[Stage1WallWritings::Power] = CreateWriting(
            "Stage1WritingPower", "assets/texture/writing_power.png", "");
        // ループ廊下を北へ進むと正面に見える突き当たりの壁。右へ曲がって先へ進み、文字が背中側になると書き換わる
        // （横を見ただけでは変わらない。振り返って見ると「ふりかえったな」になっている）。
        m_Objects.writings[Stage1WallWritings::Turn] = CreateWriting(
            "Stage1WritingTurn", "assets/texture/writing_turn.png",
            "assets/texture/writing_turned.png");

        // 点灯時の警告とは別の高さに置き、同じ壁で視線を大きく動かさずに変化を見つけられるようにする。
        constexpr Color DarkCorridorInk(0.29f, 0.40f, 0.36f, 1.0f);
        m_Objects.darkCorridorWriting = CreateWriting(
            "Stage1DarkCorridorWriting", "assets/texture/writing_dark_corridor.png", "", false);
        if (auto* writing = m_Objects.darkCorridorWriting)
        {
            writing->SetDarknessReveal();
            writing->SetInkColor(DarkCorridorInk);
            writing->SetPresenceImmediately(0.0f);
        }

        // ループの周回ごとに、開始地点の部屋に1つずつ浮かぶ文字。最初は隠しておき、StageScene が周回に合わせて浮かばせている。
        // 1周目は奥のドアの右、2周目は東の壁、3周目は西の壁（戻ってくるたびに、部屋の中の文字が増えていく）
        const struct { const char* Name; const char* Texture; } lapWritingSpots[] =
        {
            { "Stage1WritingLap1", "assets/texture/writing_lap1.png" },
            { "Stage1WritingLap2", "assets/texture/writing_lap2.png" },
            { "Stage1WritingLap3", "assets/texture/writing_lap3.png" },
        };
        for (std::size_t lap = 0; lap < m_Objects.lapWritings.size(); ++lap)
        {
            FlashlightWriting* writing = CreateWriting(
                lapWritingSpots[lap].Name, lapWritingSpots[lap].Texture, "");
            writing->SetPresenceImmediately(0.0f);
            m_Objects.lapWritings[lap] = writing;
        }
    }

    // 暗証番号の扉（任意で探索）とその先の部屋。中央の扉の先の細い廊下の東の壁にあり、ループ廊下へ向かうたびに目に入る。
    // 番号の手がかり・予備の電池・迎える文字・閉じ込めイベント（Stage1HiddenRoomEvent）で使う鍵・記録端末・影・保管灯
    void Stage1Builder::BuildKeypadRoom()
    {
        Door* keypadDoor = m_Builder.Create<Door>("Stage1KeypadDoor");
        Place(*keypadDoor, "Stage1KeypadDoor");
        keypadDoor->SetLocked(true);
        CreateDoorLintel(*keypadDoor, "Stage1KeypadDoorLintel");

        FuseBox* keypadPanel = m_Builder.Create<FuseBox>("Stage1KeypadPanel");
        keypadPanel->SetManualControl("暗証番号を入力する");
        keypadPanel->SetManualInteractionAllowed(true);
        Place(*keypadPanel, "Stage1KeypadPanel");

        m_Objects.keypad.panel = keypadPanel;
        m_Objects.keypad.door = keypadDoor;
        m_Objects.keypad.digitWritings = {
            // 右の倉庫の西の壁（開始地点との仕切りの、倉庫の側。開始地点の側には2周目の壁の文字があるため）
            CreateDigitClue(1),
            // 左の倉庫の西の壁
            CreateDigitClue(2),
            // 中央ホールの南の壁
            CreateDigitClue(3),
            // 右の倉庫の東の壁
            CreateDigitClue(4) };

        // 扉の先の部屋: 予備の電池と、入った者を迎える文字を置いている。
        BatteryItem* hiddenBattery = m_Builder.Create<BatteryItem>("Stage1KeypadRoomBattery");
        Place(*hiddenBattery, "Stage1KeypadRoomBattery");
        CreateWriting("Stage1KeypadRoomWriting", "assets/texture/writing_watched.png", "");

        // HUDを切っていても、先に開けた部屋の空箱が進行不良に見えないようにする。
        if (auto* fuseCaseSign = CreateWriting(
            "Stage1KeypadFuseCaseSign", "assets/texture/stage1_keypad_fuse_case_sign.png", "", false))
        {
            fuseCaseSign->SetPrintedSurface();
            fuseCaseSign->SetInkColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        }

        // 鍵は閉じ込めた瞬間に、部屋の隅の候補からランダムな位置へ置いている。
        KeyItem* hiddenRoomKey = m_Builder.Create<KeyItem>("Stage1HiddenRoomKey");
        Place(*hiddenRoomKey, "Stage1HiddenRoomKey");
        hiddenRoomKey->SetActive(false);

        FuseBox* hiddenRoomRecord = m_Builder.Create<FuseBox>("Stage1HiddenRoomRecord");
        hiddenRoomRecord->SetManualControl("記録端末を調べる");
        hiddenRoomRecord->SetManualInteractionAllowed(true);
        Place(*hiddenRoomRecord, "Stage1HiddenRoomRecord");

        ShadowMan* hiddenRoomShadow = m_Builder.Create<ShadowMan>("Stage1HiddenRoomShadow");
        hiddenRoomShadow->SetDeactivateOnExpire(true);
        hiddenRoomShadow->SetActive(false);

        m_Objects.hiddenRoom.door = keypadDoor;
        m_Objects.hiddenRoom.key = hiddenRoomKey;
        m_Objects.hiddenRoom.record = hiddenRoomRecord;
        m_Objects.hiddenRoom.shadow = hiddenRoomShadow;

        // 鍵の候補と同じ順に、近くの壁へ琥珀色の保管灯を付ける。
        const std::array<const char*, Stage1HiddenRoomEvent::KeySpotCount> keyLampNames = {
            "Stage1HiddenKeyLampSouth", "Stage1HiddenKeyLampNorthWest", "Stage1HiddenKeyLampNorthEast" };
        for (std::size_t index = 0; index < keyLampNames.size(); ++index)
        {
            Wall* lamp = CreatePlacedStageProp(keyLampNames[index], KeyLampDiffuse, KeyLampEmission, KeyLampShininess, false);
            lamp->SetCastsShadow(false);
            lamp->SetVisualModel("warning_lamp");
            lamp->SetGlowLight(KeyLampGlowRange, KeyLampGlowIntensity);
            lamp->SetEmissionScale(0.0f);
            m_Objects.hiddenRoom.keyLamps[index] = lamp;
        }
    }

    // 書類保管室の影・出口・保管室の電池・画面のノイズ・中央の廊下の驚かせる演出・演出用の人影
    void Stage1Builder::BuildEventsAndShadows()
    {
        // 書類保管室の「照らすと止まる影」。最初は出さず、位置はStage1LightStalkerが毎フレーム決めている。
        ShadowMan* archiveStalker = m_Builder.Create<ShadowMan>("Stage1ArchiveStalker");
        archiveStalker->SetExternallyControlled(true);
        archiveStalker->SetActive(false);
        m_Objects.archiveStalker = archiveStalker;

        // 出口（最後のドアの向こう。調べる対象にはせず、開いたドアを通ったときにStageSceneが脱出を始めている）
        ExitTrigger* exit = m_Builder.Create<ExitTrigger>("ExitTrigger");
        exit->SetPosition(Stage1EndDoorX + ExitTriggerOffsetX, ExitTriggerY, Stage1EndDoorZ);
        exit->SetNextScene(SceneName::Stage2);
        exit->SetInteractionEnabled(false);
        // ドアの先の階段を上って2階（2面）へ移る演出にしている。
        // 自動撮影・計測は時間で道順を決めているので、2面の始まりがずれないよう、前の演出（光とノイズ、2.12秒）のままにしている
        exit->SetStairClimb(!Tools::CaptureMode::IsActive());
        m_Objects.exitTrigger = exit;

        // 電池（書類保管室）
        BatteryItem* battery = m_Builder.Create<BatteryItem>("BatteryItem");
        Place(*battery, "BatteryItem");

        // 驚かせる瞬間に重ねる画面のノイズ
        ScreenDustOverlay* crt = m_Builder.Create<ScreenDustOverlay>("CRTNoise");
        crt->SetPower(ScareNoisePower);
        crt->SetActive(false);

        // 電力が戻った後、中央の廊下へ入ると、奥に影が現れる驚かせる演出
        ScareTrigger* corridorScare = m_Builder.Create<ScareTrigger>("ScareTrigger_Corridor");
        // 踏み込むと影が出る範囲（size）と、影を出す位置は、配置ファイルの place ScareTrigger_Corridor・ScareTrigger_CorridorShadow
        const auto placeCorridorScare = [corridorScare](const PropPlacement& placement)
        {
            PropLayoutFile::MoveTo(*corridorScare, placement.Position);
            corridorScare->SetSize(placement.Size);
        };
        placeCorridorScare(At("ScareTrigger_Corridor"));
        Bind("ScareTrigger_Corridor", placeCorridorScare);
        const auto placeCorridorShadow = [corridorScare](const PropPlacement& placement)
        {
            corridorScare->SetShadowPosition(placement.Position);
        };
        placeCorridorShadow(At("ScareTrigger_CorridorShadow"));
        Bind("ScareTrigger_CorridorShadow", placeCorridorShadow);
        corridorScare->SetRequiresPower(true);

        // 演出用の人影。どれも最初は隠しておき、StageSceneの演出で出している（時間切れで破棄せずに隠す）
        ShadowMan* exitOmen = CreateHiddenShadow("Stage1ExitOmen");
        Place(*exitOmen, "Stage1ExitOmen");
        m_Objects.exitOmen = exitOmen;
        m_Objects.fuseWatcher = CreateHiddenShadow("Stage1FuseWatcher");
        m_Objects.storageShadow = CreateHiddenShadow("Stage1StorageShadow");
        m_Objects.evidenceShadow = CreateHiddenShadow("Stage1EvidenceShadow");
        m_Objects.timeStopFigure = CreateHiddenShadow("Stage1TimeStopFigure");
        m_Objects.echoFigure = CreateHiddenShadow("Stage1EchoFigure");
    }

    // 懐中電灯で照らしたときだけ浮かぶ壁の文字を作っている。
    // 位置は壁の表面、向き（yaw）は壁から通路側へ向かう法線、幅と高さは size の1つ目と2つ目（配置のファイルの place 行）。画像は横長(4:1)。
    FlashlightWriting* Stage1Builder::CreateWriting(const char* name, const char* texture, const char* alternateTexture,
        bool required)
    {
        // 旧版のUnityの配置を書き出しても、今回追加した飾りがないだけでステージを開始できる。
        if (!required && std::ranges::none_of(m_Layout.Placements,
            [name](const PropPlacement& entry) { return entry.Name == name; }))
        {
            return nullptr;
        }
        const PropPlacement& placement = At(name);
        FlashlightWriting* writing = m_Builder.Create<FlashlightWriting>(name);
        writing->SetTextures(texture, alternateTexture);
        writing->Place(placement.Position, PropLayoutFile::GetFacing(placement), placement.Size.x, placement.Size.y);
        Bind(name, [writing](const PropPlacement& moved)
        {
            writing->Place(moved.Position, PropLayoutFile::GetFacing(moved), moved.Size.x, moved.Size.y);
        });
        return writing;
    }

    // 暗証番号の手がかり。「ひとつめ」などの文字の右に、その桁の数字を書いている（数字の画像は、番号を決めるStage1KeypadDoor::Initで差し替えている）。
    // 「ひとつめ」の文字の位置と向きは、配置のファイルの place Stage1KeypadClueLabel1〜4 から読んでいる。数字の文字を返している
    FlashlightWriting* Stage1Builder::CreateDigitClue(int order)
    {
        const std::string number = std::to_string(order);
        const std::string labelName = "Stage1KeypadClueLabel" + number;
        const PropPlacement& placement = At(labelName.c_str());
        const Vector3 surfaceCenter = placement.Position;
        const Vector3 outwardNormal = PropLayoutFile::GetFacing(placement);
        FlashlightWriting* label = m_Builder.Create<FlashlightWriting>(labelName);
        label->SetTextures("assets/texture/writing_order" + number + ".png");
        label->Place(surfaceCenter, outwardNormal, DigitClueLabelWidth, DigitClueLabelHeight);

        // 文字を正面から見たときの右方向。
        const Vector3 right(-outwardNormal.z, 0.0f, outwardNormal.x);
        FlashlightWriting* digit = m_Builder.Create<FlashlightWriting>("Stage1KeypadClueDigit" + number);
        digit->Place(surfaceCenter + right * DigitClueDigitOffset, outwardNormal, DigitClueDigitSize, DigitClueDigitSize);
        Bind(labelName.c_str(), [label, digit](const PropPlacement& moved)
        {
            const Vector3 facing = PropLayoutFile::GetFacing(moved);
            label->Place(moved.Position, facing, DigitClueLabelWidth, DigitClueLabelHeight);
            digit->Place(moved.Position + Vector3(-facing.z, 0.0f, facing.x) * DigitClueDigitOffset, facing,
                DigitClueDigitSize, DigitClueDigitSize);
        });
        return digit;
    }

    // 最初は隠しておき、時間切れで破棄せずに隠す人影を作っている
    ShadowMan* Stage1Builder::CreateHiddenShadow(const char* name)
    {
        ShadowMan* shadow = m_Builder.Create<ShadowMan>(name);
        shadow->SetDeactivateOnExpire(true);
        shadow->SetActive(false);
        return shadow;
    }
}
