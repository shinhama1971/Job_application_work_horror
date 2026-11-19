// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ============================================================================

#include "Stage1Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CaptureMode.h"
#include "CeilingLight.h"
#include "LayoutExtras.h"
#include "Door.h"
#include "DrainValve.h"
#include "ExitTrigger.h"
#include "FlashlightWriting.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
#include "Item.h"
#include "KeyItem.h"
#include "PipeProp.h"
#include "PropLayoutFile.h"
#include "Player.h"
#include "ScareTrigger.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"
#include "StoryText.h"
#include "VoiceRecorder.h"
#include "Wall.h"
#include "utility.h"

#include <SimpleMath.h>
#include <array>
#include <iterator>
#include <functional>
#include <random>
#include <string>
#include <utility>

using namespace DirectX::SimpleMath;

namespace
{
    // --- 1面の配置の値
    // 開始地点の奥のドアと最後のドアの中心の高さ・高さ・厚み（どちらも同じ形）
    constexpr float StageDoorCenterY = -74.0f;
    constexpr float StageDoorHeight = 50.0f;
    constexpr float StageDoorThickness = 4.0f;
    // ドアの上の赤い表示：高さ、ドアの面から手前へ出す距離、大きさ（薄い・高さ・幅）、色、光沢
    constexpr float DoorSignY = -52.0f;
    constexpr float DoorSignOffset = 3.0f;
    constexpr float DoorSignThickness = 2.0f;
    constexpr float DoorSignHeight = 3.5f;
    constexpr float DoorSignWidth = 14.0f;
    const Color DoorSignDiffuse(0.24f, 0.025f, 0.018f, 1.0f);
    const Color DoorSignEmission(0.30f, 0.005f, 0.002f, 1.0f);
    constexpr float DoorSignShininess = 30.0f;
    // 開始地点の奥のドアの裏側の通路：長さ（移った直後に振り返っても、暗い廊下が続いて見える長さ）、壁の厚み、ドアの横からのゆとり
    constexpr float StartPassageLength = 100.0f;
    constexpr float StartPassageWallThickness = 4.0f;
    constexpr float StartPassageSideMargin = 2.0f;
    // 出口（最後のドアのすぐ先）の位置
    constexpr float ExitTriggerOffsetX = 4.0f;
    constexpr float ExitTriggerY = -80.0f;
    // 西棟の排水バルブの上の警告灯：赤い光、地の色はその割合、光沢、光の届く距離と強さ
    const Color ValveLampRed(0.90f, 0.08f, 0.05f, 1.0f);
    constexpr float ValveLampDiffuseRatio = 0.6f;
    constexpr float ValveLampShininess = 24.0f;
    constexpr float ValveLampGlowRange = 45.0f;
    constexpr float ValveLampGlowIntensity = 1.8f;
    // 光らない物の発光色
    const Color NoEmission(0.0f, 0.0f, 0.0f, 1.0f);
    // 扉の状態を示すランプ（停電中は赤）
    const Color DoorIndicatorDiffuse(0.26f, 0.025f, 0.018f, 1.0f);
    const Color DoorIndicatorEmission(0.30f, 0.005f, 0.002f, 1.0f);
    constexpr float DoorIndicatorShininess = 28.0f;
    // ループ廊下の周回の赤い目印
    const Color LoopMarkerDiffuse(0.22f, 0.012f, 0.008f, 1.0f);
    constexpr float LoopMarkerShininess = 20.0f;
    // 監視カメラの端末の上の、緑に光る目印
    const Color EvidenceMarkerDiffuse(0.025f, 0.11f, 0.09f, 1.0f);
    const Color EvidenceMarkerEmission(0.02f, 0.24f, 0.16f, 1.0f);
    constexpr float EvidenceMarkerShininess = 36.0f;
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
    // 曲がり角の暗がりを補う通常の器具の大きさ
    const Vector3 CornerFixtureScale = CeilingLight::StandardSize;

