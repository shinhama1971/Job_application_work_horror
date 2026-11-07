// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ============================================================================

#include "Stage1Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
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

namespace Stage1Layout
{
    // 1面の施設をすべて作り、進行で使うObjectのポインタを入れて返している。座標の単位は全ステージ共通（床の高さはy=-100）
    StageObjects Build(Core::Game& game)
    {
        StageObjects objects;
        SceneLayoutBuilder builder(game, objects.objectNames);

        // 建物の壁と柱・小物（prop 行）と、下で作る扉・照明・電池・端末などの置き場所（place 行）は、
        // テキストファイル（assets/layout/stage1_props.txt）から読んでいる（PropLayoutFile）。
        // ビルドし直さずに配置を直せ、Debug版ではゲームを動かしたまま F5 で小物を読み直せる（StageScene）。
        const PropLayout propLayout = PropLayoutFile::Load(PropLayoutPath);
        if (!propLayout.IsValid())
        {
            utility::ReportFatalError("1面の配置を読めませんでした（" + std::to_string(propLayout.ErrorLine) +
                "行目）: " + propLayout.Error);
        }
        const auto at = [&propLayout](const char* name) -> const PropPlacement&
        {
            return PropLayoutFile::GetPlacement(propLayout, name);
        };
        // 置き場所を当てはめ、配置ファイルを読み直したとき（Debug版の F5 と、Unity などで書き出した直後の自動の読み直し）に
        // 同じ物を置き直せるよう、置き直し方を名前と一緒に覚えている
        const auto place = [&at, &objects](auto& object, const char* name)
        {
            PropLayoutFile::ApplyPlacement(object, at(name));
            auto* target = &object;
            objects.placementBindings.emplace_back(name, [target](const PropPlacement& placement)
            {
                PropLayoutFile::ApplyPlacement(*target, placement);
            });
        };
        // 置き場所から別の形で置く物（壁の文字・ロッカー・裸電球など）の置き直し方を覚えている
        const auto bind = [&objects](const char* name, std::function<void(const PropPlacement&)> apply)
        {
            objects.placementBindings.emplace_back(name, std::move(apply));
        };

        // プレイヤー（開始地点は南の区画の中央）
        Player* player = builder.Create<Player>("Player");
        PropLayoutFile::ApplyPlacement(*player, at("Player"));
       
        // 床（建物全体を覆う1000x1000の濡れた床）
        Ground* ground = builder.Create<Ground>("Ground");
        place(*ground, "Ground");
        // 西棟は床一面が水に浸かっている（水たまりと同じ反射・波紋を、範囲全体に出している）。
        ground->SetFloodRegion(
            Vector2(Stage1WestWing::MinX, Stage1WestWing::MinZ),
            Vector2(Stage1WestWing::MaxX, Stage1WestWing::MaxZ));
        objects.ground = ground;
        // 西棟の周りの天井の雨漏り（水滴が落ち続け、真下の水たまりが約2分かけて広がる）
        ground->AddLeak(Vector2(-195.0f, -25.0f), 6.0f, 120.0f);
        ground->AddLeak(Vector2(-150.0f, -45.0f), 5.0f, 140.0f);
        ground->AddLeak(Vector2(-100.0f, -95.0f), 5.0f, 160.0f);

        // 建物の壁（外周・部屋の仕切り・ループ廊下・西棟・南西の広間）と柱は、下の小物のファイルに書いている（room・weathering）


        // Wallを、色と当たり判定の有無を指定した小物として作る関数（進行のコードが色や表示を変える物だけに使っている。
        // それ以外の小物は、下で小物のファイルから読んでいる）
        const auto createStageProp = [&builder](
            const char* name,
            const Vector3& position,
            const Vector3& scale,
            const Color& diffuse,
            const Color& emission,
            float shininess,
            bool collisionEnabled)
        {
            Wall* prop = builder.Create<Wall>(name);
            prop->SetPosition(position.x, position.y, position.z);
            prop->SetScale(scale.x, scale.y, scale.z);
            prop->SetAppearance(diffuse, emission, shininess);
            prop->SetCollisionEnabled(collisionEnabled);
            return prop;
        };

        const Color noEmission(0.0f, 0.0f, 0.0f, 1.0f);
        // 置き場所（位置と大きさ）を配置のファイルの place 行から読んで、小物を作る関数
        const auto createPlacedStageProp = [&createStageProp, &at, &bind](
            const char* name,
            const Color& diffuse,
            const Color& emission,
            float shininess,
            bool collisionEnabled)
        {
            const PropPlacement& placement = at(name);
            Wall* wall = createStageProp(name, placement.Position, placement.Size, diffuse, emission, shininess, collisionEnabled);
            bind(name, [wall](const PropPlacement& moved)
            {
                wall->SetPosition(moved.Position.x, moved.Position.y, moved.Position.z);
                wall->SetScale(moved.Size.x, moved.Size.y, moved.Size.z);
                wall->SetVisualModel(wall->GetVisualModel());
            });
            return wall;
        };

        // 建物の壁と柱・天井の板・配管・幅木・棚・机・崩れた資材・木箱・色の付いた警告灯・暖かい電球など、進行のコードが使わない物（prop 行）
        objects.layoutProps = PropLayoutFile::Spawn(builder, propLayout);
        // 部屋の角の暗がりの計算用に、room を付けた建物の壁を上から見た長方形を記録している
        objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(propLayout);

        // 西棟の排水バルブ（南・西・東の壁に1つずつ）。上のランプは、回す前は赤く、回したら緑に光る（色はStage1WestWingが変える）。
        // バルブの配管は床から天井まで壁を伝い、ランプは配管の横に付けている。
        // バルブの位置と向き、ランプの位置と大きさは、配置のファイルの place 行から読んでいる。
        const auto createDrainValve = [&builder, &place](const char* valveName, const char* lampName)
        {
            DrainValve* valve = builder.Create<DrainValve>(valveName);
            place(*valve, valveName);
            const Color red(0.90f, 0.08f, 0.05f, 1.0f);
            Wall* lamp = builder.Create<Wall>(lampName);
            place(*lamp, lampName);
            lamp->SetAppearance(Color(red.R() * 0.6f, red.G() * 0.6f, red.B() * 0.6f, 1.0f), red, 24.0f);
            lamp->SetCollisionEnabled(false);
            lamp->SetCastsShadow(false);
            lamp->SetGlowLight(45.0f, 1.8f);
            // 見た目は、光るレンズを金属の枠と格子で囲んだ警告灯（Mayaで作ったモデル。色が変わるとレンズの色も変わる）
            lamp->SetVisualModel("warning_lamp");
            return std::pair<DrainValve*, Wall*>(valve, lamp);
        };
        const auto southValve = createDrainValve("Stage1WestWingValveSouth", "PropWestWingValveLampSouth");
        const auto westValve = createDrainValve("Stage1WestWingValveWest", "PropWestWingValveLampWest");
        const auto eastValve = createDrainValve("Stage1WestWingValveEast", "PropWestWingValveLampEast");
        objects.westWing.valves = { southValve.first, westValve.first, eastValve.first };
        objects.westWing.valveLamps = { southValve.second, westValve.second, eastValve.second };

        // 光の安全地帯（暖かい電球の下）の範囲。電球そのものは小物のファイルに置いている（PropSafeLight...）。
        // 下にいると落ち着き、ライトを消して休むと電池が戻る（Stage1SafeLights）。
        // 1つ目：中央ホールの西、配電盤と西棟の扉の間の裸電球（何度も通る場所の休み所）
        objects.safeZones.push_back({ Vector3(-198.0f, -99.0f, 12.0f), 30.0f });
        // 2つ目：南西の広間の机の上の電気スタンド（寄り道の先の休み所）
        objects.safeZones.push_back({ Vector3(-322.0f, -99.0f, -292.0f), 34.0f });

        // 扉の状態を示すランプ（停電中は赤）。色は進行のコードが変えるので、小物のファイルではなくここで作っている
        Wall* doorIndicator = createPlacedStageProp("PropDoorIndicator", Color(0.26f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f), 28.0f, false);
        // 見た目は、光る表示面を金属の枠で囲んだ表示灯（Mayaで作ったモデル。光るのは表示面だけ）
        doorIndicator->SetVisualModel("door_indicator");

        // ループ廊下で、周回によって現れる赤い目印（最初は隠している）
        const auto createLoopMarker = [&createPlacedStageProp, &noEmission](const char* name)
        {
            Wall* marker = createPlacedStageProp(name,
                Color(0.22f, 0.012f, 0.008f, 1.0f), noEmission, 20.0f, false);
            marker->SetCastsShadow(false);
            marker->SetVisible(false);
            return marker;
        };

        objects.loopMarkers = {
            createLoopMarker("PropLoopMarker1"),
            createLoopMarker("PropLoopMarker2"),
            createLoopMarker("PropLoopMarker3") };
        // 天井照明の見た目で、施設に電気が来ているかを直接伝えている（非常灯は停電中も赤く光る）。
        CeilingLight* light1 = builder.Create<CeilingLight>("CeilingLight1");
        place(*light1, "CeilingLight1");
        light1->SetEmergencyLight(true, 0.0f);

        CeilingLight* light2 = builder.Create<CeilingLight>("CeilingLight2");
        place(*light2, "CeilingLight2");
        light2->SetEmergencyLight(false, 0.8f);

        CeilingLight* light3 = builder.Create<CeilingLight>("CeilingLight3");
        place(*light3, "CeilingLight3");
        light3->SetEmergencyLight(true, 1.7f);

        CeilingLight* light4 = builder.Create<CeilingLight>("CeilingLight4");
        place(*light4, "CeilingLight4");
        light4->SetEmergencyLight(true, 2.4f);

        CeilingLight* light5 = builder.Create<CeilingLight>("CeilingLight5");
        place(*light5, "CeilingLight5");
        light5->SetEmergencyLight(false, 3.1f);

        CeilingLight* light6 = builder.Create<CeilingLight>("CeilingLight6");
        place(*light6, "CeilingLight6");
        light6->SetEmergencyLight(true, 3.8f);

        CeilingLight* light7 = builder.Create<CeilingLight>("CeilingLight7");
        place(*light7, "CeilingLight7");
        light7->SetEmergencyLight(false, 4.5f);

        CeilingLight* light8 = builder.Create<CeilingLight>("CeilingLight8");
        place(*light8, "CeilingLight8");
        light8->SetEmergencyLight(true, 6.2f);

        // ヒューズ。どの部屋にあるかは固定し（目的表示と演出の流れを保つため）、
        // 部屋の中のどこに落ちているかを、プレイごとに候補からランダムに選んでいる。
        // 覚えた場所へ走るのではなく、毎回ライトで部屋を探させるためである。
        std::mt19937 fuseRandom{ std::random_device{}() };
        // 候補の場所は、配置のファイルの spot 行（1本目 Stage1FirstFuse：開始地点の区画の配管の陰・左奥・右奥、
        // 2本目 Stage1SecondFuse：左の倉庫。倉庫の演出（StageScene::UpdateStorageScare）が始まる x<-110, z<-108 の範囲の中だけ、
        // 3本目 Stage1ThirdFuse：西棟の一番奥のポンプ室。西棟の鍵を拾って扉を開け、排水バルブで水を抜くまで現れない）
        const auto placeAtRandomSpot = [&fuseRandom, &propLayout](auto& object, const char* group)
        {
            const std::vector<Vector3>& candidates = PropLayoutFile::GetSpots(propLayout, group);
            std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
            PropLayoutFile::MoveTo(object, candidates[pick(fuseRandom)]);
        };

        Item* item1 = builder.Create<Item>("Item1");
        placeAtRandomSpot(*item1, "Stage1FirstFuse");

        // 2本目と3本目は、進行に合わせて出すので最初は隠している
        Item* item2 = builder.Create<Item>("Item2");
        placeAtRandomSpot(*item2, "Stage1SecondFuse");
        item2->SetActive(false);

        Item* item3 = builder.Create<Item>("Item3");
        placeAtRandomSpot(*item3, "Stage1ThirdFuse");
        item3->SetActive(false);

        // 3本目のもう1つの手に入れ方：暗証番号の扉の先の部屋の奥（place Stage1KeypadRoomFuse）。3本目を探す段階まで隠している
        Item* keypadRoomFuse = builder.Create<Item>("Item3Keypad");
        place(*keypadRoomFuse, "Stage1KeypadRoomFuse");
        keypadRoomFuse->SetActive(false);
        objects.keypadRoomFuse = keypadRoomFuse;

        // 西棟の鍵。右の倉庫（前は3本目のヒューズがあった部屋）のどこかに、2周目のループの後に現れる（spot Stage1WestWingKey）。
        KeyItem* westWingKey = builder.Create<KeyItem>("Stage1WestWingKey");
        placeAtRandomSpot(*westWingKey, "Stage1WestWingKey");
        westWingKey->SetActive(false);

        // 西棟の扉（西側の外壁の z=-35〜-5）。鍵を拾うまで開かない。
        Door* westWingDoor = builder.Create<Door>("Stage1WestWingDoor");
        place(*westWingDoor, "Stage1WestWingDoor");
        westWingDoor->SetLocked(true);

        objects.westWing.door = westWingDoor;
        objects.westWing.key = westWingKey;
        objects.westWing.fuse = item3;
        objects.westWing.ground = ground;

        // 寄り道のごほうびの電池（西棟の広間・崩れた資材置き場）。
        BatteryItem* westWingBattery = builder.Create<BatteryItem>("Stage1WestWingBattery");
        place(*westWingBattery, "Stage1WestWingBattery");
        BatteryItem* debrisBattery = builder.Create<BatteryItem>("Stage1DebrisRoomBattery");
        place(*debrisBattery, "Stage1DebrisRoomBattery");
        // 南西の広間の奥（棚の一角の木箱の陰）にも、寄り道のごほうびの電池を置いている。
        BatteryItem* southWestHallBattery = builder.Create<BatteryItem>("Stage1SouthWestHallBattery");
        place(*southWestHallBattery, "Stage1SouthWestHallBattery");

        // 南西の広間の天井の照明（停電中は1つだけ赤い非常灯がともり、電力が戻ると両方が点く）
        CeilingLight* southWestHallLight1 = builder.Create<CeilingLight>("Stage1SouthWestHallLight1");
        place(*southWestHallLight1, "Stage1SouthWestHallLight1");
        southWestHallLight1->SetEmergencyLight(false, 5.0f);
        CeilingLight* southWestHallLight2 = builder.Create<CeilingLight>("Stage1SouthWestHallLight2");
        place(*southWestHallLight2, "Stage1SouthWestHallLight2");
        southWestHallLight2->SetEmergencyLight(true, 5.7f);

        // 西棟の天井の照明（停電中は入口側の1つだけが赤い非常灯。奥とポンプ室は懐中電灯とバルブのランプが頼り）
        CeilingLight* westWingLight1 = builder.Create<CeilingLight>("Stage1WestWingLight1");
        place(*westWingLight1, "Stage1WestWingLight1");
        westWingLight1->SetEmergencyLight(true, 2.9f);
        CeilingLight* westWingLight2 = builder.Create<CeilingLight>("Stage1WestWingLight2");
        place(*westWingLight2, "Stage1WestWingLight2");
        westWingLight2->SetEmergencyLight(false, 3.4f);
        CeilingLight* westWingLight3 = builder.Create<CeilingLight>("Stage1WestWingLight3");
        place(*westWingLight3, "Stage1WestWingLight3");
        westWingLight3->SetEmergencyLight(false, 4.1f);

        // 中央の扉（ループ廊下へ続く）と、主電源の配電盤
        Door* door = builder.Create<Door>("Door");
        place(*door, "Door");
        FuseBox* fuseBox = builder.Create<FuseBox>("FuseBox");
        place(*fuseBox, "FuseBox");

        // 出口の送電盤（主電源が戻った後に操作する）
        FuseBox* exitPowerPanel =
            builder.Create<FuseBox>("ExitPowerPanel");
        exitPowerPanel->SetExitControl(true);
        place(*exitPowerPanel, "ExitPowerPanel");

        // 非常用充電器（懐中電灯の電池を回復できる）
        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage1EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        place(*emergencyCharger, "Stage1EmergencyCharger");

        // 任意で探索するごほうびは、最短の道筋から外して置いている。
        // 素早く脱出するか、全部探索するかを選ばせるためである。
        FuseBox* evidenceTerminal =
            builder.Create<FuseBox>("Stage1EvidenceTerminal");
        evidenceTerminal->SetManualControl("監視カメラを確認する");
        evidenceTerminal->SetManualInteractionAllowed(true);
        place(*evidenceTerminal, "Stage1EvidenceTerminal");
        // 監視カメラの端末の上に付けた、緑に光る目印
        Wall* evidenceMarker = createPlacedStageProp(
            "Stage1EvidenceMarker",
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.02f, 0.24f, 0.16f, 1.0f),
            36.0f,
            false);
        evidenceMarker->SetCastsShadow(false);


