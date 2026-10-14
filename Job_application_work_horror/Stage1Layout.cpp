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
#include "Player.h"
#include "ScareTrigger.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <array>
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

        // プレイヤー（開始地点は南の区画の中央）
        Player* player = builder.Create<Player>("Player");
        player->SetPosition(Vector3(0.0f, -99.0f, -120.0f));

        // 外部のFBXの配管モデル（背景の飾り）。進行や当たり判定には参加させていない。
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

        // 床（建物全体を覆う1000x1000の濡れた床）
        Ground* ground = builder.Create<Ground>("Ground");
        ground->SetPosition(0.0f, -100.0f, 0.0f);
        ground->SetScale(20.0f, 1.0f, 20.0f);
        // 西棟は床一面が水に浸かっている（水たまりと同じ反射・波紋を、範囲全体に出している）。
        ground->SetFloodRegion(
            Vector2(Stage1WestWing::MinX, Stage1WestWing::MinZ),
            Vector2(Stage1WestWing::MaxX, Stage1WestWing::MaxZ));

        // 壁（外周の4面と、部屋を区切る壁）
        Wall* wall1 = builder.Create<Wall>("Wall1");
        wall1->SetPosition(0.0f, -74.0f, 340.0f);
        wall1->SetScale(440.0f, 50.0f, 4.0f);

        Wall* wall2 = builder.Create<Wall>("Wall2");
        wall2->SetPosition(220.0f, -74.0f, 80.0f);
        wall2->SetScale(4.0f, 50.0f, 520.0f);

        // 南側の外壁は、左の倉庫から南西の広間への入口（x=-175〜-145）の分だけ開けて2枚に分けている。
        Wall* wall3West = builder.Create<Wall>("Wall3West");
        wall3West->SetPosition(-197.5f, -74.0f, -180.0f);
        wall3West->SetScale(45.0f, 50.0f, 4.0f);

        Wall* wall3 = builder.Create<Wall>("Wall3");
        wall3->SetPosition(37.5f, -74.0f, -180.0f);
        wall3->SetScale(365.0f, 50.0f, 4.0f);

        // 西側の外壁は、西棟の扉（z=-35〜-5）の分だけ開けて2枚に分けている。
        Wall* wall4 = builder.Create<Wall>("Wall4");
        wall4->SetPosition(-220.0f, -74.0f, -107.5f);
        wall4->SetScale(4.0f, 50.0f, 145.0f);

        Wall* wall4North = builder.Create<Wall>("Wall4North");
        wall4North->SetPosition(-220.0f, -74.0f, 167.5f);
        wall4North->SetScale(4.0f, 50.0f, 345.0f);

        // 西棟（浸水した機械室）。西側の扉から入る、柱の並ぶ広い部屋（技術デモの部屋と同じ作り）。
        // 奥（北）は仕切りの壁で区切ったポンプ室で、仕切りの真ん中の切れ目（x=-320〜-280）から入る。
        const auto createWestWingWall = [&builder](const char* name, const Vector3& position, const Vector3& scale)
        {
            Wall* wall = builder.Create<Wall>(name);
            wall->SetPosition(position.x, position.y, position.z);
            wall->SetScale(scale.x, scale.y, scale.z);
            return wall;
        };
        Wall* westWingOuter = createWestWingWall("WestWingOuter", Vector3(-380.0f, -74.0f, 80.0f), Vector3(4.0f, 50.0f, 364.0f));
        Wall* westWingSouth = createWestWingWall("WestWingSouth", Vector3(-300.0f, -74.0f, -100.0f), Vector3(164.0f, 50.0f, 4.0f));
        Wall* westWingNorth = createWestWingWall("WestWingNorth", Vector3(-300.0f, -74.0f, 260.0f), Vector3(164.0f, 50.0f, 4.0f));
        Wall* pumpRoomWallWest = createWestWingWall("WestWingPumpRoomWallWest", Vector3(-350.0f, -74.0f, 180.0f), Vector3(60.0f, 50.0f, 4.0f));
        Wall* pumpRoomWallEast = createWestWingWall("WestWingPumpRoomWallEast", Vector3(-250.0f, -74.0f, 180.0f), Vector3(60.0f, 50.0f, 4.0f));
        // 2列3行の柱。見通しを少しさえぎり、懐中電灯の影と、水面に映る柱を見せている。
        std::array<Wall*, 6> westWingColumns{};
        for (int column = 0; column < 6; ++column)
        {
            const std::string name = "WestWingColumn" + std::to_string(column + 1);
            westWingColumns[static_cast<size_t>(column)] = createWestWingWall(name.c_str(),
                Vector3(column % 2 == 0 ? -330.0f : -270.0f, -74.0f, 10.0f + static_cast<float>(column / 2) * 60.0f),
                Vector3(10.0f, 50.0f, 10.0f));
        }

        // 南西の広間（西棟の南、x=-380〜-120・z=-332〜-180）。左の倉庫の南の入口から入る、柱の並ぶ広い部屋。
        // 技術デモ（GraphicsTechDemo）の部屋と同じ作りで、柱・棚・机・色の付いた警告灯を置いている。
        // 北の壁は、建物の南の外壁（x>-220）とつながる西側の分だけを足している。
        Wall* southWestHallNorth = createWestWingWall("SouthWestHallNorth", Vector3(-300.0f, -74.0f, -180.0f), Vector3(160.0f, 50.0f, 4.0f));
        Wall* southWestHallWest = createWestWingWall("SouthWestHallWest", Vector3(-380.0f, -74.0f, -256.0f), Vector3(4.0f, 50.0f, 152.0f));
        Wall* southWestHallSouth = createWestWingWall("SouthWestHallSouth", Vector3(-250.0f, -74.0f, -332.0f), Vector3(264.0f, 50.0f, 4.0f));
        Wall* southWestHallEast = createWestWingWall("SouthWestHallEast", Vector3(-120.0f, -74.0f, -256.0f), Vector3(4.0f, 50.0f, 152.0f));
        // 2列2行の柱。見通しを少しさえぎり、懐中電灯の影と、天井の照明の映り込みを見せている。
        std::array<Wall*, 4> southWestHallColumns{};
        for (int column = 0; column < 4; ++column)
        {
            const std::string name = "SouthWestHallColumn" + std::to_string(column + 1);
            southWestHallColumns[static_cast<size_t>(column)] = createWestWingWall(name.c_str(),
                Vector3(column % 2 == 0 ? -290.0f : -210.0f, -74.0f, column < 2 ? -225.0f : -285.0f),
                Vector3(10.0f, 50.0f, 10.0f));
        }

        Wall* wall5 = builder.Create<Wall>("Wall5");
        wall5->SetPosition(-117.5f, -74.0f, 40.0f);
        wall5->SetScale(205.0f, 50.0f, 4.0f);

        Wall* wall6 = builder.Create<Wall>("Wall6");
        wall6->SetPosition(117.5f, -74.0f, 40.0f);
        wall6->SetScale(205.0f, 50.0f, 4.0f);

        // 倉庫の区画を分けつつ、中央に広い通路を残している。
        Wall* wall7 = builder.Create<Wall>("Wall7");
        wall7->SetPosition(-135.0f, -74.0f, -70.0f);
        wall7->SetScale(170.0f, 50.0f, 4.0f);

        Wall* wall8 = builder.Create<Wall>("Wall8");
        wall8->SetPosition(135.0f, -74.0f, -70.0f);
        wall8->SetScale(170.0f, 50.0f, 4.0f);

        // 短い壁で、奥の倉庫を探索できる3つの部屋に分けている。
        Wall* wall9 = builder.Create<Wall>("Wall9");
        wall9->SetPosition(-90.0f, -74.0f, -140.0f);
        wall9->SetScale(4.0f, 50.0f, 80.0f);

        Wall* wall10 = builder.Create<Wall>("Wall10");
        wall10->SetPosition(90.0f, -74.0f, -140.0f);
        wall10->SetScale(4.0f, 50.0f, 80.0f);

        // 電力の扉の側は、脇の部屋を持つ細い廊下にしている。
        Wall* wall11 = builder.Create<Wall>("Wall11");
        wall11->SetPosition(-45.0f, -74.0f, 75.0f);
        wall11->SetScale(4.0f, 50.0f, 70.0f);

        Wall* wall12 = builder.Create<Wall>("Wall12");
        wall12->SetPosition(-45.0f, -74.0f, 155.0f);
        wall12->SetScale(4.0f, 50.0f, 50.0f);

        // 東側の壁は、暗証番号の扉（z=95〜125）の分だけ開けて2枚に分けている。
        // 扉の先は、それまで入れなかった東側の部屋。
        Wall* wall13 = builder.Create<Wall>("Wall13");
        wall13->SetPosition(45.0f, -74.0f, 67.5f);
        wall13->SetScale(4.0f, 50.0f, 55.0f);

        Wall* wall13North = builder.Create<Wall>("Wall13North");
        wall13North->SetPosition(45.0f, -74.0f, 152.5f);
        wall13North->SetScale(4.0f, 50.0f, 55.0f);

        // 脱出のホールを区切り、中央の開口部を廊下へつないでいる。
        Wall* wall14 = builder.Create<Wall>("Wall14");
        wall14->SetPosition(-132.5f, -74.0f, 180.0f);
        wall14->SetScale(175.0f, 50.0f, 4.0f);

        Wall* wall15 = builder.Create<Wall>("Wall15");
        wall15->SetPosition(132.5f, -74.0f, 180.0f);
        wall15->SetScale(175.0f, 50.0f, 4.0f);

        // 中央の開口部から、細いL字型のループ廊下を始めている。
        // 最初は北へ進ませ、死角の先で右へ曲がる作り。
        Wall* loopWall1 = builder.Create<Wall>("LoopWall1");
        loopWall1->SetPosition(-45.0f, -74.0f, 227.5f);
        loopWall1->SetScale(4.0f, 50.0f, 95.0f);

        Wall* loopWall2 = builder.Create<Wall>("LoopWall2");
        loopWall2->SetPosition(45.0f, -74.0f, 207.5f);
        loopWall2->SetScale(4.0f, 50.0f, 55.0f);

        // 南側の壁は角から始め、まっすぐな区間の入口を開けている。
        Wall* loopWall3 = builder.Create<Wall>("LoopWall3");
        loopWall3->SetPosition(122.5f, -74.0f, 235.0f);
        loopWall3->SetScale(155.0f, 50.0f, 4.0f);

        // 長い壁で前の見通しをふさぎ、自然に右へ曲がるよう誘っている。
        Wall* loopWall4 = builder.Create<Wall>("LoopWall4");
        loopWall4->SetPosition(77.5f, -74.0f, 275.0f);
        loopWall4->SetScale(245.0f, 50.0f, 4.0f);

        // 建物の壁だけに、パネルの継ぎ目・ひび・水の跡・カビを描いている（棚や配管などの小物には描かない）。
        for (Wall* structuralWall : {
            wall1, wall2, wall3West, wall3, wall4, wall4North, wall5, wall6, wall7, wall8, wall9, wall10,
            wall11, wall12, wall13, wall13North, wall14, wall15,
            loopWall1, loopWall2, loopWall3, loopWall4,
            westWingOuter, westWingSouth, westWingNorth, pumpRoomWallWest, pumpRoomWallEast,
            southWestHallNorth, southWestHallWest, southWestHallSouth, southWestHallEast })
        {
            structuralWall->SetWeatheringSurface(true);
            // 部屋の角の暗がりの計算用に、壁を上から見た長方形を記録している
            const Vector3 position = structuralWall->GetPosition();
            const Vector3 scale = structuralWall->GetScale();
            objects.wallFootprints.emplace_back(
                position.x, position.z, scale.x * 0.5f, scale.z * 0.5f);
        }
        // 西棟と南西の広間の柱も建物の一部として古さを描いている。部屋の角の暗がりは壁32枚までなので、柱は含めていない。
        for (Wall* column : westWingColumns)
        {
            column->SetWeatheringSurface(true);
        }
        for (Wall* column : southWestHallColumns)
        {
            column->SetWeatheringSurface(true);
        }


        // 配管や設備で区画ごとの見た目の形を変え、今いる場所を分かりやすくしている。
        // 縁と天井の配管は飾りだけ、床の設備には当たり判定を持たせている。
        // Wallを、色と当たり判定の有無を指定した小物として作る関数
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

        // 小物の色（暗い金属・棚の金属・縁取り）
        const Color darkMetal(0.10f, 0.115f, 0.11f, 1.0f);
        const Color cabinetMetal(0.16f, 0.18f, 0.17f, 1.0f);
        const Color trimColor(0.075f, 0.08f, 0.075f, 1.0f);
        const Color noEmission(0.0f, 0.0f, 0.0f, 1.0f);

        // 天井（床から53の高さに、施設全体を覆う板を置いている。影は落とさない）
        Wall* ceiling = createStageProp("PropCeilingMain",
            Vector3(0.0f, -47.0f, 80.0f), Vector3(436.0f, 3.0f, 516.0f),
            Color(0.055f, 0.06f, 0.058f, 1.0f), noEmission, 4.0f, false);
        ceiling->SetCastsShadow(false);
        // 天井も建物の面として、天井板・外れた板の穴・雨漏りの染みを描いている（litTexturePS）。
        ceiling->SetWeatheringSurface(true);
        // 西棟の天井
        Wall* westWingCeiling = createStageProp("PropCeilingWestWing",
            Vector3(-300.0f, -47.0f, 80.0f), Vector3(164.0f, 3.0f, 364.0f),
            Color(0.055f, 0.06f, 0.058f, 1.0f), noEmission, 4.0f, false);
        westWingCeiling->SetCastsShadow(false);
        westWingCeiling->SetWeatheringSurface(true);
        // 南西の広間の天井
        Wall* southWestHallCeiling = createStageProp("PropCeilingSouthWestHall",
            Vector3(-250.0f, -47.0f, -256.0f), Vector3(264.0f, 3.0f, 156.0f),
            Color(0.055f, 0.06f, 0.058f, 1.0f), noEmission, 4.0f, false);
        southWestHallCeiling->SetCastsShadow(false);
        southWestHallCeiling->SetWeatheringSurface(true);

        // 壁際と天井近くを走る配管（Stage1AmbientSoundsが、この線に沿って配管の音を鳴らしている）
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

        // 西側の幅木は、西棟の扉（z=-35〜-5）の前で途切れさせている。
        createStageProp("PropBaseboardLeft", Vector3(-217.2f, -96.5f, -105.0f),
            Vector3(1.5f, 6.0f, 140.0f), trimColor, noEmission, 5.0f, false);
        createStageProp("PropBaseboardLeftNorth", Vector3(-217.2f, -96.5f, 165.0f),
            Vector3(1.5f, 6.0f, 340.0f), trimColor, noEmission, 5.0f, false);
        createStageProp("PropBaseboardRight", Vector3(217.2f, -96.5f, 80.0f),
            Vector3(1.5f, 6.0f, 510.0f), trimColor, noEmission, 5.0f, false);

        // 棚や設備（当たり判定あり）
        createStageProp("PropCabinetLeft", Vector3(-190.0f, -87.0f, -132.0f),
            Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropCabinetRight", Vector3(190.0f, -87.0f, -112.0f),
            Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropCabinetBack", Vector3(-145.0f, -88.0f, 326.0f),
            Vector3(36.0f, 22.0f, 14.0f), cabinetMetal, noEmission, 14.0f, true);
        createStageProp("PropServiceBox", Vector3(205.0f, -82.0f, 118.0f),
            Vector3(10.0f, 30.0f, 24.0f), cabinetMetal, noEmission, 12.0f, true);

        // 書類保管室（廊下の西側の部屋）。目線より高い書類棚を互い違いに並べ、見通しの悪い迷路にしている。
        // 入口は x=-45 の壁の z=110〜130。
        const Color shelfColor(0.13f, 0.12f, 0.10f, 1.0f);
        createStageProp("PropArchiveShelf1", Vector3(-160.0f, -77.0f, 70.0f),
            Vector3(80.0f, 44.0f, 6.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropArchiveShelf2", Vector3(-105.0f, -77.0f, 100.0f),
            Vector3(90.0f, 44.0f, 6.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropArchiveShelf3", Vector3(-155.0f, -77.0f, 130.0f),
            Vector3(90.0f, 44.0f, 6.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropArchiveShelf4", Vector3(-110.0f, -77.0f, 158.0f),
            Vector3(100.0f, 44.0f, 6.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropArchiveDesk", Vector3(-200.0f, -89.0f, 160.0f),
            Vector3(26.0f, 20.0f, 14.0f), cabinetMetal, noEmission, 12.0f, true);

        // 崩れた資材置き場（北西の部屋）。崩れた資材と機材で視界をさえぎっている。
        const Color debrisColor(0.12f, 0.11f, 0.095f, 1.0f);
        createStageProp("PropDebris1", Vector3(-150.0f, -84.0f, 230.0f),
            Vector3(40.0f, 30.0f, 30.0f), debrisColor, noEmission, 6.0f, true);
        createStageProp("PropDebris2", Vector3(-95.0f, -88.0f, 285.0f),
            Vector3(50.0f, 22.0f, 24.0f), debrisColor, noEmission, 6.0f, true);
        createStageProp("PropDebris3", Vector3(-190.0f, -80.0f, 300.0f),
            Vector3(30.0f, 38.0f, 40.0f), cabinetMetal, noEmission, 10.0f, true);
        createStageProp("PropDebrisBeam", Vector3(-120.0f, -93.0f, 250.0f),
            Vector3(60.0f, 6.0f, 6.0f), darkMetal, noEmission, 18.0f, true);

        // 西棟の中の機材（ポンプと配管）。水の中に沈んだ機械室らしさを出している。
        createStageProp("PropWestWingPump", Vector3(-345.0f, -86.0f, 235.0f),
            Vector3(30.0f, 26.0f, 26.0f), darkMetal, noEmission, 20.0f, true);
        createStageProp("PropWestWingTank", Vector3(-240.0f, -82.0f, 205.0f),
            Vector3(20.0f, 34.0f, 30.0f), cabinetMetal, noEmission, 14.0f, true);
        // ポンプ室の床の排水口。排水バルブを回すと、水はここへ向かって引いていく。
        Wall* westWingDrain = createStageProp("PropWestWingDrain", Vector3(-290.0f, -99.6f, 228.0f),
            Vector3(16.0f, 0.8f, 16.0f), Color(0.035f, 0.04f, 0.038f, 1.0f), noEmission, 16.0f, false);
        westWingDrain->SetCastsShadow(false);
        createStageProp("PropWestWingPipe", Vector3(-375.0f, -55.0f, 80.0f),
            Vector3(3.0f, 3.0f, 350.0f), darkMetal, noEmission, 22.0f, false);

        // 南西の広間の中。西の奥は、棚・机・細い柱・木箱の一角（懐中電灯の影が、根元はくっきり、奥の棚ではぼんやり落ちる）。
        createStageProp("PropSouthWestShelf1", Vector3(-362.0f, -77.0f, -300.0f),
            Vector3(10.0f, 44.0f, 30.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropSouthWestShelf2", Vector3(-362.0f, -77.0f, -262.0f),
            Vector3(10.0f, 44.0f, 30.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropSouthWestShelf3", Vector3(-362.0f, -77.0f, -224.0f),
            Vector3(10.0f, 44.0f, 30.0f), shelfColor, noEmission, 10.0f, true);
        const Color deskWood(0.16f, 0.13f, 0.095f, 1.0f);
        createStageProp("PropSouthWestDeskTop", Vector3(-322.0f, -76.0f, -300.0f),
            Vector3(40.0f, 2.0f, 24.0f), deskWood, noEmission, 8.0f, false);
        for (int leg = 0; leg < 4; ++leg)
        {
            const std::string name = "PropSouthWestDeskLeg" + std::to_string(leg + 1);
            createStageProp(name.c_str(),
                Vector3(-322.0f + (leg % 2 == 0 ? -18.0f : 18.0f), -88.5f, -300.0f + (leg < 2 ? -10.0f : 10.0f)),
                Vector3(2.0f, 23.0f, 2.0f), deskWood, noEmission, 8.0f, true);
        }
        createStageProp("PropSouthWestPole", Vector3(-268.0f, -75.0f, -252.0f),
            Vector3(3.0f, 50.0f, 3.0f), darkMetal, noEmission, 20.0f, true);
        createStageProp("PropSouthWestCrate1", Vector3(-340.0f, -93.0f, -205.0f),
            Vector3(14.0f, 14.0f, 14.0f), debrisColor, noEmission, 6.0f, true);
        createStageProp("PropSouthWestCrate2", Vector3(-240.0f, -95.0f, -316.0f),
            Vector3(10.0f, 10.0f, 10.0f), debrisColor, noEmission, 6.0f, true);

        // 色の付いた警告灯（赤・緑・青・橙）。暗い中で目立ち、自分の色で周りを照らす点光源にもなる（タイルベースライティング）。
        const auto createWarningLamp = [&createStageProp](const char* name, const Vector3& position, const Vector3& scale, const Color& color)
        {
            Wall* lamp = createStageProp(name, position, scale,
                Color(color.R() * 0.6f, color.G() * 0.6f, color.B() * 0.6f, 1.0f), color, 24.0f, false);
            lamp->SetCastsShadow(false);
            lamp->SetGlowLight(55.0f, 2.0f);
            return lamp;
        };
        createWarningLamp("PropSouthWestLampRed", Vector3(-250.0f, -70.0f, -329.5f), Vector3(10.0f, 4.0f, 1.0f), Color(0.90f, 0.08f, 0.05f, 1.0f));
        createWarningLamp("PropSouthWestLampGreen", Vector3(-122.5f, -70.0f, -256.0f), Vector3(1.0f, 4.0f, 10.0f), Color(0.10f, 0.85f, 0.25f, 1.0f));
        createWarningLamp("PropSouthWestLampBlue", Vector3(-377.5f, -70.0f, -190.0f), Vector3(1.0f, 4.0f, 10.0f), Color(0.15f, 0.35f, 0.95f, 1.0f));
        createWarningLamp("PropSouthWestLampAmber", Vector3(-180.0f, -70.0f, -329.5f), Vector3(10.0f, 4.0f, 1.0f), Color(0.95f, 0.55f, 0.10f, 1.0f));

        // 西棟の南西の角。南西の広間と同じ、棚・机・木箱の一角（水面に映り、懐中電灯の影が落ちる）。
        createStageProp("PropWestWingShelf1", Vector3(-368.0f, -77.0f, -78.0f),
            Vector3(10.0f, 44.0f, 30.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropWestWingShelf2", Vector3(-368.0f, -77.0f, -40.0f),
            Vector3(10.0f, 44.0f, 30.0f), shelfColor, noEmission, 10.0f, true);
        createStageProp("PropWestWingDeskTop", Vector3(-330.0f, -76.0f, -78.0f),
            Vector3(40.0f, 2.0f, 24.0f), deskWood, noEmission, 8.0f, false);
        for (int leg = 0; leg < 4; ++leg)
        {
            const std::string name = "PropWestWingDeskLeg" + std::to_string(leg + 1);
            createStageProp(name.c_str(),
                Vector3(-330.0f + (leg % 2 == 0 ? -18.0f : 18.0f), -88.5f, -78.0f + (leg < 2 ? -10.0f : 10.0f)),
                Vector3(2.0f, 23.0f, 2.0f), deskWood, noEmission, 8.0f, true);
        }
        createStageProp("PropWestWingCrate", Vector3(-345.0f, -93.0f, -15.0f),
            Vector3(14.0f, 14.0f, 14.0f), debrisColor, noEmission, 6.0f, true);

        // 西棟の排水バルブ（南・西・東の壁に1つずつ）。上のランプは、回す前は赤く、回したら緑に光る。
        // バルブの配管は床から天井まで壁を伝い、ランプは配管の横に付けている。
        const auto createDrainValve = [&builder, &createWarningLamp](
            const char* valveName, const char* lampName,
            const Vector3& position, float yaw, const Vector3& lampOffset, const Vector3& lampScale)
        {
            DrainValve* valve = builder.Create<DrainValve>(valveName);
            valve->SetPosition(position.x, position.y, position.z);
            valve->SetRotation(Vector3(0.0f, yaw, 0.0f));
            Wall* lamp = createWarningLamp(lampName, position + lampOffset, lampScale, Color(0.90f, 0.08f, 0.05f, 1.0f));
            lamp->SetGlowLight(45.0f, 1.8f);
            return std::pair<DrainValve*, Wall*>(valve, lamp);
        };
        const auto southValve = createDrainValve("Stage1WestWingValveSouth", "PropWestWingValveLampSouth",
            Vector3(-290.0f, -88.0f, -98.0f), DirectX::XM_PI, Vector3(9.0f, 24.0f, 0.5f), Vector3(10.0f, 4.0f, 1.0f));
        const auto westValve = createDrainValve("Stage1WestWingValveWest", "PropWestWingValveLampWest",
            Vector3(-378.0f, -88.0f, 100.0f), -DirectX::XM_PIDIV2, Vector3(0.5f, 24.0f, 9.0f), Vector3(1.0f, 4.0f, 10.0f));
        const auto eastValve = createDrainValve("Stage1WestWingValveEast", "PropWestWingValveLampEast",
            Vector3(-222.0f, -88.0f, 150.0f), DirectX::XM_PIDIV2, Vector3(-0.5f, 24.0f, 9.0f), Vector3(1.0f, 4.0f, 10.0f));
        objects.westWing.valves = { southValve.first, westValve.first, eastValve.first };
        objects.westWing.valveLamps = { southValve.second, westValve.second, eastValve.second };

        // 脱出のホールの入口の柱と、扉の状態を示すランプ（停電中は赤）
        createStageProp("PropExitColumnLeft", Vector3(-52.0f, -80.0f, 179.0f),
            Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
        createStageProp("PropExitColumnRight", Vector3(52.0f, -80.0f, 179.0f),
            Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
        Wall* doorIndicator = createStageProp("PropDoorIndicator", Vector3(58.0f, -68.0f, 177.4f),
            Vector3(10.0f, 5.0f, 1.0f), Color(0.26f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f), 28.0f, false);

        // ループ廊下で、周回によって現れる赤い目印（最初は隠している）
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
        // 天井照明の見た目で、施設に電気が来ているかを直接伝えている（非常灯は停電中も赤く光る）。
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

        // ヒューズ。どの部屋にあるかは固定し（目的表示と演出の流れを保つため）、
        // 部屋の中のどこに落ちているかを、プレイごとに候補からランダムに選んでいる。
        // 覚えた場所へ走るのではなく、毎回ライトで部屋を探させるためである。
        std::mt19937 fuseRandom{ std::random_device{}() };
        const auto placeFuse = [&fuseRandom](Item* fuse, const std::array<Vector3, 3>& candidates)
        {
            std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
            const Vector3& spot = candidates[pick(fuseRandom)];
            fuse->SetPosition(spot.x, spot.y, spot.z);
        };
        // 1本目: 開始地点の区画（配管の陰・左奥・右奥）
        const std::array<Vector3, 3> firstFuseSpots =
        {
            Vector3(0.0f, -95.0f, -155.0f),
            Vector3(-62.0f, -95.0f, -168.0f),
            Vector3(64.0f, -95.0f, -160.0f)
        };
        // 2本目: 左の倉庫。倉庫の演出（StageScene::UpdateStorageScare）が始まる
        // x<-110, z<-108 の範囲の中だけから選んでいる。
        const std::array<Vector3, 3> secondFuseSpots =
        {
            Vector3(-150.0f, -95.0f, -140.0f),
            Vector3(-196.0f, -95.0f, -160.0f),
            Vector3(-128.0f, -95.0f, -166.0f)
        };
        // 3本目: 西棟の一番奥のポンプ室（ポンプの陰・奥の角・タンク寄り）。
        // 右の倉庫で西棟の鍵を拾って西側の扉を開け、排水バルブで水を抜くまで現れない（Stage1WestWingが出している）。
        const std::array<Vector3, 3> thirdFuseSpots =
        {
            Vector3(-345.0f, -95.0f, 210.0f),
            Vector3(-365.0f, -95.0f, 250.0f),
            Vector3(-245.0f, -95.0f, 240.0f)
        };

        Item* item1 = builder.Create<Item>("Item1");
        placeFuse(item1, firstFuseSpots);

        // 2本目と3本目は、進行に合わせて出すので最初は隠している
        Item* item2 = builder.Create<Item>("Item2");
        placeFuse(item2, secondFuseSpots);
        item2->SetActive(false);

        Item* item3 = builder.Create<Item>("Item3");
        placeFuse(item3, thirdFuseSpots);
        item3->SetActive(false);

        // 西棟の鍵。右の倉庫（前は3本目のヒューズがあった部屋）のどこかに、2周目のループの後に現れる。
        const std::array<Vector3, 3> westWingKeySpots =
        {
            Vector3(150.0f, -95.0f, -140.0f),
            Vector3(196.0f, -95.0f, -164.0f),
            Vector3(122.0f, -95.0f, -98.0f)
        };
        KeyItem* westWingKey = builder.Create<KeyItem>("Stage1WestWingKey");
        {
            std::uniform_int_distribution<std::size_t> pick(0, westWingKeySpots.size() - 1);
            const Vector3& spot = westWingKeySpots[pick(fuseRandom)];
            westWingKey->SetPosition(spot.x, spot.y, spot.z);
        }
        westWingKey->SetActive(false);

        // 西棟の扉（西側の外壁の z=-35〜-5）。鍵を拾うまで開かない。
        Door* westWingDoor = builder.Create<Door>("Stage1WestWingDoor");
        westWingDoor->SetPosition(-220.0f, -74.0f, -20.0f);
        westWingDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        westWingDoor->SetLocked(true);

        objects.westWing.door = westWingDoor;
        objects.westWing.key = westWingKey;
        objects.westWing.fuse = item3;
        objects.westWing.ground = ground;

        // 寄り道のごほうびの電池（西棟の広間・崩れた資材置き場）。
        BatteryItem* westWingBattery = builder.Create<BatteryItem>("Stage1WestWingBattery");
        westWingBattery->SetPosition(-355.0f, -95.0f, 40.0f);
        BatteryItem* debrisBattery = builder.Create<BatteryItem>("Stage1DebrisRoomBattery");
        debrisBattery->SetPosition(-120.0f, -95.0f, 300.0f);
        // 南西の広間の奥（棚の一角の木箱の陰）にも、寄り道のごほうびの電池を置いている。
        BatteryItem* southWestHallBattery = builder.Create<BatteryItem>("Stage1SouthWestHallBattery");
        southWestHallBattery->SetPosition(-348.0f, -95.0f, -222.0f);

        // 南西の広間の天井の照明（停電中は1つだけ赤い非常灯がともり、電力が戻ると両方が点く）
        CeilingLight* southWestHallLight1 = builder.Create<CeilingLight>("Stage1SouthWestHallLight1");
        southWestHallLight1->SetPosition(-290.0f, -50.5f, -256.0f);
        southWestHallLight1->SetScale(24.0f, 2.0f, 11.0f);
        southWestHallLight1->SetEmergencyLight(false, 5.0f);
        CeilingLight* southWestHallLight2 = builder.Create<CeilingLight>("Stage1SouthWestHallLight2");
        southWestHallLight2->SetPosition(-180.0f, -50.5f, -256.0f);
        southWestHallLight2->SetScale(24.0f, 2.0f, 11.0f);
        southWestHallLight2->SetEmergencyLight(true, 5.7f);

        // 西棟の天井の照明（停電中は入口側の1つだけが赤い非常灯。奥とポンプ室は懐中電灯とバルブのランプが頼り）
        CeilingLight* westWingLight1 = builder.Create<CeilingLight>("Stage1WestWingLight1");
        westWingLight1->SetPosition(-300.0f, -50.5f, -40.0f);
        westWingLight1->SetScale(24.0f, 2.0f, 11.0f);
        westWingLight1->SetEmergencyLight(true, 2.9f);
        CeilingLight* westWingLight2 = builder.Create<CeilingLight>("Stage1WestWingLight2");
        westWingLight2->SetPosition(-300.0f, -50.5f, 100.0f);
        westWingLight2->SetScale(24.0f, 2.0f, 11.0f);
        westWingLight2->SetEmergencyLight(false, 3.4f);
        CeilingLight* westWingLight3 = builder.Create<CeilingLight>("Stage1WestWingLight3");
        westWingLight3->SetPosition(-300.0f, -50.5f, 220.0f);
        westWingLight3->SetScale(20.0f, 2.0f, 9.0f);
        westWingLight3->SetEmergencyLight(false, 4.1f);

        // 中央の扉（ループ廊下へ続く）と、主電源の配電盤
        Door* door = builder.Create<Door>("Door");
        door->SetPosition(0.0f, -74.0f, 40.0f);
        FuseBox* fuseBox = builder.Create<FuseBox>("FuseBox");
        fuseBox->SetPosition(-180.0f, -90.0f, 35.0f);

        // 出口の送電盤（主電源が戻った後に操作する）
        FuseBox* exitPowerPanel =
            builder.Create<FuseBox>("ExitPowerPanel");
        exitPowerPanel->SetExitControl(true);
        exitPowerPanel->SetPosition(145.0f, -90.0f, 270.0f);

        // 非常用充電器（懐中電灯の電池を回復できる）
        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage1EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        emergencyCharger->SetPosition(-205.0f, -90.0f, -112.0f);
        emergencyCharger->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        // 任意で探索するごほうびは、最短の道筋から外して置いている。
        // 素早く脱出するか、全部探索するかを選ばせるためである。
        FuseBox* evidenceTerminal =
            builder.Create<FuseBox>("Stage1EvidenceTerminal");
        evidenceTerminal->SetManualControl("監視カメラを確認する");
        evidenceTerminal->SetManualInteractionAllowed(true);
        evidenceTerminal->SetPosition(205.0f, -90.0f, -42.0f);
        evidenceTerminal->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));
        // 監視カメラの端末の上に付けた、緑に光る目印
        Wall* evidenceMarker = createStageProp(
            "Stage1EvidenceMarker",
            Vector3(217.2f, -65.0f, -42.0f),
            Vector3(1.0f, 5.0f, 18.0f),
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.02f, 0.24f, 0.16f, 1.0f),
            36.0f,
            false);
        evidenceMarker->SetCastsShadow(false);


        // 1面の出口は、見える扉を調べて通ったときだけ成り立つようにしている。
        // 廊下を歩きながら操作キーを押すだけで、見えない出口が反応してしまう問題を防いでいる。
        Door* stageExitDoor = builder.Create<Door>("Stage1ExitDoor");
        stageExitDoor->SetPosition(202.0f, -74.0f, 307.5f);
        stageExitDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        stageExitDoor->SetScale(Vector3(60.0f, 50.0f, 4.0f));
        stageExitDoor->SetLocked(true);

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

        // 出口の上の赤い非常口の表示
        Wall* stageExitSign = createStageProp(
            "PropStage1ExitSign",
            Vector3(198.0f, -51.5f, 307.5f),
            Vector3(2.0f, 5.0f, 24.0f),
            Color(0.24f, 0.025f, 0.018f, 1.0f),
            Color(0.30f, 0.005f, 0.002f, 1.0f),
            30.0f,
            false);
        stageExitSign->SetCastsShadow(false);

        // 懐中電灯で照らしたときだけ浮かぶ壁の文字。進む道筋に沿って、目的の手がかりと不安を置いている。
        // 位置は壁の表面、向きは壁から通路側へ向かう法線。画像は横長(4:1)。
        const auto createWriting = [&builder](
            const char* name,
            const char* texture,
            const char* alternateTexture,
            const Vector3& surfaceCenter,
            const Vector3& outwardNormal,
            float width)
        {
            FlashlightWriting* writing = builder.Create<FlashlightWriting>(name);
            writing->SetTextures(texture, alternateTexture);
            writing->Place(surfaceCenter, outwardNormal, width, width * 0.25f);
            return writing;
        };
        objects.writings[Stage1WallWritings::Warning] = createWriting(
            "Stage1WritingWarning", "assets/texture/writing_warning.png", "",
            Vector3(0.0f, -71.0f, -178.0f), Vector3(0.0f, 0.0f, 1.0f), 56.0f);
        objects.writings[Stage1WallWritings::Three] = createWriting(
            "Stage1WritingThree", "assets/texture/writing_three.png", "",
            Vector3(-150.0f, -70.0f, -72.0f), Vector3(0.0f, 0.0f, -1.0f), 64.0f);
        objects.writings[Stage1WallWritings::Power] = createWriting(
            "Stage1WritingPower", "assets/texture/writing_power.png", "",
            Vector3(-128.0f, -69.0f, 38.0f), Vector3(0.0f, 0.0f, -1.0f), 64.0f);
        // ループ廊下を北へ進むと正面に見える突き当たりの壁。右へ曲がると目を離すことになる（その間に書き換わる）。
        objects.writings[Stage1WallWritings::Turn] = createWriting(
            "Stage1WritingTurn", "assets/texture/writing_turn.png",
            "assets/texture/writing_turned.png",
            Vector3(0.0f, -70.0f, 273.0f), Vector3(0.0f, 0.0f, -1.0f), 60.0f);

        // 暗証番号の扉（任意で探索）。中央の扉の先の細い廊下の東の壁にあり、ループ廊下へ向かうたびに目に入る。
        Door* keypadDoor = builder.Create<Door>("Stage1KeypadDoor");
        keypadDoor->SetPosition(45.0f, -74.0f, 110.0f);
        keypadDoor->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));
        keypadDoor->SetLocked(true);

        FuseBox* keypadPanel = builder.Create<FuseBox>("Stage1KeypadPanel");
        keypadPanel->SetManualControl("暗証番号を入力する");
        keypadPanel->SetManualInteractionAllowed(true);
        keypadPanel->SetPosition(41.5f, -80.0f, 135.0f);
        keypadPanel->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        // 番号の手がかり。「ひとつめ」などの文字の右に、その桁の数字を書いている。
        // 数字の画像は、番号を決めるStage1KeypadDoor::Initで差し替えている。
        const auto createDigitClue = [&builder](
            int order,
            const Vector3& surfaceCenter,
            const Vector3& outwardNormal)
        {
            const std::string number = std::to_string(order);
            FlashlightWriting* label = builder.Create<FlashlightWriting>(
                "Stage1KeypadClueLabel" + number);
            label->SetTextures("assets/texture/writing_order" + number + ".png");
            label->Place(surfaceCenter, outwardNormal, 40.0f, 10.0f);

            // 文字を正面から見たときの右方向。
            const Vector3 right(-outwardNormal.z, 0.0f, outwardNormal.x);
            FlashlightWriting* digit = builder.Create<FlashlightWriting>(
                "Stage1KeypadClueDigit" + number);
            digit->Place(surfaceCenter + right * 30.0f, outwardNormal, 20.0f, 20.0f);
            return digit;
        };
        objects.keypad.panel = keypadPanel;
        objects.keypad.door = keypadDoor;
        objects.keypad.digitWritings = {
            // 開始地点の東の壁（右の倉庫との仕切り）
            createDigitClue(1, Vector3(88.0f, -70.0f, -130.0f), Vector3(-1.0f, 0.0f, 0.0f)),
            // 左の倉庫の西の壁
            createDigitClue(2, Vector3(-218.0f, -70.0f, -155.0f), Vector3(1.0f, 0.0f, 0.0f)),
            // 中央ホールの南の壁
            createDigitClue(3, Vector3(120.0f, -70.0f, -68.0f), Vector3(0.0f, 0.0f, 1.0f)),
            // 右の倉庫の東の壁
            createDigitClue(4, Vector3(218.0f, -70.0f, -135.0f), Vector3(-1.0f, 0.0f, 0.0f)) };

        // 扉の先の部屋: 予備の電池と、入った者を迎える文字を置いている。
        BatteryItem* hiddenBattery = builder.Create<BatteryItem>("Stage1KeypadRoomBattery");
        hiddenBattery->SetPosition(140.0f, -95.0f, 110.0f);
        createWriting("Stage1KeypadRoomWriting", "assets/texture/writing_watched.png", "",
            Vector3(218.0f, -70.0f, 70.0f), Vector3(-1.0f, 0.0f, 0.0f), 64.0f);

        // 閉じ込めイベント（Stage1HiddenRoomEvent）で使う、鍵・記録端末・影。
        // 鍵は閉じ込めた瞬間に、部屋の隅の候補からランダムな位置へ置いている。
        KeyItem* hiddenRoomKey = builder.Create<KeyItem>("Stage1HiddenRoomKey");
        hiddenRoomKey->SetPosition(140.0f, -95.0f, 60.0f);
        hiddenRoomKey->SetActive(false);

        FuseBox* hiddenRoomRecord = builder.Create<FuseBox>("Stage1HiddenRoomRecord");
        hiddenRoomRecord->SetManualControl("記録を読む");
        hiddenRoomRecord->SetManualInteractionAllowed(true);
        hiddenRoomRecord->SetPosition(213.0f, -82.0f, 150.0f);
        hiddenRoomRecord->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

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

        // 出口（出口の扉の前。電力が戻るまでは調べられない）
        ExitTrigger* exit = builder.Create<ExitTrigger>("ExitTrigger");
        exit->SetPosition(207.0f, -80.0f, 307.5f);
        exit->SetNextScene(SceneName::Stage2);
        exit->SetInteractionEnabled(false);

        // 電池（書類保管室）
        BatteryItem* battery = builder.Create<BatteryItem>("BatteryItem");
        battery->SetPosition(-130.0f, -95.0f, 120.0f);

        // 驚かせる瞬間に重ねる画面のノイズ
        ScreenDustOverlay* crt =
            builder.Create<ScreenDustOverlay>("CRTNoise");

        crt->SetPower(0.7f);
        crt->SetActive(false);

        // 電力が戻った後、中央の廊下へ入ると、奥に影が現れる驚かせる演出
        ScareTrigger* corridorScare =
            builder.Create<ScareTrigger>("ScareTrigger_Corridor");
        corridorScare->SetPosition(Vector3(0.0f, -90.0f, 65.0f));
        corridorScare->SetSize(Vector3(70.0f, 30.0f, 32.0f));
        corridorScare->SetShadowPosition(Vector3(0.0f, -99.0f, 150.0f));
        corridorScare->SetRequiresPower(true);

        // 演出用の人影。どれも最初は隠しておき、StageSceneの演出で出している（時間切れで破棄せずに隠す）
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
        objects.firstFuse = item1;
        objects.secondFuse = item2;
        objects.thirdFuse = item3;
        objects.ceilingLights = {
            light1, light2, light3, light4, light5, light6, light7, light8 };
        return objects;
    }
}