    // 天井の照明：名前、停電中も赤い非常灯として光るか、点滅の周期をずらす値（そろって点滅すると機械的に見えるため）
    struct CeilingLightSpec
    {
        const char* Name;
        bool Emergency;
        float FlickerOffset;
    };
    // 施設の8灯（StageObjects::CeilingLightAt の番号の順）
    constexpr CeilingLightSpec MainCeilingLights[] =
    {
        { "CeilingLight1", true, 0.0f },
        { "CeilingLight2", false, 0.8f },
        { "CeilingLight3", true, 1.7f },
        { "CeilingLight4", true, 2.4f },
        { "CeilingLight5", false, 3.1f },
        { "CeilingLight6", true, 3.8f },
        { "CeilingLight7", false, 4.5f },
        { "CeilingLight8", true, 6.2f },
    };
    // 南西の広間（停電中は1つだけ赤い非常灯がともり、電力が戻ると両方が点く）
    constexpr CeilingLightSpec SouthWestHallLights[] =
    {
        { "Stage1SouthWestHallLight1", false, 5.0f },
        { "Stage1SouthWestHallLight2", true, 5.7f },
    };
    // 西棟（停電中は入口側の1つだけが赤い非常灯。奥とポンプ室は懐中電灯とバルブのランプが頼り）
    constexpr CeilingLightSpec WestWingLights[] =
    {
        { "Stage1WestWingLight1", true, 2.9f },
        { "Stage1WestWingLight2", false, 3.4f },
        { "Stage1WestWingLight3", false, 4.1f },
    };
    constexpr std::size_t WestWingLightCount = std::size(WestWingLights);
    // CornerFixtures の並びに対応する。施設の8灯とは別の、部屋の入口に足した器具。
    constexpr std::size_t MainPowerCornerFixture = 3;
    // 曲がり角の器具（進む先の照明の誘導で使う）
    constexpr CeilingLightSpec CornerFixtures[] =
    {
        { "Stage1LeftEntranceLight", false, 7.3f },
        { "Stage1RightEntranceLight", false, 8.6f },
        { "Stage1WestEntranceLight", false, 9.7f },
        { "Stage1MainPowerLight", false, 10.9f },
    };

    // 1面の施設を作る手順をまとめている。置き場所の読み方（配置のファイル）と、読み直したときの置き直し方を、手順のあいだで共有している
    class Stage1Builder final
    {
    public:
        Stage1Builder(Core::Game& game, StageObjects& objects, const PropLayout& layout)
            : m_Builder(game, objects.objectNames), m_Objects(objects), m_Layout(layout),
            m_FuseRandom(Tools::CaptureMode::MakeRandomSeed(Tools::CaptureMode::RandomSeed::Stage1FusePlacement))
        {
        }

        // プレイヤー（開始地点は南の区画の中央）と、床・西棟の浸水・天井の雨漏り
        void BuildPlayerAndGround()
        {
            m_Objects.player = m_Builder.Create<Player>("Player");
            PropLayoutFile::ApplyPlacement(*m_Objects.player, At("Player"));

            // 床（建物全体を覆う1000x1000の濡れた床）
            Ground* ground = m_Builder.Create<Ground>("Ground");
            Place(*ground, "Ground");
            // 西棟は床一面が水に浸かっている（水たまりと同じ反射・波紋を、範囲全体に出している）。
            ground->SetFloodRegion(
                Vector2(Stage1WestWing::MinX, Stage1WestWing::MinZ),
                Vector2(Stage1WestWing::MaxX, Stage1WestWing::MaxZ));
            m_Objects.ground = ground;
            // 西棟の周りの天井の雨漏り（水滴が落ち続け、真下の水たまりが約2分かけて広がる）。
            // 位置と、広がりきったときの水たまりの大きさ（size の幅 = 直径）は配置ファイルの place Stage1Leak1〜3 に書いている。
            // 広がりきるまでの秒数は、雨漏りごとにずらしてここで決めている（同時に広がりきらないように）
            constexpr std::pair<const char*, float> leaks[] =
            {
                { "Stage1Leak1", 120.0f },
                { "Stage1Leak2", 140.0f },
                { "Stage1Leak3", 160.0f },
            };
            for (const auto& [leakName, growSeconds] : leaks)
            {
                const PropPlacement& leak = At(leakName);
                ground->AddLeak(Vector2(leak.Position.x, leak.Position.z), leak.Size.x * 0.5f, growSeconds);
            }
        }

        // 建物の壁と柱・天井の板・配管・幅木・棚・机・崩れた資材・木箱・色の付いた警告灯・暖かい電球など、進行のコードが使わない物（prop 行）
        void BuildLayoutProps()
        {
            m_Objects.layoutProps = PropLayoutFile::Spawn(m_Builder, m_Layout);
            // 部屋の角の暗がりの計算用に、room を付けた建物の壁を上から見た長方形を記録している
            m_Objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(m_Layout);
        }

