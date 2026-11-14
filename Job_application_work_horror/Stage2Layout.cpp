// ============================================================================
// ファイルの役割: 2面のループ廊下に置くObject（壁・照明・端末・異変用の小物）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ============================================================================

#include "Stage2Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CaptureMode.h"
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
#include "HangingBulb.h"
#include "LayoutExtras.h"
#include "PropLayoutFile.h"
#include "utility.h"

#include <SimpleMath.h>
#include <functional>

using namespace DirectX::SimpleMath;

namespace Stage2Layout
{
    // 2面の廊下（幅84・長さ300のまっすぐな廊下）を組み立て、進行で使うObjectのポインタを入れて返している
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle)
    {
        Stage2Objects objects;
        // 廊下の壁・天井・縁取り・配管（prop 行）と、下で作る扉・照明・電池・端末などの置き場所（place 行）は、
        // テキストファイル（assets/layout/stage2_props.txt）から読んでいる（PropLayoutFile。1面と同じ書き方）
        const PropLayout propLayout = PropLayoutFile::Load(PropLayoutPath);
        if (!propLayout.IsValid())
        {
            utility::ReportFatalError("2面の配置を読めませんでした（" + std::to_string(propLayout.ErrorLine) +
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
        SceneLayoutBuilder builder(game, objects.objectNames);

        // プレイヤー（廊下の入口側。2面は最後の追跡まで走れない）
        Player* player = builder.Create<Player>("Player");
        PropLayoutFile::ApplyPlacement(*player, at("Player"));
        player->SetSprintAllowed(false);

        // 謎解きと最後の追跡で使う影（最初は隠している）
        ShadowMan* stageShadow = builder.Create<ShadowMan>("Stage2Shadow");
        place(*stageShadow, "Stage2Shadow");
        stageShadow->SetDeactivateOnExpire(true);
        stageShadow->SetActive(false);

        // 謎解き用の影と分け、足音だけに反応する追跡者を別に管理している。
        ShadowMan* noiseShadow = builder.Create<ShadowMan>("Stage2NoiseShadow");
        place(*noiseShadow, "Stage2NoiseShadow");
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
        place(*confirmationPanel, "Stage2ConfirmationPanel");

        // 非常用充電器（入口側の左の壁）
        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage2EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        place(*emergencyCharger, "Stage2EmergencyCharger");

        // 残された記録の端末（2台）
        FuseBox* evidenceTerminal1 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal1");
        evidenceTerminal1->SetManualControl("残された記録を回収する");
        evidenceTerminal1->SetManualInteractionAllowed(true);
        place(*evidenceTerminal1, "Stage2EvidenceTerminal1");

        FuseBox* evidenceTerminal2 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal2");
        evidenceTerminal2->SetManualControl("残された記録を回収する");
        evidenceTerminal2->SetManualInteractionAllowed(true);
        place(*evidenceTerminal2, "Stage2EvidenceTerminal2");

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
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            FuseBox* signalTerminal =
                builder.Create<FuseBox>(signalTerminalNames[signalIndex]);
            signalTerminal->SetManualControl(signalPrompts[signalIndex]);
            signalTerminal->SetManualInteractionAllowed(false);
            place(*signalTerminal, signalTerminalNames[signalIndex]);
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

        // 置き場所（位置と大きさ）を配置のファイルの place 行から読んで、Wall を作る関数
        const auto createPlacedWall = [&createWall, &at, &bind](const char* name, const Color& diffuse, bool collision)
        {
            const PropPlacement& placement = at(name);
            Wall* wall = createWall(name, placement.Position, placement.Size, diffuse, collision);
            bind(name, [wall](const PropPlacement& moved)
            {
                wall->SetPosition(moved.Position.x, moved.Position.y, moved.Position.z);
                wall->SetScale(moved.Size.x, moved.Size.y, moved.Size.z);
                wall->SetVisualModel(wall->GetVisualModel());
            });
            return wall;
        };

        // 縁取りの色（時計・偽の扉の枠にも使う）
        const Color trimColor(0.075f, 0.068f, 0.06f, 1.0f);

        // 廊下の左右の壁・奥の壁・出口の扉の左右の壁・天井・床際の縁取り・天井近くの配管（prop 行）。
        // room を付けた壁は、部屋の角の暗がりの計算用に、上から見た長方形を記録している
        objects.layoutProps = PropLayoutFile::Spawn(builder, propLayout);
        objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(propLayout);

        // 床は1面と同じ濡れた床（Ground）にし、タイルの継ぎ目・汚れ・水たまりの反射と波を描いている。
        // 水たまりは安全な近道と静かな回り道の間に置き、上を通るほど（走るほど）敵へ音が伝わるようにしている
        // （足音が水音になるのは、Groundの水たまりの判定を Player が使っているため）。
        Ground* ground = builder.Create<Ground>("Stage2Ground");
        place(*ground, "Stage2Ground");
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


        // 残された記録の端末の上に付けた、緑に光る目印
        Wall* evidenceMarker1 = createPlacedWall("Stage2EvidenceMarker1", Color(0.025f, 0.11f, 0.09f, 1.0f), false);
        evidenceMarker1->SetAppearance(
            Color(0.025f, 0.11f, 0.09f, 1.0f),
            Color(0.012f, 0.13f, 0.09f, 1.0f), 36.0f);
        evidenceMarker1->SetCastsShadow(false);
        Wall* evidenceMarker2 = createPlacedWall("Stage2EvidenceMarker2", Color(0.025f, 0.11f, 0.09f, 1.0f), false);
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
            Wall* marker = createPlacedWall(signalMarkerNames[signalIndex], signalDiffuse[signalIndex], false);
            marker->SetAppearance(
                signalDiffuse[signalIndex], signalEmission[signalIndex], 48.0f);
            marker->SetSignalSurface(true);
            marker->SetCastsShadow(false);
            objects.signalMarkers[signalIndex] = marker;
        }

        // 肖像画と、目が開いたときに出す赤い目（最初は隠している）
        Wall* portrait = createPlacedWall("Stage2Portrait", Color(0.075f, 0.058f, 0.044f, 1.0f), false);
        // 古い油絵のように、ニスの弱い光沢を返している
        portrait->SetAppearance(Color(0.075f, 0.058f, 0.044f, 1.0f), Color(0.0f, 0.0f, 0.0f, 1.0f), 18.0f);
        // 絵は、古い油絵の人物をシェーダーで描いている（目は閉じていて、異変では絵の中で目が開き、見る人を目で追う）
        portrait->SetPortraitSurface();
        portrait->SetCastsShadow(false);
        // 肖像画の額縁（くすんだ金色の4本）。時計の枠と同じく、壁から少し浮かせている。
        // PBRでは金箔の金属として描くので、色は金の映り込みの色（黒ずんだ分だけ暗く）にし、埃の膜の分だけ金属らしさを下げている
        const Color portraitFrameColor(0.52f, 0.38f, 0.17f, 1.0f);
        const char* portraitFrames[] =
        {
            "Stage2PortraitFrameTop", "Stage2PortraitFrameBottom", "Stage2PortraitFrameNear", "Stage2PortraitFrameFar"
        };
        for (const char* frame : portraitFrames)
        {
            Wall* piece = createPlacedWall(frame, portraitFrameColor, false);
            piece->SetAppearance(portraitFrameColor, Color(0.0f, 0.0f, 0.0f, 1.0f), 26.0f);
            piece->SetSurfaceFinish(0.75f, 0.45f);
            piece->SetCastsShadow(false);
            // 見た目は、溝の入った金の額縁の棒（Mayaで作ったモデル）
            piece->SetVisualModel("frame_rail");
        }

        // 時計（文字盤と枠）。文字盤・目盛り・ローマ数字・針・ガラスは、文字盤のシェーダー（clockPS）で描いている
        Wall* clockFace = createPlacedWall("Stage2ClockFace", Color(0.095f, 0.086f, 0.070f, 1.0f), false);
        Wall* clockFrameTop = createPlacedWall("Stage2ClockFrameTop", trimColor, false);
        Wall* clockFrameBottom = createPlacedWall("Stage2ClockFrameBottom", trimColor, false);
        Wall* clockFrameNear = createPlacedWall("Stage2ClockFrameNear", trimColor, false);
        Wall* clockFrameFar = createPlacedWall("Stage2ClockFrameFar", trimColor, false);
        Wall* clockPieces[] =
        {
            clockFace, clockFrameTop, clockFrameBottom,
            clockFrameNear, clockFrameFar
        };
        for (Wall* piece : clockPieces)
        {
            piece->SetCastsShadow(false);
        }
        // 時計の枠の見た目は、鏡や肖像画と同じ溝の入った額縁の棒（Mayaで作ったモデル）
        for (Wall* frame : { clockFrameTop, clockFrameBottom, clockFrameNear, clockFrameFar })
        {
            frame->SetVisualModel("frame_rail");
        }
        clockFace->SetClockSurface();
        // 文字盤はガラスで覆われているので、なめらかな面の映り込みにしている（PBR）
        clockFace->SetSurfaceFinish(0.0f, 0.12f);
        clockFace->SetClockHands(clockHourAngle, clockMinuteAngle);

        // 左の壁の古い鏡（どの周回にもある）。鏡の面は廊下の側（+x）を向き、MirrorAnomaly の面の位置・大きさと合わせている。
        // 映る景色は、鏡のシェーダー（mirrorPS）が、鏡で反射した光線を部屋の形・小物の箱・画面の中（SSR）へ飛ばして求めている。
        Wall* mirror = createWall(
            "Stage2Mirror", Vector3(MirrorAnomaly::SurfaceX - 0.3f, MirrorAnomaly::CenterY, MirrorAnomaly::CenterZ),
            Vector3(0.6f, MirrorAnomaly::HalfHeight * 2.0f, MirrorAnomaly::HalfWidth * 2.0f),
            Color(0.5f, 0.5f, 0.5f, 1.0f), false);
        mirror->SetMirrorSurface();
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
            // 古いニスの木の枠（PBR：金属でなく、少し艶のある粗さ）
            piece->SetSurfaceFinish(0.0f, 0.48f);
            piece->SetCastsShadow(false);
            // 見た目は、溝の入った木の額縁の棒（Mayaで作ったモデル。当たり判定はない）
            piece->SetVisualModel("frame_rail");
        }
        // 鏡にだけ映る自分の姿（一人称で体がないため、位置と向きを鏡のシェーダーへ渡し、鏡の中にだけ体を描いている）
        objects.mirrorSelf = builder.Create<MirrorSelf>("Stage2MirrorSelf");
        objects.mirror = mirror;

        // 周回によって現れる赤い印（最初は隠している）
        Wall* loopMark = createPlacedWall("Stage2LoopMark", Color(0.22f, 0.01f, 0.006f, 1.0f), false);
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
            Wall* cycleMark = createPlacedWall(
                cycleMarkNames[markIndex], Color(0.18f, 0.004f, 0.002f, 1.0f), false);
            cycleMark->SetRotation(
                Vector3(cycleMarkRotations[markIndex], 0.0f, 0.0f));
            cycleMark->SetCastsShadow(false);
            cycleMark->SetVisible(false);
            objects.cycleMarks[markIndex] = cycleMark;
        }

        // 出口の扉の上のランプ（開くまでは赤）
        Wall* doorIndicator = createPlacedWall("Stage2DoorIndicator", Color(0.24f, 0.012f, 0.008f, 1.0f), false);
        doorIndicator->SetCastsShadow(false);
        doorIndicator->SetAppearance(
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(0.18f, 0.001f, 0.0f, 1.0f), 24.0f);
        // 見た目は、光る表示面を金属の枠で囲んだ表示灯（Mayaで作ったモデル。光るのは表示面だけ）
        doorIndicator->SetVisualModel("door_indicator");

        // 偽の扉（板・枠・取っ手）。異変の周回だけ表示している。板は、本物の扉と見分けがつかないよう、
        // 羽目板・木目・剥げた塗装・部屋番号の札・鍵穴をシェーダー（doorPS）で描いている
        Wall* falseDoorPanel = createPlacedWall("Stage2FalseDoorPanel", Color(0.07f, 0.025f, 0.018f, 1.0f), false);
        Wall* falseDoorFrameNear = createPlacedWall("Stage2FalseDoorFrameNear", trimColor, false);
        Wall* falseDoorFrameFar = createPlacedWall("Stage2FalseDoorFrameFar", trimColor, false);
        Wall* falseDoorFrameTop = createPlacedWall("Stage2FalseDoorFrameTop", trimColor, false);
        Wall* falseDoorHandle = createPlacedWall("Stage2FalseDoorHandle", Color(0.16f, 0.09f, 0.025f, 1.0f), false);
        // 偽の扉の枠の見た目は、額縁と同じ溝の入った棒（Mayaで作ったモデル）
        falseDoorFrameNear->SetVisualModel("frame_rail");
        falseDoorFrameFar->SetVisualModel("frame_rail");
        falseDoorFrameTop->SetVisualModel("frame_rail");
        falseDoorPanel->SetDoorSurface();
        falseDoorPanel->SetSurfaceParams(Color(-1.0f, 0.0f, 0.0f, 0.0f));
        // 取っ手は、くすんだ真鍮の金属にしている（PBR。色は真鍮の映り込みの色）。板は本物の扉と同じ木の粗さ
        falseDoorHandle->SetAppearance(Color(0.72f, 0.50f, 0.20f, 1.0f), Color(0.0f, 0.0f, 0.0f, 1.0f), 40.0f);
        falseDoorHandle->SetSurfaceFinish(0.85f, 0.36f);
        falseDoorPanel->SetSurfaceFinish(0.0f, 0.58f);
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
        for (int index = 0; index < Stage2ScratchCount; ++index)
        {
            Wall* scratch = createPlacedWall(
                Stage2ScratchNames[index],
                Color(0.16f, 0.006f, 0.003f, 1.0f),
                false);
            scratch->SetCastsShadow(false);
            scratch->SetVisible(false);
            objects.scratches[index] = scratch;
        }

        // 棚と、その上の電池（進行に合わせて出している）
        Wall* batteryShelf = createPlacedWall("Stage2BatteryShelf", Color(0.07f, 0.055f, 0.045f, 1.0f),
            false);
        batteryShelf->SetCastsShadow(false);
        BatteryItem* battery = builder.Create<BatteryItem>("Stage2Battery");
        place(*battery, "Stage2Battery");
        battery->SetActive(false);

        // 天井照明（4つ。出口の扉の上の照明は1面と同じ名前"CeilingLight4"にしている）
        CeilingLight* light1 = builder.Create<CeilingLight>("Stage2Light1");
        place(*light1, "Stage2Light1");
        light1->SetEmergencyLight(false, 0.2f);
        CeilingLight* light2 = builder.Create<CeilingLight>("Stage2Light2");
        place(*light2, "Stage2Light2");
        light2->SetEmergencyLight(false, 1.4f);
        CeilingLight* light3 = builder.Create<CeilingLight>("Stage2Light3");
        place(*light3, "Stage2Light3");
        light3->SetEmergencyLight(false, 2.8f);
        CeilingLight* light4 = builder.Create<CeilingLight>("CeilingLight4");
        place(*light4, "CeilingLight4");
        light4->SetEmergencyLight(false, 4.1f);
        // 廊下の中ほどに、天井から吊るした裸電球を2つ下げている（すきま風でゆっくり揺れ、異変や影が近いときに大きく揺れる）
        // 吊るす付け根の位置は、配置のファイルの place Stage2HangingBulb1・2 から読んでいる
        for (std::size_t index = 0; index < objects.hangingBulbs.size(); ++index)
        {
            const char* name = index == 0 ? "Stage2HangingBulb1" : "Stage2HangingBulb2";
            HangingBulb* bulb = builder.Create<HangingBulb>(name);
            const float seed = 1.3f + static_cast<float>(index) * 2.1f;
            bulb->Place(at(name).Position, seed);
            bind(name, [bulb, seed](const PropPlacement& moved) { bulb->Place(moved.Position, seed); });
            objects.hangingBulbs[index] = bulb;
        }

        // 2面の廊下は、P.T. の廊下のような暖かい白熱電球の光にしている（1面は古い蛍光灯のまま）
        for (CeilingLight* bulb : { light1, light2, light3, light4 })
        {
            bulb->SetWarmBulb(true);
        }
        // 配置ファイルの ExtraLight_・ExtraBattery_ で始まる place（何個でも置ける照明と電池。Unity で足したり消したりできる）
        const LayoutExtras::Spawned extras = LayoutExtras::Spawn(builder, propLayout, objects.placementBindings);
        for (CeilingLight* bulb : extras.Lights)
        {
            bulb->SetWarmBulb(true);
        }

        // 出口の扉と、その先の出口。最後のループで扉を抜けると、階段を上って3面へ移る（Stage2Scene が階段の演出を始める）。
        // 自動撮影では、道順の時間がずれないよう、階段の演出をせず、これまでどおり調べてリザルト画面へ移る
        Door* door = builder.Create<Door>("Stage2Door");
        place(*door, "Stage2Door");
        door->ResetClosed(0);

        const bool capture = Tools::CaptureMode::IsActive();
        ExitTrigger* exit = builder.Create<ExitTrigger>("Stage2Exit");
        place(*exit, "Stage2Exit");
        exit->SetNextScene(capture ? SceneName::Result : SceneName::Stage3);
        exit->SetStairClimb(!capture);
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
        // 本体（当たり判定のある箱）の位置と大きさ、扉の中心の位置と正面の向きは、配置のファイルの place 行から読んでいる
        const auto createLocker = [&builder, &createPlacedWall, &lockerColor, &at, &bind](const char* name, const char* bodyName)
        {
            // 本体の見た目は、上の通気口・脚・継ぎ目のあるロッカー（Mayaで作ったモデル。当たり判定は箱のまま）
            createPlacedWall(bodyName, lockerColor, true)->SetVisualModel("locker_body");
            Locker* locker = builder.Create<Locker>(name);
            const PropPlacement& door = at(name);
            locker->Place(door.Position, door.Yaw);
            bind(name, [locker](const PropPlacement& moved) { locker->Place(moved.Position, moved.Yaw); });
            return locker;
        };
        objects.lockers = {
            createLocker("Stage2LockerLeft", "Stage2LockerLeftBody"),
            createLocker("Stage2LockerRight", "Stage2LockerRightBody") };
        return objects;
    }
}
