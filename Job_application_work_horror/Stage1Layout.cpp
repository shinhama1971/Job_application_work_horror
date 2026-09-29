// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を生成して配置します。
// 主な技術: 配置データの分離、生成時のポインタ受け渡し、名前一覧の一元管理
// ============================================================================

#include "Stage1Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
#include "Item.h"
#include "PipeProp.h"
#include "Player.h"
#include "ScareTrigger.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

namespace Stage1Layout
{
    StageObjects Build(Core::Game& game)
    {
        StageObjects objects;
        SceneLayoutBuilder builder(game, objects.objectNames);

        // プレイヤー
        Player* player = builder.Create<Player>("Player");
        player->SetPosition(Vector3(0.0f, -99.0f, -120.0f));

        // 外部FBXの配管モデル（背景装飾）。進行や当たり判定には参加させません。
        PipeProp* pipe = builder.Create<PipeProp>("PipeProp");
        pipe->SetPosition(Vector3(22.0f, -87.54f, -105.0f));
        pipe->SetRotation(Vector3(0.0f, 0.45f, 0.0f));
        pipe->SetScale(Vector3(8.0f, 8.0f, 8.0f));

        PipeProp* pipe2 = builder.Create<PipeProp>("PipeProp2");
        pipe2->SetPosition(Vector3(-22.0f, -87.54f, -105.0f));
        pipe2->SetRotation(Vector3(0.0f, -0.45f, 0.0f));
        pipe2->SetScale(Vector3(8.0f, 8.0f, 8.0f));

        PipeProp* pipe3 = builder.Create<PipeProp>("PipeProp3");
        pipe3->SetPosition(Vector3(0.0f, -87.54f, -155.0f));
        pipe3->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        pipe3->SetScale(Vector3(8.0f, 8.0f, 8.0f));

        // 地面
        Ground* ground = builder.Create<Ground>("Ground");
        ground->SetPosition(0.0f, -100.0f, 0.0f);
        ground->SetScale(20.0f, 1.0f, 20.0f);

        // 壁
        Wall* wall1 = builder.Create<Wall>("Wall1");
        wall1->SetPosition(0.0f, -74.0f, 340.0f);
        wall1->SetScale(440.0f, 50.0f, 4.0f);

        Wall* wall2 = builder.Create<Wall>("Wall2");
        wall2->SetPosition(220.0f, -74.0f, 80.0f);
        wall2->SetScale(4.0f, 50.0f, 520.0f);

        Wall* wall3 = builder.Create<Wall>("Wall3");
        wall3->SetPosition(0.0f, -74.0f, -180.0f);
        wall3->SetScale(440.0f, 50.0f, 4.0f);

        Wall* wall4 = builder.Create<Wall>("Wall4");
        wall4->SetPosition(-220.0f, -74.0f, 80.0f);
        wall4->SetScale(4.0f, 50.0f, 520.0f);

        Wall* wall5 = builder.Create<Wall>("Wall5");
        wall5->SetPosition(-117.5f, -74.0f, 40.0f);
        wall5->SetScale(205.0f, 50.0f, 4.0f);

        Wall* wall6 = builder.Create<Wall>("Wall6");
        wall6->SetPosition(117.5f, -74.0f, 40.0f);
        wall6->SetScale(205.0f, 50.0f, 4.0f);

        // 倉庫区画を分けつつ、中央に広い通路を確保します。
        Wall* wall7 = builder.Create<Wall>("Wall7");
        wall7->SetPosition(-135.0f, -74.0f, -70.0f);
        wall7->SetScale(170.0f, 50.0f, 4.0f);

        Wall* wall8 = builder.Create<Wall>("Wall8");
        wall8->SetPosition(135.0f, -74.0f, -70.0f);
        wall8->SetScale(170.0f, 50.0f, 4.0f);

        // 短い壁で奥の倉庫を三つの探索可能な部屋へ分割します。
        Wall* wall9 = builder.Create<Wall>("Wall9");
        wall9->SetPosition(-90.0f, -74.0f, -140.0f);
        wall9->SetScale(4.0f, 50.0f, 80.0f);

        Wall* wall10 = builder.Create<Wall>("Wall10");
        wall10->SetPosition(90.0f, -74.0f, -140.0f);
        wall10->SetScale(4.0f, 50.0f, 80.0f);

        // 通電扉側は脇部屋を持つ細い廊下として構成します。
        Wall* wall11 = builder.Create<Wall>("Wall11");
        wall11->SetPosition(-45.0f, -74.0f, 75.0f);
        wall11->SetScale(4.0f, 50.0f, 70.0f);

        Wall* wall12 = builder.Create<Wall>("Wall12");
        wall12->SetPosition(-45.0f, -74.0f, 155.0f);
        wall12->SetScale(4.0f, 50.0f, 50.0f);

        Wall* wall13 = builder.Create<Wall>("Wall13");
        wall13->SetPosition(45.0f, -74.0f, 110.0f);
        wall13->SetScale(4.0f, 50.0f, 140.0f);

        // 脱出ホールを区切り、中央の開口部を廊下へ接続します。
        Wall* wall14 = builder.Create<Wall>("Wall14");
        wall14->SetPosition(-132.5f, -74.0f, 180.0f);
        wall14->SetScale(175.0f, 50.0f, 4.0f);

        Wall* wall15 = builder.Create<Wall>("Wall15");
        wall15->SetPosition(132.5f, -74.0f, 180.0f);
        wall15->SetScale(175.0f, 50.0f, 4.0f);

        // 中央開口部から細いL字型のループ廊下を始めます。
        // 最初は北へ進ませ、死角の先で右へ曲がる構成です。
        Wall* loopWall1 = builder.Create<Wall>("LoopWall1");
        loopWall1->SetPosition(-45.0f, -74.0f, 227.5f);
        loopWall1->SetScale(4.0f, 50.0f, 95.0f);

        Wall* loopWall2 = builder.Create<Wall>("LoopWall2");
        loopWall2->SetPosition(45.0f, -74.0f, 207.5f);
        loopWall2->SetScale(4.0f, 50.0f, 55.0f);

        // 南側の壁は角から始め、直線区間の入口を開けておきます。
        Wall* loopWall3 = builder.Create<Wall>("LoopWall3");
        loopWall3->SetPosition(122.5f, -74.0f, 235.0f);
        loopWall3->SetScale(155.0f, 50.0f, 4.0f);

        // 長い壁で前方視界を塞ぎ、右折を自然に誘導します。
        Wall* loopWall4 = builder.Create<Wall>("LoopWall4");
        loopWall4->SetPosition(77.5f, -74.0f, 275.0f);
        loopWall4->SetScale(245.0f, 50.0f, 4.0f);


        // 配管や設備で区画ごとのシルエットを区別し、現在地を把握しやすくします。
        // 縁と天井配管は装飾のみ、床設備には当たり判定を持たせます。
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

        const Color darkMetal(0.10f, 0.115f, 0.11f, 1.0f);
        const Color cabinetMetal(0.16f, 0.18f, 0.17f, 1.0f);
        const Color trimColor(0.075f, 0.08f, 0.075f, 1.0f);
        const Color noEmission(0.0f, 0.0f, 0.0f, 1.0f);

        Wall* ceiling = createStageProp("PropCeilingMain",
            Vector3(0.0f, -47.0f, 80.0f), Vector3(436.0f, 3.0f, 516.0f),
            Color(0.055f, 0.06f, 0.058f, 1.0f), noEmission, 4.0f, false);
        ceiling->SetCastsShadow(false);

        createStageProp("PropPipeLeft", Vector3(-205.0f, -55.0f, 60.0f),
            Vector3(3.0f, 3.0f, 450.0f), darkMetal, noEmission, 22.0f, false);
        createStageProp("PropPipeRight", Vector3(205.0f, -55.0f, 60.0f),
            Vector3(3.0f, 3.0f, 450.0f), darkMetal, noEmission, 22.0f, false);
        createStageProp("PropPipeCrossDoor", Vector3(-115.0f, -52.5f, 37.0f),
            Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
        createStageProp("PropPipeCrossDoorRight", Vector3(115.0f, -52.5f, 37.0f),
            Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
        createStageProp("PropPipeCrossHall", Vector3(-132.5f, -52.5f, 177.0f),
            Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
        createStageProp("PropPipeCrossHallRight", Vector3(132.5f, -52.5f, 177.0f),
            Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);

        createStageProp("PropBaseboardLeft", Vector3(-217.2f, -96.5f, 80.0f),
            Vector3(1.5f, 6.0f, 510.0f), trimColor, noEmission, 5.0f, false);
        createStageProp("PropBaseboardRight", Vector3(217.2f, -96.5f, 80.0f),
            Vector3(1.5f, 6.0f, 510.0f), trimColor, noEmission, 5.0f, false);

        createStageProp("PropCabinetLeft", Vector3(-190.0f, -87.0f, -132.0f),
            Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropCabinetRight", Vector3(190.0f, -87.0f, -112.0f),
            Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropCabinetBack", Vector3(-145.0f, -88.0f, 326.0f),
            Vector3(36.0f, 22.0f, 14.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropServiceBox", Vector3(205.0f, -82.0f, 118.0f),
            Vector3(10.0f, 30.0f, 24.0f), cabinetMetal, noEmission, 12.0f, true);

        createStageProp("PropExitColumnLeft", Vector3(-52.0f, -80.0f, 179.0f),
            Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
        createStageProp("PropExitColumnRight", Vector3(52.0f, -80.0f, 179.0f),
            Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
        Wall* doorIndicator = createStageProp("PropDoorIndicator", Vector3(58.0f, -68.0f, 177.4f),
            Vector3(10.0f, 5.0f, 1.0f), Color(0.26f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f), 28.0f, false);

        const auto createLoopMarker = [&createStageProp, &noEmission](
            const char* name,
            const Vector3& position)
        {
            Wall* marker = createStageProp(name, position,
                Vector3(30.0f, 7.0f, 1.0f),
                Color(0.22f, 0.012f, 0.008f, 1.0f), noEmission, 20.0f, false);
            marker->SetCastsShadow(false);
            marker->SetVisible(false);
            return marker;
        };

        objects.loopMarkers = {
            createLoopMarker("PropLoopMarker1", Vector3(-82.0f, -67.0f, -177.4f)),
            createLoopMarker("PropLoopMarker2", Vector3(82.0f, -67.0f, -177.4f)),
            createLoopMarker("PropLoopMarker3", Vector3(36.0f, -67.0f, 37.4f)) };
        // 天井照明の見た目で、施設の通電状態を直接伝えます。
        CeilingLight* light1 = builder.Create<CeilingLight>("CeilingLight1");
        light1->SetPosition(0.0f, -50.5f, -140.0f);
        light1->SetScale(24.0f, 2.0f, 11.0f);
        light1->SetEmergencyLight(true, 0.0f);

        CeilingLight* light2 = builder.Create<CeilingLight>("CeilingLight2");
        light2->SetPosition(-150.0f, -50.5f, -140.0f);
        light2->SetScale(22.0f, 2.0f, 10.0f);
        light2->SetEmergencyLight(false, 0.8f);

        CeilingLight* light3 = builder.Create<CeilingLight>("CeilingLight3");
        light3->SetPosition(150.0f, -50.5f, -140.0f);
        light3->SetScale(22.0f, 2.0f, 10.0f);
        light3->SetEmergencyLight(true, 1.7f);

        CeilingLight* light4 = builder.Create<CeilingLight>("CeilingLight4");
        light4->SetPosition(0.0f, -50.5f, -10.0f);
        light4->SetScale(26.0f, 2.0f, 11.0f);
        light4->SetEmergencyLight(true, 2.4f);

        CeilingLight* light5 = builder.Create<CeilingLight>("CeilingLight5");
        light5->SetPosition(0.0f, -50.5f, 105.0f);
        light5->SetScale(18.0f, 2.0f, 8.0f);
        light5->SetEmergencyLight(false, 3.1f);

        CeilingLight* light6 = builder.Create<CeilingLight>("CeilingLight6");
        light6->SetPosition(-130.0f, -50.5f, 120.0f);
        light6->SetScale(24.0f, 2.0f, 11.0f);
        light6->SetEmergencyLight(true, 3.8f);

        CeilingLight* light7 = builder.Create<CeilingLight>("CeilingLight7");
        light7->SetPosition(120.0f, -50.5f, 270.0f);
        light7->SetScale(30.0f, 2.0f, 13.0f);
        light7->SetEmergencyLight(false, 4.5f);

        CeilingLight* light8 = builder.Create<CeilingLight>("CeilingLight8");
        light8->SetPosition(0.0f, -50.5f, 215.0f);
        light8->SetScale(20.0f, 2.0f, 8.0f);
        light8->SetEmergencyLight(true, 6.2f);

        // アイテム
        Item* item1 = builder.Create<Item>("Item1");
        item1->SetPosition(0.0f, -95.0f, -155.0f);

        Item* item2 = builder.Create<Item>("Item2");
        item2->SetPosition(-150.0f, -95.0f, -140.0f);
        item2->SetActive(false);

        Item* item3 = builder.Create<Item>("Item3");
        item3->SetPosition(150.0f, -95.0f, -140.0f);
        item3->SetActive(false);

        // ドア
        Door* door = builder.Create<Door>("Door");
        door->SetPosition(0.0f, -74.0f, 40.0f);
        FuseBox* fuseBox = builder.Create<FuseBox>("FuseBox");
        fuseBox->SetPosition(-180.0f, -90.0f, 35.0f);

        FuseBox* exitPowerPanel =
            builder.Create<FuseBox>("ExitPowerPanel");
        exitPowerPanel->SetExitControl(true);
        exitPowerPanel->SetPosition(145.0f, -90.0f, 270.0f);

        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage1EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        emergencyCharger->SetPosition(-205.0f, -90.0f, -112.0f);
        emergencyCharger->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        // 任意探索の報酬は最短経路から外して配置します。
        // 素早い脱出と完全探索のどちらを選ぶか判断させるためです。
        FuseBox* evidenceTerminal =
            builder.Create<FuseBox>("Stage1EvidenceTerminal");
        evidenceTerminal->SetManualControl("監視カメラを確認する");
        evidenceTerminal->SetManualInteractionAllowed(true);
        evidenceTerminal->SetPosition(205.0f, -90.0f, -42.0f);
        evidenceTerminal->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));
        Wall* evidenceMarker = createStageProp(
            "Stage1EvidenceMarker",
            Vector3(217.2f, -65.0f, -42.0f),
            Vector3(1.0f, 5.0f, 18.0f),
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.02f, 0.24f, 0.16f, 1.0f),
            36.0f,
            false);
        evidenceMarker->SetCastsShadow(false);


        // 1面の出口は、見える扉を操作して通過したときだけ成立させます。
        // 廊下を歩きながら操作キーを押すだけで透明トリガーが反応する問題を防ぎます。
        Door* stageExitDoor = builder.Create<Door>("Stage1ExitDoor");
        stageExitDoor->SetPosition(202.0f, -74.0f, 307.5f);
        stageExitDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        stageExitDoor->SetScale(Vector3(60.0f, 50.0f, 4.0f));
        stageExitDoor->SetLocked(true);

        // 監視カメラの異常用に、壁際へ開かずの扉を置きます。開いても先は壁です。
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

        Wall* stageExitSign = createStageProp(
            "PropStage1ExitSign",
            Vector3(198.0f, -51.5f, 307.5f),
            Vector3(2.0f, 5.0f, 24.0f),
            Color(0.24f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f),
            30.0f,
            false);
        stageExitSign->SetCastsShadow(false);

        ExitTrigger* exit = builder.Create<ExitTrigger>("ExitTrigger");
        exit->SetPosition(207.0f, -80.0f, 307.5f);
        exit->SetNextScene(SceneName::Stage2);
        exit->SetInteractionEnabled(false);

        // バッテリー
        BatteryItem* battery = builder.Create<BatteryItem>("BatteryItem");
        battery->SetPosition(-130.0f, -95.0f, 120.0f);

        ScreenDustOverlay* crt =
            builder.Create<ScreenDustOverlay>("CRTNoise");

        crt->SetPower(0.7f);
        crt->SetActive(false);

        ScareTrigger* corridorScare =
            builder.Create<ScareTrigger>("ScareTrigger_Corridor");
        corridorScare->SetPosition(Vector3(0.0f, -90.0f, 65.0f));
        corridorScare->SetSize(Vector3(70.0f, 30.0f, 32.0f));
        corridorScare->SetShadowPosition(Vector3(0.0f, -99.0f, 150.0f));
        corridorScare->SetRequiresPower(true);

        ShadowMan* exitOmen =
            builder.Create<ShadowMan>("Stage1ExitOmen");
        exitOmen->SetPosition(92.0f, -99.0f, 278.0f);
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
        objects.doorIndicator = doorIndicator;
        objects.loopDoor = door;
        objects.exitDoor = stageExitDoor;
        objects.exitTrigger = exit;
        objects.secondFuse = item2;
        objects.thirdFuse = item3;
        objects.ceilingLights = {
            light1, light2, light3, light4, light5, light6, light7, light8 };
        return objects;
    }
}