        // 西棟の排水バルブ（南・西・東の壁に1つずつ）。上のランプは、回す前は赤く、回したら緑に光る（色はStage1WestWingが変える）。
        // バルブの配管は床から天井まで壁を伝い、ランプは配管の横に付けている。
        // バルブの位置と向き、ランプの位置と大きさは、配置のファイルの place 行から読んでいる。
        void BuildWestWingValves()
        {
            const auto createDrainValve = [this](const char* valveName, const char* lampName)
            {
                DrainValve* valve = m_Builder.Create<DrainValve>(valveName);
                Place(*valve, valveName);
                Wall* lamp = m_Builder.Create<Wall>(lampName);
                Place(*lamp, lampName);
                lamp->SetAppearance(Color(ValveLampRed.R() * ValveLampDiffuseRatio, ValveLampRed.G() * ValveLampDiffuseRatio,
                    ValveLampRed.B() * ValveLampDiffuseRatio, 1.0f), ValveLampRed, ValveLampShininess);
                lamp->SetCollisionEnabled(false);
                lamp->SetCastsShadow(false);
                lamp->SetGlowLight(ValveLampGlowRange, ValveLampGlowIntensity);
                // 見た目は、光るレンズを金属の枠と格子で囲んだ警告灯（Mayaで作ったモデル。色が変わるとレンズの色も変わる）
                lamp->SetVisualModel("warning_lamp");
                return std::pair<DrainValve*, Wall*>(valve, lamp);
            };
            const auto southValve = createDrainValve("Stage1WestWingValveSouth", "PropWestWingValveLampSouth");
            const auto westValve = createDrainValve("Stage1WestWingValveWest", "PropWestWingValveLampWest");
            const auto eastValve = createDrainValve("Stage1WestWingValveEast", "PropWestWingValveLampEast");
            m_Objects.westWing.valves = { southValve.first, westValve.first, eastValve.first };
            m_Objects.westWing.valveLamps = { southValve.second, westValve.second, eastValve.second };
        }

        // 光の安全地帯（暖かい電球の下）の範囲。電球そのものは小物のファイルに置いている（PropSafeLight...）。
        // 下にいると落ち着き、ライトを消して休むと電池が戻る（Stage1SafeLights）。
        // 範囲の中心と大きさ（size の幅 = 直径）は配置ファイルの place SafeZone_... に書いている。
        // 1つ目：中央ホールの西、配電盤と西棟の扉の間の裸電球（何度も通る場所の休み所）
        // 2つ目：南西の広間の机の上の電気スタンド（寄り道の先の休み所）
        void BuildSafeZones()
        {
            for (const char* zoneName : { "SafeZone_CentralHall", "SafeZone_SouthWestHall" })
            {
                const PropPlacement& zone = At(zoneName);
                m_Objects.safeZones.push_back({ zone.Position, zone.Size.x * 0.5f });
            }
        }

        // 扉の状態を示すランプ（停電中は赤。色は進行のコードが変えるので、小物のファイルではなくここで作っている）と、
        // ループ廊下で、周回によって現れる赤い目印（最初は隠している）
        void BuildDoorIndicatorAndLoopMarkers()
        {
            Wall* doorIndicator = CreatePlacedStageProp("PropDoorIndicator", DoorIndicatorDiffuse,
                DoorIndicatorEmission, DoorIndicatorShininess, false);
            // 見た目は、光る表示面を金属の枠で囲んだ表示灯（Mayaで作ったモデル。光るのは表示面だけ）
            doorIndicator->SetVisualModel("door_indicator");
            m_Objects.doorIndicator = doorIndicator;

            const auto createLoopMarker = [this](const char* name)
            {
                Wall* marker = CreatePlacedStageProp(name, LoopMarkerDiffuse, NoEmission, LoopMarkerShininess, false);
                marker->SetCastsShadow(false);
                marker->SetVisible(false);
                return marker;
            };
            m_Objects.loopMarkers = {
                createLoopMarker("PropLoopMarker1"),
                createLoopMarker("PropLoopMarker2"),
                createLoopMarker("PropLoopMarker3") };
        }

        // 施設の天井照明。照明の見た目で、施設に電気が来ているかを直接伝えている（非常灯は停電中も赤く光る）。
        void BuildMainCeilingLights()
        {
            static_assert(std::size(MainCeilingLights) == StageObjects::CeilingLightCount, "1面の天井照明の数をそろえる");
            for (std::size_t index = 0; index < std::size(MainCeilingLights); ++index)
            {
                m_Objects.ceilingLights[index] = CreateCeilingLight(MainCeilingLights[index]);
            }
        }