        // 1面の最後のドア。ループ廊下の突き当たりをふさいでいる（蝶番は北側で、廊下側へ開く）。
        // 電力が戻る前は開けて通ると次の周回へ、出口の送電が終わった後は開けて通ると2面へ進む（StageSceneが判定）。
        // 見える扉を調べて通ったときだけ進むので、廊下を歩きながら操作キーを押しただけで反応することはない。
        Door* stageExitDoor = builder.Create<Door>("Stage1ExitDoor");
        stageExitDoor->SetPosition(Stage1EndDoorX, -74.0f, Stage1EndDoorZ);
        stageExitDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        stageExitDoor->SetScale(Vector3(Stage1EndDoorHalfWidth * 2.0f, 50.0f, 4.0f));

        // 監視カメラの異常用に、壁際へ開かずの扉を置いている。開いても先は壁。
        for (int index = 0; index < StageSealedDoorCount; ++index)
        {
            const StageSealedDoor& sealed = StageSealedDoors[index];
            Door* sealedDoor = builder.Create<Door>(sealed.Name);
            sealedDoor->SetPosition(
                sealed.Position[0], sealed.Position[1], sealed.Position[2]);
            sealedDoor->ResetClosed(0);
            sealedDoor->SetLocked(true);
            objects.sealedDoors[static_cast<std::size_t>(index)] = sealedDoor;
        }

