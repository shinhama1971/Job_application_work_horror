// ============================================================================
// ファイルの役割: 2面のループ廊下に置くObject（壁・照明・端末・異変用の小物）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ============================================================================

#include "Stage2Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"
#include "Locker.h"
#include "MirrorAnomaly.h"
#include "MirrorSelf.h"

#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

namespace Stage2Layout
{
    // 2面の廊下（幅84・長さ300のまっすぐな廊下）を組み立て、進行で使うObjectのポインタを入れて返している
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle)
    {
        Stage2Objects objects;
        SceneLayoutBuilder builder(game, objects.objectNames);

        // プレイヤー（廊下の入口側。2面は最後の追跡まで走れない）
        Player* player = builder.Create<Player>("Player");
        player->SetPosition(Vector3(0.0f, -99.0f, -125.0f));
        player->SetSprintAllowed(false);

        // 謎解きと最後の追跡で使う影（最初は隠している）
        ShadowMan* stageShadow = builder.Create<ShadowMan>("Stage2Shadow");
        stageShadow->SetPosition(0.0f, -99.0f, 62.0f);
        stageShadow->SetDeactivateOnExpire(true);
        stageShadow->SetActive(false);

        // 謎解き用の影と分け、足音だけに反応する追跡者を別に管理している。
        ShadowMan* noiseShadow = builder.Create<ShadowMan>("Stage2NoiseShadow");
        noiseShadow->SetPosition(0.0f, -99.0f, -145.0f);
        noiseShadow->SetDeactivateOnExpire(true);
        noiseShadow->SetActive(false);

        // 2周目以降、視界の外から静かに近づく「背後の気配」。
        ShadowMan* presence = builder.Create<ShadowMan>("Stage2Presence");
        presence->SetDeactivateOnExpire(true);
        presence->SetActive(false);

        // 異常確認のスイッチ（出口の扉の横。異変を見つけた後に押せるようになる）
        FuseBox* confirmationPanel =
            builder.Create<FuseBox>("Stage2ConfirmationPanel");
        confirmationPanel->SetManualControl("異常確認スイッチを押す");
        confirmationPanel->SetManualInteractionAllowed(false);
        confirmationPanel->SetPosition(35.5f, -90.0f, 112.0f);
        confirmationPanel->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));

        // 非常用充電器（入口側の左の壁）
        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage2EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        emergencyCharger->SetPosition(-35.5f, -90.0f, -106.0f);
        emergencyCharger->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        // 残された記録の端末（2台）
        FuseBox* evidenceTerminal1 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal1");
        evidenceTerminal1->SetManualControl("残された記録を回収する");
        evidenceTerminal1->SetManualInteractionAllowed(true);
        evidenceTerminal1->SetPosition(35.5f, -90.0f, -76.0f);
        evidenceTerminal1->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));

        FuseBox* evidenceTerminal2 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal2");
        evidenceTerminal2->SetManualControl("残された記録を回収する");
        evidenceTerminal2->SetManualInteractionAllowed(true);
        evidenceTerminal2->SetPosition(-35.5f, -90.0f, 108.0f);
        evidenceTerminal2->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        // 信号盤（青・黄・赤）。3周目に、この順番で操作する
        constexpr const char* signalTerminalNames[] =
        {
            "Stage2SignalTerminalBlue",
            "Stage2SignalTerminalAmber",
            "Stage2SignalTerminalRed"
        };
        constexpr const char* signalPrompts[] =
        {
            "青い信号盤を操作する",
            "黄色い信号盤を操作する",
            "赤い信号盤を操作する"
        };
        const Vector3 signalPositions[] =
        {
            Vector3(35.5f, -90.0f, 82.0f),
            Vector3(-35.5f, -90.0f, 18.0f),
            Vector3(35.5f, -90.0f, -108.0f)
        };
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            FuseBox* signalTerminal =
                builder.Create<FuseBox>(signalTerminalNames[signalIndex]);
            signalTerminal->SetManualControl(signalPrompts[signalIndex]);
            signalTerminal->SetManualInteractionAllowed(false);
            signalTerminal->SetPosition(
                signalPositions[signalIndex].x,
                signalPositions[signalIndex].y,
                signalPositions[signalIndex].z);
            signalTerminal->SetRotation(Vector3(
                0.0f,
                signalIndex == 1 ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2,
                0.0f));
            objects.signalTerminals[signalIndex] = signalTerminal;
        }

        // Wallを、色と当たり判定の有無を指定して作る関数
        const auto createWall = [&builder](
            const char* name,
            const Vector3& position,
            const Vector3& scale,
            const Color& diffuse,
            bool collision)
        {
            Wall* wall = builder.Create<Wall>(name);
            wall->SetPosition(position.x, position.y, position.z);
            wall->SetScale(scale.x, scale.y, scale.z);
            wall->SetAppearance(diffuse, Color(0.0f, 0.0f, 0.0f, 1.0f), 7.0f);
            wall->SetCollisionEnabled(collision);
            return wall;
        };

        // 壁・縁取り・天井の色。壁は1面より少し暖かい色にし、古い住まいの廊下らしくしている
        const Color wallColor(0.31f, 0.295f, 0.265f, 1.0f);
        const Color trimColor(0.075f, 0.068f, 0.06f, 1.0f);
        const Color ceilingColor(0.06f, 0.058f, 0.054f, 1.0f);

        // 廊下の左右の壁と、奥の壁、出口の扉の左右の壁。
        // 1面の建物の壁と同じく、パネルの継ぎ目・ひび・水の跡・カビを描き（SetWeatheringSurface）、
        // 部屋の角の暗がりの計算用に、上から見た長方形を記録している
        const auto createCorridorWall = [&createWall, &objects, &wallColor](
            const char* name, const Vector3& position, const Vector3& scale)
        {
            Wall* wall = createWall(name, position, scale, wallColor, true);
            wall->SetAppearance(wallColor, Color(0.0f, 0.0f, 0.0f, 1.0f), 4.0f);
            wall->SetWeatheringSurface(true);
            objects.wallFootprints.emplace_back(position.x, position.z, scale.x * 0.5f, scale.z * 0.5f);
            return wall;
        };
        createCorridorWall("Stage2WallLeft", Vector3(-42.0f, -74.0f, -10.0f), Vector3(4.0f, 50.0f, 300.0f));
        createCorridorWall("Stage2WallRight", Vector3(42.0f, -74.0f, -10.0f), Vector3(4.0f, 50.0f, 300.0f));
        createCorridorWall("Stage2WallBack", Vector3(0.0f, -74.0f, -160.0f), Vector3(88.0f, 50.0f, 4.0f));
        createCorridorWall("Stage2WallFrontLeft", Vector3(-28.5f, -74.0f, 140.0f), Vector3(27.0f, 50.0f, 4.0f));
        createCorridorWall("Stage2WallFrontRight", Vector3(28.5f, -74.0f, 140.0f), Vector3(27.0f, 50.0f, 4.0f));

        // 床は1面と同じ濡れた床（Ground）にし、タイルの継ぎ目・汚れ・水たまりの反射と波を描いている。
        // 水たまりは安全な近道と静かな回り道の間に置き、上を通るほど（走るほど）敵へ音が伝わるようにしている
        // （足音が水音になるのは、Groundの水たまりの判定を Player が使っているため）。
        Ground* ground = builder.Create<Ground>("Stage2Ground");
        ground->SetPosition(0.0f, -100.0f, 0.0f);
        ground->SetScale(20.0f, 1.0f, 20.0f);
        // 水たまりのマス目（82x82）を41ずらし、廊下の中心（x=0）と z=-82・0・82 にマス目の中心が来るようにしている。
        // 形の値（0〜1）で、横のずれ（0なら左へ9、1なら右へ9）と奥行きのずれ・幅を決め、片側に回り道を残している。
        WaterEffectSystem::PuddleLayout puddleLayout;
        puddleLayout.GridOffset = Vector2(41.0f, 41.0f);
        puddleLayout.Cells = {
            Vector4(0.0f, -1.0f, 0.0f, 0.5f),   // 入口側：中心(-9, -82)。右側を回れる
            Vector4(0.0f, 0.0f, 1.0f, 1.0f),    // 中央：中心(9, 9)。大きく、左側を回れる
            Vector4(0.0f, 1.0f, 0.0f, 1.0f) };  // 出口側：中心(-9, 91)。右側を回れる
        ground->SetPuddleLayout(puddleLayout);
        objects.ground = ground;

        // 天井（1面と同じく、天井板・外れた板の穴・雨漏りの染みを描いている）、床際の縁取り、天井近くの配管
        Wall* ceiling = createWall("Stage2Ceiling", Vector3(0.0f, -47.0f, -10.0f),
            Vector3(84.0f, 3.0f, 300.0f), ceilingColor, false);
        ceiling->SetAppearance(ceilingColor, Color(0.0f, 0.0f, 0.0f, 1.0f), 4.0f);
        ceiling->SetWeatheringSurface(true);
        ceiling->SetCastsShadow(false);

        createWall("Stage2TrimLeft", Vector3(-39.4f, -96.0f, -10.0f),
            Vector3(1.0f, 6.0f, 292.0f), trimColor, false);
        createWall("Stage2TrimRight", Vector3(39.4f, -96.0f, -10.0f),
            Vector3(1.0f, 6.0f, 292.0f), trimColor, false);

        Wall* pipeLeft = createWall("Stage2PipeLeft", Vector3(-34.0f, -53.0f, -10.0f),
            Vector3(3.0f, 3.0f, 282.0f), trimColor, false);
        pipeLeft->SetCastsShadow(false);
        Wall* pipeRight = createWall("Stage2PipeRight", Vector3(34.0f, -53.0f, -10.0f),
            Vector3(3.0f, 3.0f, 282.0f), trimColor, false);
        pipeRight->SetCastsShadow(false);

        // 残された記録の端末の上に付けた、緑に光る目印
        Wall* evidenceMarker1 = createWall(
            "Stage2EvidenceMarker1", Vector3(39.4f, -65.0f, -76.0f),
            Vector3(1.0f, 5.0f, 18.0f),
            Color(0.025f, 0.11f, 0.09f, 1.0f), false);
        evidenceMarker1->SetAppearance(
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.012f, 0.13f, 0.09f, 1.0f), 36.0f);
        evidenceMarker1->SetCastsShadow(false);
        Wall* evidenceMarker2 = createWall(
            "Stage2EvidenceMarker2", Vector3(-39.4f, -65.0f, 108.0f),
            Vector3(1.0f, 5.0f, 18.0f),
            Color(0.025f, 0.11f, 0.09f, 1.0f), false);
        evidenceMarker2->SetAppearance(
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.012f, 0.13f, 0.09f, 1.0f), 36.0f);
        evidenceMarker2->SetCastsShadow(false);

        // 信号盤の上の、色付きで光る目印（信号の面として、シェーダーで模様を描いている）
        const char* signalMarkerNames[] =
        {
            "Stage2SignalMarkerBlue",
            "Stage2SignalMarkerAmber",
            "Stage2SignalMarkerRed"
        };
        const Color signalDiffuse[] =
        {
            Color(0.015f, 0.055f, 0.13f, 1.0f),
            Color(0.13f, 0.075f, 0.012f, 1.0f),
            Color(0.13f, 0.018f, 0.012f, 1.0f)
        };
        const Color signalEmission[] =
        {
            Color(0.015f, 0.18f, 0.48f, 1.0f),
            Color(0.40f, 0.17f, 0.008f, 1.0f),
            Color(0.42f, 0.018f, 0.008f, 1.0f)
        };
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            const bool leftWall = signalIndex == 1;
            Wall* marker = createWall(
                signalMarkerNames[signalIndex],
                Vector3(leftWall ? -39.4f : 39.4f, -72.0f,
                    signalPositions[signalIndex].z),
                Vector3(1.0f, 17.0f, 18.0f),
                signalDiffuse[signalIndex], false);
            marker->SetAppearance(
                signalDiffuse[signalIndex], signalEmission[signalIndex], 48.0f);
            marker->SetSignalSurface(true);
            marker->SetCastsShadow(false);
            objects.signalMarkers[signalIndex] = marker;
        }

        // 肖像画と、目が開いたときに出す赤い目（最初は隠している）
        Wall* portrait = createWall("Stage2Portrait", Vector3(39.4f, -70.0f, -25.0f),
            Vector3(1.0f, 22.0f, 16.0f), Color(0.075f, 0.058f, 0.044f, 1.0f), false);
        // 古い油絵のように、ニスの弱い光沢を返している
        portrait->SetAppearance(Color(0.075f, 0.058f, 0.044f, 1.0f), Color(0.0f, 0.0f, 0.0f, 1.0f), 18.0f);
        // 絵は、古い油絵の人物をシェーダーで描いている（目は閉じていて、異変では絵の中で目が開き、見る人を目で追う）
        portrait->SetPortraitSurface();
        portrait->SetCastsShadow(false);
        // 肖像画の額縁（くすんだ金色の4本）。時計の枠と同じく、壁から少し浮かせている
        const Color portraitFrameColor(0.15f, 0.11f, 0.055f, 1.0f);
        const struct { const char* Name; Vector3 Position; Vector3 Scale; } portraitFrames[] =
        {
            { "Stage2PortraitFrameTop", Vector3(39.1f, -58.0f, -25.0f), Vector3(1.8f, 2.0f, 20.0f) },
            { "Stage2PortraitFrameBottom", Vector3(39.1f, -82.0f, -25.0f), Vector3(1.8f, 2.0f, 20.0f) },
            { "Stage2PortraitFrameNear", Vector3(39.1f, -70.0f, -34.0f), Vector3(1.8f, 26.0f, 2.0f) },
            { "Stage2PortraitFrameFar", Vector3(39.1f, -70.0f, -16.0f), Vector3(1.8f, 26.0f, 2.0f) },
        };
        for (const auto& frame : portraitFrames)
        {
            Wall* piece = createWall(frame.Name, frame.Position, frame.Scale, portraitFrameColor, false);
            piece->SetAppearance(portraitFrameColor, Color(0.0f, 0.0f, 0.0f, 1.0f), 26.0f);
            piece->SetCastsShadow(false);
        }

        // 時計（文字盤と枠）。文字盤・目盛り・ローマ数字・針・ガラスは、文字盤のシェーダー（clockPS）で描いている
        Wall* clockFace = createWall(
            "Stage2ClockFace", Vector3(-39.3f, -70.0f, -25.0f),
            Vector3(1.0f, 20.0f, 20.0f),
            Color(0.095f, 0.086f, 0.070f, 1.0f), false);
        Wall* clockFrameTop = createWall(
            "Stage2ClockFrameTop", Vector3(-38.9f, -59.0f, -25.0f),
            Vector3(1.8f, 2.0f, 24.0f), trimColor, false);
        Wall* clockFrameBottom = createWall(
            "Stage2ClockFrameBottom", Vector3(-38.9f, -81.0f, -25.0f),
            Vector3(1.8f, 2.0f, 24.0f), trimColor, false);
        Wall* clockFrameNear = createWall(
            "Stage2ClockFrameNear", Vector3(-38.9f, -70.0f, -36.0f),
            Vector3(1.8f, 24.0f, 2.0f), trimColor, false);
        Wall* clockFrameFar = createWall(
            "Stage2ClockFrameFar", Vector3(-38.9f, -70.0f, -14.0f),
            Vector3(1.8f, 24.0f, 2.0f), trimColor, false);
        Wall* clockPieces[] =
        {
            clockFace, clockFrameTop, clockFrameBottom,
            clockFrameNear, clockFrameFar
        };
        for (Wall* piece : clockPieces)
        {
            piece->SetCastsShadow(false);
        }
        clockFace->SetClockSurface();
        clockFace->SetClockHands(clockHourAngle, clockMinuteAngle);

        // 左の壁の古い鏡（どの周回にもある）。鏡の面は廊下の側（+x）を向き、MirrorAnomaly の面の位置・大きさと合わせている。
        // 映る景色は Game::Draw が鏡の面で反転したカメラから描き、鏡のシェーダー（mirrorPS）が読んでいる。
        Wall* mirror = createWall(
            "Stage2Mirror", Vector3(MirrorAnomaly::SurfaceX - 0.3f, MirrorAnomaly::CenterY, MirrorAnomaly::CenterZ),
            Vector3(0.6f, MirrorAnomaly::HalfHeight * 2.0f, MirrorAnomaly::HalfWidth * 2.0f),
            Color(0.5f, 0.5f, 0.5f, 1.0f), false);
        mirror->SetMirrorSurface(Vector3(1.0f, 0.0f, 0.0f));
        mirror->SetCastsShadow(false);
        // 鏡の枠（黒ずんだ木の4本）。鏡の面より少し手前へ出し、剥げた縁を半分隠している
        const Color mirrorFrameColor(0.065f, 0.048f, 0.034f, 1.0f);
        const float frameX = MirrorAnomaly::SurfaceX + 0.3f;
        const float frameTop = MirrorAnomaly::CenterY + MirrorAnomaly::HalfHeight;
        const float frameBottom = MirrorAnomaly::CenterY - MirrorAnomaly::HalfHeight;
        const float frameNear = MirrorAnomaly::CenterZ - MirrorAnomaly::HalfWidth;
        const float frameFar = MirrorAnomaly::CenterZ + MirrorAnomaly::HalfWidth;
        const struct { const char* Name; Vector3 Position; Vector3 Scale; } mirrorFrames[] =
        {
            { "Stage2MirrorFrameTop", Vector3(frameX, frameTop, MirrorAnomaly::CenterZ),
                Vector3(1.8f, 2.0f, MirrorAnomaly::HalfWidth * 2.0f + 4.0f) },
            { "Stage2MirrorFrameBottom", Vector3(frameX, frameBottom, MirrorAnomaly::CenterZ),
                Vector3(1.8f, 2.0f, MirrorAnomaly::HalfWidth * 2.0f + 4.0f) },
            { "Stage2MirrorFrameNear", Vector3(frameX, MirrorAnomaly::CenterY, frameNear),
                Vector3(1.8f, MirrorAnomaly::HalfHeight * 2.0f, 2.0f) },
            { "Stage2MirrorFrameFar", Vector3(frameX, MirrorAnomaly::CenterY, frameFar),
                Vector3(1.8f, MirrorAnomaly::HalfHeight * 2.0f, 2.0f) },
        };
        for (const auto& frame : mirrorFrames)
        {
            Wall* piece = createWall(frame.Name, frame.Position, frame.Scale, mirrorFrameColor, false);
            piece->SetAppearance(mirrorFrameColor, Color(0.0f, 0.0f, 0.0f, 1.0f), 14.0f);
            piece->SetCastsShadow(false);
        }
        // 鏡の中にだけ映る人影（鏡の異変の周回だけ表示する）。位置は MirrorAnomaly が決め、プレイヤーの方を向くだけにしている
        ShadowMan* mirrorFigure = builder.Create<ShadowMan>("Stage2MirrorFigure");
        mirrorFigure->SetExternallyControlled(true);
        mirrorFigure->SetMirrorOnly(true);
        mirrorFigure->SetScale(Vector3(MirrorAnomaly::FigureScaleXZ, MirrorAnomaly::FigureScaleY, MirrorAnomaly::FigureScaleXZ));
        mirrorFigure->SetActive(false);
        // 鏡にだけ映る自分の姿（一人称で体がないため、鏡を描く間だけ体を描いている）
        objects.mirrorSelf = builder.Create<MirrorSelf>("Stage2MirrorSelf");
        objects.mirror = mirror;
        objects.mirrorFigure = mirrorFigure;

        // 周回によって現れる赤い印（最初は隠している）
        Wall* loopMark = createWall("Stage2LoopMark", Vector3(-39.4f, -68.0f, 52.0f),
            Vector3(1.0f, 18.0f, 12.0f), Color(0.22f, 0.01f, 0.006f, 1.0f), false);
        loopMark->SetCastsShadow(false);
        loopMark->SetVisible(false);

        // 入口側の右の壁に、周回の数だけ現れる3本の傷の印
        const char* cycleMarkNames[] =
        {
            "Stage2CycleMark1",
            "Stage2CycleMark2",
            "Stage2CycleMark3"
        };
        constexpr float cycleMarkRotations[] =
        {
            0.22f, -0.18f, 0.12f
        };
        for (int markIndex = 0; markIndex < 3; ++markIndex)
        {
            Wall* cycleMark = createWall(
                cycleMarkNames[markIndex],
                Vector3(
                    39.45f,
                    -69.0f,
                    -112.0f + static_cast<float>(markIndex) * 8.0f),
                Vector3(1.0f, 20.0f, 2.0f),
                Color(0.18f, 0.004f, 0.002f, 1.0f),
                false);
            cycleMark->SetRotation(
                Vector3(cycleMarkRotations[markIndex], 0.0f, 0.0f));
            cycleMark->SetCastsShadow(false);
            cycleMark->SetVisible(false);
            objects.cycleMarks[markIndex] = cycleMark;
        }

        // 出口の扉の上のランプ（開くまでは赤）
        Wall* doorIndicator = createWall("Stage2DoorIndicator",
            Vector3(22.0f, -67.0f, 137.4f), Vector3(10.0f, 5.0f, 1.0f),
            Color(0.24f, 0.012f, 0.008f, 1.0f), false);
        doorIndicator->SetCastsShadow(false);
        doorIndicator->SetAppearance(
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(0.18f, 0.001f, 0.0f, 1.0f), 24.0f);

        // 偽の扉（板・枠・取っ手）。異変の周回だけ表示している。板は、本物の扉と見分けがつかないよう、
        // 羽目板・木目・剥げた塗装・部屋番号の札・鍵穴をシェーダー（doorPS）で描いている
        Wall* falseDoorPanel = createWall(
            "Stage2FalseDoorPanel", Vector3(-39.3f, -76.0f, 70.0f),
            Vector3(1.2f, 40.0f, 22.0f),
            Color(0.07f, 0.025f, 0.018f, 1.0f), false);
        Wall* falseDoorFrameNear = createWall(
            "Stage2FalseDoorFrameNear", Vector3(-39.0f, -76.0f, 58.0f),
            Vector3(1.8f, 44.0f, 2.0f), trimColor, false);
        Wall* falseDoorFrameFar = createWall(
            "Stage2FalseDoorFrameFar", Vector3(-39.0f, -76.0f, 82.0f),
            Vector3(1.8f, 44.0f, 2.0f), trimColor, false);
        Wall* falseDoorFrameTop = createWall(
            "Stage2FalseDoorFrameTop", Vector3(-39.0f, -54.0f, 70.0f),
            Vector3(1.8f, 2.0f, 26.0f), trimColor, false);
        Wall* falseDoorHandle = createWall(
            "Stage2FalseDoorHandle", Vector3(-38.3f, -76.0f, 62.0f),
            Vector3(2.0f, 5.0f, 2.0f),
            Color(0.16f, 0.09f, 0.025f, 1.0f), false);
        falseDoorPanel->SetDoorSurface();
        falseDoorPanel->SetSurfaceParams(Color(-1.0f, 0.0f, 0.0f, 0.0f));
        // 取っ手は、くすんだ真鍮の光沢にしている
        falseDoorHandle->SetAppearance(Color(0.30f, 0.21f, 0.08f, 1.0f), Color(0.0f, 0.0f, 0.0f, 1.0f), 40.0f);
        Wall* falseDoorPieces[] =
        {
            falseDoorPanel, falseDoorFrameNear, falseDoorFrameFar,
            falseDoorFrameTop, falseDoorHandle
        };
        for (Wall* piece : falseDoorPieces)
        {
            piece->SetCastsShadow(false);
            piece->SetVisible(false);
        }

        // 壁の引っかき傷（周回が進むほど増えていく）
        struct ScratchPiece
        {
            Vector3 Position;
            Vector3 Scale;
        };
        const ScratchPiece scratchPieces[] =
        {
            { Vector3(39.45f, -71.0f,   1.0f), Vector3(1.0f, 18.0f, 2.0f) },
            { Vector3(39.45f, -71.0f,   9.0f), Vector3(1.0f, 18.0f, 2.0f) },
            { Vector3(39.45f, -71.0f,   5.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -71.0f,  20.0f), Vector3(1.0f, 18.0f, 2.0f) },
            { Vector3(39.45f, -63.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -71.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -79.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -71.0f,  36.0f), Vector3(1.0f, 18.0f, 2.0f) },
            { Vector3(39.45f, -79.0f,  41.0f), Vector3(1.0f,  2.0f, 12.0f) },
            { Vector3(39.45f, -71.0f,  52.0f), Vector3(1.0f, 18.0f, 2.0f) },
            { Vector3(39.45f, -63.0f,  57.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -71.0f,  57.0f), Vector3(1.0f,  2.0f, 10.0f) },
            { Vector3(39.45f, -67.0f,  62.0f), Vector3(1.0f, 10.0f, 2.0f) }
        };
        for (int index = 0; index < Stage2ScratchCount; ++index)
        {
            Wall* scratch = createWall(
                Stage2ScratchNames[index],
                scratchPieces[index].Position,
                scratchPieces[index].Scale,
                Color(0.16f, 0.006f, 0.003f, 1.0f),
                false);
            scratch->SetCastsShadow(false);
            scratch->SetVisible(false);
            objects.scratches[index] = scratch;
        }

        // 棚と、その上の電池（進行に合わせて出している）
        Wall* batteryShelf = createWall(
            "Stage2BatteryShelf",
            Vector3(35.8f, -87.0f, -45.0f),
            Vector3(8.0f, 2.0f, 10.0f),
            Color(0.07f, 0.055f, 0.045f, 1.0f),
            false);
        batteryShelf->SetCastsShadow(false);
        BatteryItem* battery = builder.Create<BatteryItem>("Stage2Battery");
        battery->SetPosition(35.0f, -82.5f, -45.0f);
        battery->SetActive(false);

        // 天井照明（4つ。出口の扉の上の照明は1面と同じ名前"CeilingLight4"にしている）
        CeilingLight* light1 = builder.Create<CeilingLight>("Stage2Light1");
        light1->SetPosition(0.0f, -50.5f, -112.0f);
        light1->SetScale(18.0f, 2.0f, 8.0f);
        light1->SetEmergencyLight(false, 0.2f);
        CeilingLight* light2 = builder.Create<CeilingLight>("Stage2Light2");
        light2->SetPosition(0.0f, -50.5f, -38.0f);
        light2->SetScale(18.0f, 2.0f, 8.0f);
        light2->SetEmergencyLight(false, 1.4f);
        CeilingLight* light3 = builder.Create<CeilingLight>("Stage2Light3");
        light3->SetPosition(0.0f, -50.5f, 38.0f);
        light3->SetScale(18.0f, 2.0f, 8.0f);
        light3->SetEmergencyLight(false, 2.8f);
        CeilingLight* light4 = builder.Create<CeilingLight>("CeilingLight4");
        light4->SetPosition(0.0f, -50.5f, 112.0f);
        light4->SetScale(18.0f, 2.0f, 8.0f);
        light4->SetEmergencyLight(false, 4.1f);
        // 2面の廊下は、P.T. の廊下のような暖かい白熱電球の光にしている（1面は古い蛍光灯のまま）
        for (CeilingLight* bulb : { light1, light2, light3, light4 })
        {
            bulb->SetWarmBulb(true);
        }

        // 出口の扉と、その先の出口（調べるとリザルト画面へ）
        Door* door = builder.Create<Door>("Stage2Door");
        door->SetPosition(0.0f, -74.0f, 140.0f);
        door->ResetClosed(0);

        ExitTrigger* exit = builder.Create<ExitTrigger>("Stage2Exit");
        exit->SetPosition(0.0f, -80.0f, 153.0f);
        exit->SetNextScene(SceneName::Result);
        exit->SetInteractionEnabled(false);

        objects.player = player;
        objects.exit = exit;
        objects.door = door;
        objects.shadow = stageShadow;
        objects.noiseShadow = noiseShadow;
        objects.presence = presence;
        objects.confirmationPanel = confirmationPanel;
        objects.emergencyCharger = emergencyCharger;
        objects.battery = battery;
        objects.doorIndicator = doorIndicator;
        objects.portrait = portrait;
        objects.loopMark = loopMark;
        objects.clockFace = clockFace;
        objects.falseDoorPanel = falseDoorPanel;
        objects.falseDoorFrameNear = falseDoorFrameNear;
        objects.falseDoorFrameFar = falseDoorFrameFar;
        objects.falseDoorFrameTop = falseDoorFrameTop;
        objects.falseDoorHandle = falseDoorHandle;
        objects.lights = { light1, light2, light3, light4 };
        objects.evidenceTerminals = { evidenceTerminal1, evidenceTerminal2 };
        objects.evidenceMarkers = { evidenceMarker1, evidenceMarker2 };

        // 隠れられるロッカー。本体は当たり判定のある箱（Wall）、扉と「隠れる」操作はLockerが担当している。
        // 時計・偽の扉・肖像画・端末・信号盤・電池と重ならない位置に、左右1つずつ置いている。
        const Color lockerColor(0.16f, 0.19f, 0.17f, 1.0f);
        const auto createLocker = [&builder, &createWall, &lockerColor](
            const char* name,
            const char* bodyName,
            float wallSideX,
            float z)
        {
            // wallSideXは壁側の符号（左の壁は-1、右の壁は+1）。扉は廊下の中央を向いている。
            createWall(bodyName, Vector3(wallSideX * 35.0f, -77.0f, z),
                Vector3(10.0f, 44.0f, 16.0f), lockerColor, true);
            Locker* locker = builder.Create<Locker>(name);
            locker->Place(Vector3(wallSideX * 29.95f, -77.0f, z),
                wallSideX < 0.0f ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2);
            return locker;
        };
        objects.lockers = {
            createLocker("Stage2LockerLeft", "Stage2LockerLeftBody", -1.0f, -62.0f),
            createLocker("Stage2LockerRight", "Stage2LockerRightBody", 1.0f, 40.0f) };
        return objects;
    }
}