        // ヒューズ。どの部屋にあるかは固定し（目的表示と演出の流れを保つため）、部屋の中のどこに落ちているかを、プレイごとに候補からランダムに選んでいる。
        // 覚えた場所へ走るのではなく、毎回ライトで部屋を探させるためである。
        // 候補の場所は、配置のファイルの spot 行（1本目 Stage1FirstFuse：開始地点の区画の配管の陰・左奥・右奥、
        // 2本目 Stage1SecondFuse：左の倉庫。倉庫の演出（StageScene::UpdateStorageScare）が始まる x<-110, z<-108 の範囲の中だけ、
        // 3本目 Stage1ThirdFuse：西棟の一番奥のポンプ室。西棟の鍵を拾って扉を開け、排水バルブで水を抜くまで現れない）。
        // あわせて、西棟の鍵と扉も作っている
        void BuildFusesAndWestWingDoor()
        {
            Item* item1 = m_Builder.Create<Item>("Item1");
            PlaceAtRandomSpot(*item1, "Stage1FirstFuse");

            // 2本目と3本目は、進行に合わせて出すので最初は隠している
            Item* item2 = m_Builder.Create<Item>("Item2");
            PlaceAtRandomSpot(*item2, "Stage1SecondFuse");
            item2->SetActive(false);

            Item* item3 = m_Builder.Create<Item>("Item3");
            PlaceAtRandomSpot(*item3, "Stage1ThirdFuse");
            item3->SetActive(false);

            // 3本目のもう1つの手に入れ方：暗証番号の扉の先の部屋の奥（place Stage1KeypadRoomFuse）。3本目を探す段階まで隠している
            Item* keypadRoomFuse = m_Builder.Create<Item>("Item3Keypad");
            Place(*keypadRoomFuse, "Stage1KeypadRoomFuse");
            keypadRoomFuse->SetActive(false);
            m_Objects.keypadRoomFuse = keypadRoomFuse;

            // 西棟の鍵。右の倉庫（前は3本目のヒューズがあった部屋）のどこかに、2周目のループの後に現れる（spot Stage1WestWingKey）。
            KeyItem* westWingKey = m_Builder.Create<KeyItem>("Stage1WestWingKey");
            PlaceAtRandomSpot(*westWingKey, "Stage1WestWingKey");
            westWingKey->SetActive(false);

            // 西棟の扉（西側の外壁の z=-35〜-5）。鍵を拾うまで開かない。
            Door* westWingDoor = m_Builder.Create<Door>("Stage1WestWingDoor");
            Place(*westWingDoor, "Stage1WestWingDoor");
            westWingDoor->SetLocked(true);

            m_Objects.westWing.door = westWingDoor;
            m_Objects.westWing.key = westWingKey;
            m_Objects.westWing.fuse = item3;
            m_Objects.westWing.ground = m_Objects.ground;
            m_Objects.firstFuse = item1;
            m_Objects.secondFuse = item2;
            m_Objects.thirdFuse = item3;
        }

        // 寄り道のごほうびの電池（西棟の広間・崩れた資材置き場・南西の広間の奥の棚の一角の木箱の陰）と、
        // ボイスレコーダー（研究員たちの録音）。中央ホールの木箱の上・南西の広間の机・西棟の机・書類保管室の奥の机。
        // 4つ目（調査員の録音）は、1面の途中で届く無線が「予備のヒューズを置いてきた」と嘘をつく机に置いている（Stage1Transmissions）
        void BuildBatteriesAndRecorders()
        {
            for (const char* batteryName : { "Stage1WestWingBattery", "Stage1DebrisRoomBattery", "Stage1SouthWestHallBattery" })
            {
                BatteryItem* battery = m_Builder.Create<BatteryItem>(batteryName);
                Place(*battery, batteryName);
            }

            constexpr const char* recorderNames[] =
            {
                "Stage1RecorderHall", "Stage1RecorderSouthWestHall", "Stage1RecorderWestWing", "Stage1RecorderArchive"
            };
            static_assert(std::size(recorderNames) == StoryText::RecordingCount,
                "レコーダーの配置名と録音を同じ件数にする");
            for (int i = 0; i < StoryText::RecordingCount; ++i)
            {
                VoiceRecorder* recorder = m_Builder.Create<VoiceRecorder>(recorderNames[i]);
                recorder->SetRecordingIndex(i);
                Place(*recorder, recorderNames[i]);
                m_Objects.recorders[static_cast<std::size_t>(i)] = recorder;
            }
        }

        // 南西の広間と西棟の天井の照明
        void BuildSideRoomLights()
        {
            for (const CeilingLightSpec& spec : SouthWestHallLights)
            {
                CreateCeilingLight(spec);
            }
            for (std::size_t index = 0; index < WestWingLightCount; ++index)
            {
                m_WestWingLights[index] = CreateCeilingLight(WestWingLights[index]);
            }
        }