        // 最後のドアの上の赤い非常口の表示（廊下側の面）
        Wall* stageExitSign = createStageProp(
            "PropStage1ExitSign",
            Vector3(Stage1EndDoorX - 3.0f, -52.0f, Stage1EndDoorZ),
            Vector3(2.0f, 3.5f, 14.0f),
            Color(0.24f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f),
            30.0f,
            false);
        stageExitSign->SetCastsShadow(false);

        // 開始地点の奥の壁のドア（最後のドアと同じ形・同じ大きさ）。普段は鍵がかかっていて、開始地点の側からは開かない。
        // 最後のドアを90度回した向きに置き、蝶番も同じ側（通る向きの左）にしている。
        Door* startDoor = builder.Create<Door>("Stage1StartDoor");
        startDoor->SetPosition(Stage1StartDoorX, -74.0f, Stage1StartDoorZ);
        startDoor->SetScale(Vector3(Stage1EndDoorHalfWidth * 2.0f, 50.0f, 4.0f));
        startDoor->SetLocked(true);

        // ドアの裏側の通路（建物の外へ張り出した、最後の廊下の突き当たりと同じ幅・高さの行き止まり）。
        // 移った直後に振り返っても、暗い廊下が続いているように見える長さ（調べられる距離より長く）にしている。
        constexpr float passageLength = 100.0f;
        const float passageCenterZ = Stage1StartDoorZ - passageLength * 0.5f;
        const float passageWallX = Stage1EndDoorHalfWidth + 2.0f;
        Wall* passageWest = builder.Create<Wall>("Stage1StartPassageWest");
        passageWest->SetPosition(Stage1StartDoorX - passageWallX, -74.0f, passageCenterZ);
        passageWest->SetScale(4.0f, 50.0f, passageLength);
        Wall* passageEast = builder.Create<Wall>("Stage1StartPassageEast");
        passageEast->SetPosition(Stage1StartDoorX + passageWallX, -74.0f, passageCenterZ);
        passageEast->SetScale(4.0f, 50.0f, passageLength);
        Wall* passageEnd = builder.Create<Wall>("Stage1StartPassageEnd");
        passageEnd->SetPosition(Stage1StartDoorX, -74.0f, Stage1StartDoorZ - passageLength - 2.0f);
        passageEnd->SetScale(passageWallX * 2.0f + 4.0f, 50.0f, 4.0f);
        for (Wall* passageWall : { passageWest, passageEast, passageEnd })
        {
            passageWall->SetWeatheringSurface(true);
        }