        // 中央の扉（ループ廊下へ続く）・主電源の配電盤・出口の送電盤・非常用充電器・監視カメラの端末と、その上の緑の目印。
        // 任意で探索するごほうびは、最短の道筋から外して置いている（素早く脱出するか、全部探索するかを選ばせるため）
        void BuildPowerControls()
        {
            Door* door = m_Builder.Create<Door>("Door");
            Place(*door, "Door");
            m_Objects.loopDoor = door;
            FuseBox* fuseBox = m_Builder.Create<FuseBox>("FuseBox");
            Place(*fuseBox, "FuseBox");

            // 出口の送電盤（主電源が戻った後に操作する）
            FuseBox* exitPowerPanel = m_Builder.Create<FuseBox>("ExitPowerPanel");
            exitPowerPanel->SetExitControl(true);
            Place(*exitPowerPanel, "ExitPowerPanel");
            m_Objects.exitPowerPanel = exitPowerPanel;

            // 非常用充電器（懐中電灯の電池を回復できる）
            FuseBox* emergencyCharger = m_Builder.Create<FuseBox>("Stage1EmergencyCharger");
            emergencyCharger->SetManualControl("非常用充電器を使う");
            emergencyCharger->SetManualInteractionAllowed(true);
            Place(*emergencyCharger, "Stage1EmergencyCharger");
            m_Objects.emergencyCharger = emergencyCharger;

            FuseBox* evidenceTerminal = m_Builder.Create<FuseBox>("Stage1EvidenceTerminal");
            evidenceTerminal->SetManualControl("監視カメラを確認する");
            evidenceTerminal->SetManualInteractionAllowed(true);
            Place(*evidenceTerminal, "Stage1EvidenceTerminal");
            m_Objects.evidenceTerminal = evidenceTerminal;
            Wall* evidenceMarker = CreatePlacedStageProp(
                "Stage1EvidenceMarker", EvidenceMarkerDiffuse, EvidenceMarkerEmission, EvidenceMarkerShininess, false);
            evidenceMarker->SetCastsShadow(false);
            m_Objects.evidenceMarker = evidenceMarker;
        }

        // 1面の最後のドア・開かずの扉・最後のドアの上の表示・開始地点の奥のドアとその裏の通路・通路側の表示
        void BuildEndAndStartDoors()
        {
            // 1面の最後のドア。ループ廊下の突き当たりをふさいでいる（蝶番は北側で、廊下側へ開く）。
            // 電力が戻る前は開けて通ると次の周回へ、出口の送電が終わった後は開けて通ると2面へ進む（StageSceneが判定）。
            // 見える扉を調べて通ったときだけ進むので、廊下を歩きながら操作キーを押しただけで反応することはない。
            Door* stageExitDoor = m_Builder.Create<Door>("Stage1ExitDoor");
            stageExitDoor->SetPosition(Stage1EndDoorX, StageDoorCenterY, Stage1EndDoorZ);
            stageExitDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
            stageExitDoor->SetScale(Vector3(Stage1EndDoorHalfWidth * 2.0f, StageDoorHeight, StageDoorThickness));
            m_Objects.exitDoor = stageExitDoor;

            // 監視カメラの異常用に、壁際へ開かずの扉を置いている。開いても先は壁。
            for (int index = 0; index < StageSealedDoorCount; ++index)
            {
                const StageSealedDoor& sealed = StageSealedDoors[index];
                Door* sealedDoor = m_Builder.Create<Door>(sealed.Name);
                sealedDoor->SetPosition(
                    sealed.Position[0], sealed.Position[1], sealed.Position[2]);
                sealedDoor->ResetClosed(0);
                sealedDoor->SetLocked(true);
                m_Objects.sealedDoors[static_cast<std::size_t>(index)] = sealedDoor;
            }

            // 最後のドアの上の赤い非常口の表示（廊下側の面）
            Wall* stageExitSign = CreateStageProp(
                "PropStage1ExitSign",
                Vector3(Stage1EndDoorX - DoorSignOffset, DoorSignY, Stage1EndDoorZ),
                Vector3(DoorSignThickness, DoorSignHeight, DoorSignWidth),
                DoorSignDiffuse, DoorSignEmission, DoorSignShininess, false);
            stageExitSign->SetCastsShadow(false);
            m_Objects.exitSign = stageExitSign;

            // 開始地点の奥の壁のドア（最後のドアと同じ形・同じ大きさ）。普段は鍵がかかっていて、開始地点の側からは開かない。
            // 最後のドアを90度回した向きに置き、蝶番も同じ側（通る向きの左）にしている。
            Door* startDoor = m_Builder.Create<Door>("Stage1StartDoor");
            startDoor->SetPosition(Stage1StartDoorX, StageDoorCenterY, Stage1StartDoorZ);
            startDoor->SetScale(Vector3(Stage1EndDoorHalfWidth * 2.0f, StageDoorHeight, StageDoorThickness));
            startDoor->SetLocked(true);
            m_Objects.startDoor = startDoor;

            // ドアの裏側の通路（建物の外へ張り出した、最後の廊下の突き当たりと同じ幅・高さの行き止まり）。
            const float passageCenterZ = Stage1StartDoorZ - StartPassageLength * 0.5f;
            const float passageWallX = Stage1EndDoorHalfWidth + StartPassageSideMargin;
            Wall* passageWest = m_Builder.Create<Wall>("Stage1StartPassageWest");
            passageWest->SetPosition(Stage1StartDoorX - passageWallX, StageDoorCenterY, passageCenterZ);
            passageWest->SetScale(StartPassageWallThickness, StageDoorHeight, StartPassageLength);
            Wall* passageEast = m_Builder.Create<Wall>("Stage1StartPassageEast");
            passageEast->SetPosition(Stage1StartDoorX + passageWallX, StageDoorCenterY, passageCenterZ);
            passageEast->SetScale(StartPassageWallThickness, StageDoorHeight, StartPassageLength);
            Wall* passageEnd = m_Builder.Create<Wall>("Stage1StartPassageEnd");
            passageEnd->SetPosition(Stage1StartDoorX, StageDoorCenterY,
                Stage1StartDoorZ - StartPassageLength - StartPassageWallThickness * 0.5f);
            passageEnd->SetScale(passageWallX * 2.0f + StartPassageWallThickness, StageDoorHeight, StartPassageWallThickness);
            for (Wall* passageWall : { passageWest, passageEast, passageEnd })
            {
                passageWall->SetWeatheringSurface(true);
            }

            // 最後のドアの上の表示と同じ物を、通路側の同じ位置に置いている（移った瞬間に景色が変わらないように）
            Wall* startDoorSign = CreateStageProp(
                "PropStage1StartDoorSign",
                Vector3(Stage1StartDoorX, DoorSignY, Stage1StartDoorZ - DoorSignOffset),
                Vector3(DoorSignWidth, DoorSignHeight, DoorSignThickness),
                DoorSignDiffuse, DoorSignEmission, DoorSignShininess, false);
            startDoorSign->SetCastsShadow(false);
            m_Objects.startDoorSign = startDoorSign;
        }