        // 最後のドアの上の表示と同じ物を、通路側の同じ位置に置いている（移った瞬間に景色が変わらないように）
        Wall* startDoorSign = createStageProp(
            "PropStage1StartDoorSign",
            Vector3(Stage1StartDoorX, -52.0f, Stage1StartDoorZ - 3.0f),
            Vector3(14.0f, 3.5f, 2.0f),
            Color(0.24f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f),
            30.0f,
            false);
        startDoorSign->SetCastsShadow(false);

        // 懐中電灯で照らしたときだけ浮かぶ壁の文字。進む道筋に沿って、目的の手がかりと不安を置いている。
        // 位置は壁の表面、向き（yaw）は壁から通路側へ向かう法線、幅と高さは size の1つ目と2つ目（配置のファイルの place 行）。画像は横長(4:1)。
        const auto createWriting = [&builder, &at, &bind](
            const char* name,
            const char* texture,
            const char* alternateTexture)
        {
            const PropPlacement& placement = at(name);
            FlashlightWriting* writing = builder.Create<FlashlightWriting>(name);
            writing->SetTextures(texture, alternateTexture);
            writing->Place(placement.Position, PropLayoutFile::GetFacing(placement), placement.Size.x, placement.Size.y);
            bind(name, [writing](const PropPlacement& moved)
            {
                writing->Place(moved.Position, PropLayoutFile::GetFacing(moved), moved.Size.x, moved.Size.y);
            });
            return writing;
        };
        objects.writings[Stage1WallWritings::Warning] = createWriting(
            "Stage1WritingWarning", "assets/texture/writing_warning.png", "");
        objects.writings[Stage1WallWritings::Three] = createWriting(
            "Stage1WritingThree", "assets/texture/writing_three.png", "");
        objects.writings[Stage1WallWritings::Power] = createWriting(
            "Stage1WritingPower", "assets/texture/writing_power.png", "");
        // ループ廊下を北へ進むと正面に見える突き当たりの壁。右へ曲がって先へ進み、文字が背中側になると書き換わる
        // （横を見ただけでは変わらない。振り返って見ると「ふりかえったな」になっている）。
        objects.writings[Stage1WallWritings::Turn] = createWriting(
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
        for (std::size_t lap = 0; lap < objects.lapWritings.size(); ++lap)
        {
            FlashlightWriting* writing = createWriting(
                lapWritingSpots[lap].Name, lapWritingSpots[lap].Texture, "");
            writing->SetPresenceImmediately(0.0f);
            objects.lapWritings[lap] = writing;
        }

        // 暗証番号の扉（任意で探索）。中央の扉の先の細い廊下の東の壁にあり、ループ廊下へ向かうたびに目に入る。
        Door* keypadDoor = builder.Create<Door>("Stage1KeypadDoor");
        place(*keypadDoor, "Stage1KeypadDoor");
        keypadDoor->SetLocked(true);

        FuseBox* keypadPanel = builder.Create<FuseBox>("Stage1KeypadPanel");
        keypadPanel->SetManualControl("暗証番号を入力する");
        keypadPanel->SetManualInteractionAllowed(true);
        place(*keypadPanel, "Stage1KeypadPanel");

        // 番号の手がかり。「ひとつめ」などの文字の右に、その桁の数字を書いている。
        // 数字の画像は、番号を決めるStage1KeypadDoor::Initで差し替えている。
        // 「ひとつめ」の文字の位置と向きは、配置のファイルの place Stage1KeypadClueLabel1〜4 から読んでいる。
        const auto createDigitClue = [&builder, &at, &bind](int order)
        {
            const std::string number = std::to_string(order);
            const PropPlacement& placement = at(("Stage1KeypadClueLabel" + number).c_str());
            const Vector3 surfaceCenter = placement.Position;
            const Vector3 outwardNormal = PropLayoutFile::GetFacing(placement);
            FlashlightWriting* label = builder.Create<FlashlightWriting>(
                "Stage1KeypadClueLabel" + number);
            label->SetTextures("assets/texture/writing_order" + number + ".png");
            label->Place(surfaceCenter, outwardNormal, 40.0f, 10.0f);

            // 文字を正面から見たときの右方向。
            const Vector3 right(-outwardNormal.z, 0.0f, outwardNormal.x);
            FlashlightWriting* digit = builder.Create<FlashlightWriting>(
                "Stage1KeypadClueDigit" + number);
            digit->Place(surfaceCenter + right * 30.0f, outwardNormal, 20.0f, 20.0f);
            bind(("Stage1KeypadClueLabel" + number).c_str(), [label, digit](const PropPlacement& moved)
            {
                const Vector3 facing = PropLayoutFile::GetFacing(moved);
                label->Place(moved.Position, facing, 40.0f, 10.0f);
                digit->Place(moved.Position + Vector3(-facing.z, 0.0f, facing.x) * 30.0f, facing, 20.0f, 20.0f);
            });
            return digit;
        };
        objects.keypad.panel = keypadPanel;
        objects.keypad.door = keypadDoor;
        objects.keypad.digitWritings = {
            // 開始地点の東の壁（右の倉庫との仕切り）
            createDigitClue(1),
            // 左の倉庫の西の壁
            createDigitClue(2),
            // 中央ホールの南の壁
            createDigitClue(3),
            // 右の倉庫の東の壁
            createDigitClue(4) };

        // 扉の先の部屋: 予備の電池と、入った者を迎える文字を置いている。
        BatteryItem* hiddenBattery = builder.Create<BatteryItem>("Stage1KeypadRoomBattery");
        place(*hiddenBattery, "Stage1KeypadRoomBattery");
        createWriting("Stage1KeypadRoomWriting", "assets/texture/writing_watched.png", "");

        // 閉じ込めイベント（Stage1HiddenRoomEvent）で使う、鍵・記録端末・影。
        // 鍵は閉じ込めた瞬間に、部屋の隅の候補からランダムな位置へ置いている。
        KeyItem* hiddenRoomKey = builder.Create<KeyItem>("Stage1HiddenRoomKey");
        place(*hiddenRoomKey, "Stage1HiddenRoomKey");
        hiddenRoomKey->SetActive(false);

        FuseBox* hiddenRoomRecord = builder.Create<FuseBox>("Stage1HiddenRoomRecord");
        hiddenRoomRecord->SetManualControl("記録を読む");
        hiddenRoomRecord->SetManualInteractionAllowed(true);
        place(*hiddenRoomRecord, "Stage1HiddenRoomRecord");

        ShadowMan* hiddenRoomShadow = builder.Create<ShadowMan>("Stage1HiddenRoomShadow");
        hiddenRoomShadow->SetDeactivateOnExpire(true);
        hiddenRoomShadow->SetActive(false);

        objects.hiddenRoom.door = keypadDoor;
        objects.hiddenRoom.key = hiddenRoomKey;
        objects.hiddenRoom.record = hiddenRoomRecord;
        objects.hiddenRoom.shadow = hiddenRoomShadow;

        // 書類保管室の「照らすと止まる影」。最初は出さず、位置はStage1LightStalkerが毎フレーム決めている。
        ShadowMan* archiveStalker = builder.Create<ShadowMan>("Stage1ArchiveStalker");
        archiveStalker->SetExternallyControlled(true);
        archiveStalker->SetActive(false);
        objects.archiveStalker = archiveStalker;

        // 出口（最後のドアの向こう。調べる対象にはせず、開いたドアを通ったときにStageSceneが脱出を始めている）
        ExitTrigger* exit = builder.Create<ExitTrigger>("ExitTrigger");
        exit->SetPosition(Stage1EndDoorX + 4.0f, -80.0f, Stage1EndDoorZ);
        exit->SetNextScene(SceneName::Stage2);
        exit->SetInteractionEnabled(false);

        // 電池（書類保管室）
        BatteryItem* battery = builder.Create<BatteryItem>("BatteryItem");
        place(*battery, "BatteryItem");

        // 驚かせる瞬間に重ねる画面のノイズ
        ScreenDustOverlay* crt =
            builder.Create<ScreenDustOverlay>("CRTNoise");

        crt->SetPower(0.7f);
        crt->SetActive(false);

        // 電力が戻った後、中央の廊下へ入ると、奥に影が現れる驚かせる演出
        ScareTrigger* corridorScare =
            builder.Create<ScareTrigger>("ScareTrigger_Corridor");
        place(*corridorScare, "ScareTrigger_Corridor");
        corridorScare->SetSize(Vector3(70.0f, 30.0f, 32.0f));
        corridorScare->SetShadowPosition(Vector3(0.0f, -99.0f, 150.0f));
        corridorScare->SetRequiresPower(true);

        // 演出用の人影。どれも最初は隠しておき、StageSceneの演出で出している（時間切れで破棄せずに隠す）
        ShadowMan* exitOmen =
            builder.Create<ShadowMan>("Stage1ExitOmen");
        place(*exitOmen, "Stage1ExitOmen");
        exitOmen->SetDeactivateOnExpire(true);
        exitOmen->SetActive(false);

        ShadowMan* fuseWatcher =
            builder.Create<ShadowMan>("Stage1FuseWatcher");
        fuseWatcher->SetDeactivateOnExpire(true);
        fuseWatcher->SetActive(false);

        ShadowMan* storageShadow =
            builder.Create<ShadowMan>("Stage1StorageShadow");
        storageShadow->SetDeactivateOnExpire(true);
        storageShadow->SetActive(false);

        ShadowMan* evidenceShadow =
            builder.Create<ShadowMan>("Stage1EvidenceShadow");
        evidenceShadow->SetDeactivateOnExpire(true);
        evidenceShadow->SetActive(false);

        objects.player = player;
        objects.fuseWatcher = fuseWatcher;
        objects.storageShadow = storageShadow;
        objects.evidenceShadow = evidenceShadow;
        objects.exitOmen = exitOmen;
        objects.emergencyCharger = emergencyCharger;
        objects.evidenceTerminal = evidenceTerminal;
        objects.exitPowerPanel = exitPowerPanel;
        objects.evidenceMarker = evidenceMarker;
        objects.exitSign = stageExitSign;
        objects.startDoorSign = startDoorSign;
        objects.doorIndicator = doorIndicator;
        objects.loopDoor = door;
        objects.exitDoor = stageExitDoor;
        objects.startDoor = startDoor;
        objects.exitTrigger = exit;
        objects.firstFuse = item1;
        objects.secondFuse = item2;
        objects.thirdFuse = item3;
        // 曲がり角の暗がりを補う通常の器具（下で、通路と部屋の天井にも照明を一通り並べている）
        const auto addCeilingFixture = [&builder](const char* name, float x, float z, float offset)
        {
            CeilingLight* light = builder.Create<CeilingLight>(name);
            light->SetPosition(x, -50.5f, z);
            light->SetScale(22.0f, 2.0f, 10.0f);
            light->SetEmergencyLight(false, offset);
            return light;
        };
        CeilingLight* leftEntranceLight = addCeilingFixture("Stage1LeftEntranceLight", -125.0f, -88.0f, 7.3f);
        CeilingLight* rightEntranceLight = addCeilingFixture("Stage1RightEntranceLight", 125.0f, -88.0f, 8.6f);
        CeilingLight* westEntranceLight = addCeilingFixture("Stage1WestEntranceLight", -190.0f, -20.0f, 9.7f);
        CeilingLight* mainPowerLight = addCeilingFixture("Stage1MainPowerLight", -180.0f, 20.0f, 10.9f);

        // 通路と部屋の天井に、照明を一通り並べている。懐中電灯を消すとほとんど何も見えないため、
        // 足した照明はすべて非常回路につなぎ、停電中も赤い非常灯の光だまりが点々と続くようにしている（電力が戻ると普通の照明になる）。
        // 照明の間には暗がりが残る間隔にしている（P.T. やサイレントヒルのように、光だまりと闇が交互に続く）。
        struct ExtraFixture { const char* Name; float X; float Z; bool Emergency; };
        const ExtraFixture extraFixtures[] =
        {
            { "Stage1HallCenterLight", 0.0f, -75.0f, true },        // 開始地点と中央ホールの間
            { "Stage1HallWestLight", -120.0f, -20.0f, true },        // 中央ホールの西
            { "Stage1HallEastLight", 120.0f, -20.0f, true },         // 中央ホールの東
            { "Stage1HallFarEastLight", 190.0f, 10.0f, true },      // 中央ホールの東の奥
            { "Stage1LeftStorageInnerLight", -180.0f, -150.0f, true },  // 左の倉庫の奥
            { "Stage1RightStorageInnerLight", 185.0f, -150.0f, true }, // 右の倉庫の奥
            { "Stage1CenterStorageLight", 0.0f, -105.0f, true },     // 開始地点の部屋の入口
            { "Stage1CorridorSouthLight", 0.0f, 55.0f, true },      // 廊下の入口
            { "Stage1CorridorMidLight", 0.0f, 160.0f, true },        // 廊下の中ほど
            { "Stage1ArchiveWestLight", -180.0f, 75.0f, true },     // 書類保管室の西
            { "Stage1DebrisLight", -130.0f, 265.0f, true },          // 崩れた資材置き場
            { "Stage1ExitHallLight", 0.0f, 300.0f, true },          // 脱出のホールの奥
            { "Stage1WestWingSouthLight", -345.0f, 35.0f, true },    // 西棟の西の壁際
            { "Stage1SouthWestHallWestLight", -345.0f, -210.0f, true }, // 南西の広間の奥
        };
        for (std::size_t index = 0; index < std::size(extraFixtures); ++index)
        {
            const ExtraFixture& fixture = extraFixtures[index];
            CeilingLight* light = addCeilingFixture(fixture.Name, fixture.X, fixture.Z, 12.3f + static_cast<float>(index) * 0.77f);
            light->SetEmergencyLight(fixture.Emergency, 12.3f + static_cast<float>(index) * 0.77f);
        }

        // 進み具合ごとに、進む先の照明を手前から奥の順に並べている。色や明るさは変えず、そのうち1灯だけをちかちかさせる。
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::StartSearch)] = { light1 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::HallEntrance)] = { light4 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::CorridorDoor)] = { light4, light5, light8, light7 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::LeftStorage)] = { leftEntranceLight, light2 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::RightStorage)] = { rightEntranceLight, light3 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::WestWing)] = { westEntranceLight, westWingLight1 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::PumpRoom)] = { westWingLight2, westWingLight3 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::MainPower)] = { westWingLight2, westWingLight1, mainPowerLight };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::ExitPower)] = { light4, light5, light8, light7 };
        objects.routeLights[static_cast<std::size_t>(Stage1RouteLight::ExitDoor)] = { light7 };

        objects.ceilingLights = {
            light1, light2, light3, light4, light5, light6, light7, light8 };
        return objects;
    }
}