        // 懐中電灯で照らしたときだけ浮かぶ壁の文字。進む道筋に沿って、目的の手がかりと不安を置いている。
        void BuildWallWritings()
        {
            m_Objects.facilityMap = CreateWriting(
                "Stage1FacilityMap", "assets/texture/stage1_facility_map.png",
                "assets/texture/stage1_facility_map_loop.png");
            m_Objects.facilityMap->SetPrintedSurface();
            m_Objects.facilityMap->SetInkColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
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
        void BuildKeypadRoom()
        {
            Door* keypadDoor = m_Builder.Create<Door>("Stage1KeypadDoor");
            Place(*keypadDoor, "Stage1KeypadDoor");
            keypadDoor->SetLocked(true);

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
        void BuildEventsAndShadows()
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
        }

        // 曲がり角の器具と、配置ファイルの ExtraLight_・ExtraBattery_ で始まる照明と電池、進み具合ごとの進む先の照明の並び
        void BuildRouteLights()
        {
            // 曲がり角の暗がりを補う通常の器具。進む先の照明の誘導（下の routeLights）で使うので、名前の決まった place にしている。
            // 位置・向き・大きさは配置ファイルの place から読んでいる（Unity のマップエディターで動かせる）
            std::array<CeilingLight*, std::size(CornerFixtures)> corners{};
            for (std::size_t index = 0; index < corners.size(); ++index)
            {
                CeilingLight* light = m_Builder.Create<CeilingLight>(CornerFixtures[index].Name);
                light->SetScale(CornerFixtureScale.x, CornerFixtureScale.y, CornerFixtureScale.z);
                Place(*light, CornerFixtures[index].Name);
                light->SetEmergencyLight(CornerFixtures[index].Emergency, CornerFixtures[index].FlickerOffset);
                corners[index] = light;
            }
            CeilingLight* leftEntranceLight = corners[0];
            CeilingLight* rightEntranceLight = corners[1];
            CeilingLight* westEntranceLight = corners[2];
            CeilingLight* mainPowerLight = corners[MainPowerCornerFixture];

            // 通路と部屋の天井に、照明を一通り並べている。懐中電灯を消すとほとんど何も見えないため、
            // 足した照明はすべて非常回路につなぎ、停電中も赤い非常灯の光だまりが点々と続くようにしている（電力が戻ると普通の照明になる）。
            // 照明の間には暗がりが残る間隔にしている（P.T. やサイレントヒルのように、光だまりと闇が交互に続く）。
            // 名前で探さない物なので、Unity のマップエディターでコピーして足したり、消したりできる
            LayoutExtras::Spawn(m_Builder, m_Layout, m_Objects.placementBindings);

            // 進み具合ごとに、進む先の照明を手前から奥の順に並べている。色や明るさは変えず、そのうち1灯だけをちかちかさせる。
            const auto light = [this](int number) { return m_Objects.CeilingLightAt(number); };
            const auto route = [this](Stage1RouteLight which) -> std::vector<CeilingLight*>&
            {
                return m_Objects.routeLights[static_cast<std::size_t>(which)];
            };
            CeilingLight* westWingLight1 = m_WestWingLights[0];
            CeilingLight* westWingLight2 = m_WestWingLights[1];
            CeilingLight* westWingLight3 = m_WestWingLights[2];
            route(Stage1RouteLight::StartSearch) = { light(Stage1LightNumber::StartRoom) };
            route(Stage1RouteLight::HallEntrance) = { light(Stage1LightNumber::MiddleCorridor) };
            route(Stage1RouteLight::CorridorDoor) = { light(Stage1LightNumber::MiddleCorridor), light(Stage1LightNumber::CentralHall), light(Stage1LightNumber::NorthCorner), light(Stage1LightNumber::LoopExitCorridor) };
            route(Stage1RouteLight::LeftStorage) = { leftEntranceLight, light(Stage1LightNumber::LeftStorage) };
            route(Stage1RouteLight::RightStorage) = { rightEntranceLight, light(Stage1LightNumber::RightStorage) };
            route(Stage1RouteLight::WestWing) = { westEntranceLight, westWingLight1 };
            route(Stage1RouteLight::PumpRoom) = { westWingLight2, westWingLight3 };
            route(Stage1RouteLight::MainPower) = { westWingLight2, westWingLight1, mainPowerLight };
            route(Stage1RouteLight::ExitPower) = { light(Stage1LightNumber::MiddleCorridor), light(Stage1LightNumber::CentralHall), light(Stage1LightNumber::NorthCorner), light(Stage1LightNumber::LoopExitCorridor) };
            route(Stage1RouteLight::ExitDoor) = { light(Stage1LightNumber::LoopExitCorridor) };
            // 暗証番号の扉（x=45・z=110）は、中央の廊下の CeilingLight5 のすぐ東
            route(Stage1RouteLight::KeypadRoom) = { light(Stage1LightNumber::MiddleCorridor), light(Stage1LightNumber::CentralHall) };
        }

    private:
        SceneLayoutBuilder m_Builder;
        StageObjects& m_Objects;
        const PropLayout& m_Layout;
        std::mt19937 m_FuseRandom;
        std::array<CeilingLight*, WestWingLightCount> m_WestWingLights{};

        const PropPlacement& At(const char* name) const
        {
            return PropLayoutFile::GetPlacement(m_Layout, name);
        }

        // 置き場所を当てはめ、配置ファイルを読み直したとき（Debug版の F5 と、Unity などで書き出した直後の自動の読み直し）に
        // 同じ物を置き直せるよう、置き直し方を名前と一緒に覚えている
        template<typename T>
        void Place(T& object, const char* name)
        {
            PropLayoutFile::ApplyPlacement(object, At(name));
            T* target = &object;
            m_Objects.placementBindings.emplace_back(name, [target](const PropPlacement& placement)
            {
                PropLayoutFile::ApplyPlacement(*target, placement);
            });
        }

        // 置き場所から別の形で置く物（壁の文字・ロッカー・裸電球など）の置き直し方を覚えている
        void Bind(const char* name, std::function<void(const PropPlacement&)> apply)
        {
            m_Objects.placementBindings.emplace_back(name, std::move(apply));
        }

        // 配置のファイルの spot 行の候補から、プレイごとにランダムに1つ選んで置いている
        template<typename T>
        void PlaceAtRandomSpot(T& object, const char* group)
        {
            const std::vector<Vector3>& candidates = PropLayoutFile::GetSpots(m_Layout, group);
            std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
            PropLayoutFile::MoveTo(object, candidates[pick(m_FuseRandom)]);
        }

        // Wallを、色と当たり判定の有無を指定した小物として作る（進行のコードが色や表示を変える物だけに使っている。
        // それ以外の小物は、小物のファイルから読んでいる）
        Wall* CreateStageProp(const char* name, const Vector3& position, const Vector3& scale,
            const Color& diffuse, const Color& emission, float shininess, bool collisionEnabled)
        {
            Wall* prop = m_Builder.Create<Wall>(name);
            prop->SetPosition(position.x, position.y, position.z);
            prop->SetScale(scale.x, scale.y, scale.z);
            prop->SetAppearance(diffuse, emission, shininess);
            prop->SetCollisionEnabled(collisionEnabled);
            return prop;
        }

        // 置き場所（位置と大きさ）を配置のファイルの place 行から読んで、小物を作る
        Wall* CreatePlacedStageProp(const char* name, const Color& diffuse, const Color& emission,
            float shininess, bool collisionEnabled)
        {
            const PropPlacement& placement = At(name);
            Wall* wall = CreateStageProp(name, placement.Position, placement.Size, diffuse, emission, shininess, collisionEnabled);
            Bind(name, [wall](const PropPlacement& moved)
            {
                wall->SetPosition(moved.Position.x, moved.Position.y, moved.Position.z);
                wall->SetScale(moved.Size.x, moved.Size.y, moved.Size.z);
                wall->SetVisualModel(wall->GetVisualModel());
            });
            return wall;
        }

        // 天井の照明を作り、置き場所に置いて、非常灯にするかと点滅の周期のずらしを決めている
        CeilingLight* CreateCeilingLight(const CeilingLightSpec& spec)
        {
            CeilingLight* light = m_Builder.Create<CeilingLight>(spec.Name);
            Place(*light, spec.Name);
            light->SetEmergencyLight(spec.Emergency, spec.FlickerOffset);
            return light;
        }

        // 懐中電灯で照らしたときだけ浮かぶ壁の文字を作っている。
        // 位置は壁の表面、向き（yaw）は壁から通路側へ向かう法線、幅と高さは size の1つ目と2つ目（配置のファイルの place 行）。画像は横長(4:1)。
        FlashlightWriting* CreateWriting(const char* name, const char* texture, const char* alternateTexture)
        {
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
        FlashlightWriting* CreateDigitClue(int order)
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
        ShadowMan* CreateHiddenShadow(const char* name)
        {
            ShadowMan* shadow = m_Builder.Create<ShadowMan>(name);
            shadow->SetDeactivateOnExpire(true);
            shadow->SetActive(false);
            return shadow;
        }
    };
}

namespace Stage1Layout
{
    // 1面の施設をすべて作り、進行で使うObjectのポインタを入れて返している。座標の単位は全ステージ共通（床の高さはy=-100）。
    // 作る順番は前と同じにしている（描く順番と、置き直しの登録の順番が変わらないように）
    StageObjects Build(Core::Game& game)
    {
        StageObjects objects;

        // 建物の壁と柱・小物（prop 行）と、扉・照明・電池・端末などの置き場所（place 行）は、
        // テキストファイル（assets/layout/stage1_props.txt）から読んでいる（PropLayoutFile）。
        // ビルドし直さずに配置を直せ、Debug版ではゲームを動かしたまま F5 で小物を読み直せる（StageScene）。
        const PropLayout propLayout = PropLayoutFile::Load(PropLayoutPath);
        if (!propLayout.IsValid())
        {
            utility::ReportFatalError("1面の配置を読めませんでした（" + std::to_string(propLayout.ErrorLine) +
                "行目）: " + propLayout.Error);
        }

        Stage1Builder builder(game, objects, propLayout);
        builder.BuildPlayerAndGround();
        builder.BuildLayoutProps();
        builder.BuildWestWingValves();
        builder.BuildSafeZones();
        builder.BuildDoorIndicatorAndLoopMarkers();
        builder.BuildMainCeilingLights();
        builder.BuildFusesAndWestWingDoor();
        builder.BuildBatteriesAndRecorders();
        builder.BuildSideRoomLights();
        builder.BuildPowerControls();
        builder.BuildEndAndStartDoors();
        builder.BuildWallWritings();
        builder.BuildKeypadRoom();
        builder.BuildEventsAndShadows();
        builder.BuildRouteLights();
        return objects;
    }
}
