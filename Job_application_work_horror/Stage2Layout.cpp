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
#include <array>
#include <functional>
#include <iterator>
#include <utility>

using namespace DirectX::SimpleMath;

namespace
{
    // --- 2面の配置の値
    // 光らない物の発光色と、色だけを決めて作る壁の光沢（くすんだ塗装）
    const Color NoEmission(0.0f, 0.0f, 0.0f, 1.0f);
    constexpr float PlainWallShininess = 7.0f;
    // 縁取りの色（時計・偽の扉の枠にも使う）
    const Color TrimColor(0.075f, 0.068f, 0.06f, 1.0f);

    // 水たまりのマス目（82x82）のずらし。41ずらし、廊下の中心（x=0）と z=-82・0・82 にマス目の中心が来るようにしている
    const Vector2 PuddleGridOffset(41.0f, 41.0f);

    // 残された記録の端末の上の、緑に光る目印
    const Color EvidenceMarkerDiffuse(0.025f, 0.11f, 0.09f, 1.0f);
    const Color EvidenceMarkerEmission(0.012f, 0.13f, 0.09f, 1.0f);
    constexpr float EvidenceMarkerShininess = 36.0f;

    // 信号盤（青・黄・赤）。3周目に、この順番で操作する。名前・操作の案内・上の目印の名前と色
    struct SignalSpec
    {
        const char* TerminalName;
        const char* Prompt;
        const char* MarkerName;
        Color MarkerDiffuse;
        Color MarkerEmission;
    };
    const SignalSpec Signals[] =
    {
        { "Stage2SignalTerminalBlue", "青い信号盤を操作する", "Stage2SignalMarkerBlue",
            Color(0.015f, 0.055f, 0.13f, 1.0f), Color(0.015f, 0.18f, 0.48f, 1.0f) },
        { "Stage2SignalTerminalAmber", "黄色い信号盤を操作する", "Stage2SignalMarkerAmber",
            Color(0.13f, 0.075f, 0.012f, 1.0f), Color(0.40f, 0.17f, 0.008f, 1.0f) },
        { "Stage2SignalTerminalRed", "赤い信号盤を操作する", "Stage2SignalMarkerRed",
            Color(0.13f, 0.018f, 0.012f, 1.0f), Color(0.42f, 0.018f, 0.008f, 1.0f) },
    };
    constexpr float SignalMarkerShininess = 48.0f;

    // 肖像画（古い油絵のニスの弱い光沢）
    const Color PortraitColor(0.075f, 0.058f, 0.044f, 1.0f);
    constexpr float PortraitShininess = 18.0f;
    // 肖像画の額縁（くすんだ金色）。PBRでは金箔の金属として描くので、色は金の映り込みの色（黒ずんだ分だけ暗く）にし、
    // 埃の膜の分だけ金属らしさを下げている
    const Color PortraitFrameColor(0.52f, 0.38f, 0.17f, 1.0f);
    constexpr float PortraitFrameShininess = 26.0f;
    constexpr float PortraitFrameMetallic = 0.75f;
    constexpr float PortraitFrameRoughness = 0.45f;

    // 時計の文字盤の地の色と、覆っているガラスのなめらかさ（PBR）
    const Color ClockFaceColor(0.095f, 0.086f, 0.070f, 1.0f);
    constexpr float ClockGlassMetallic = 0.0f;
    constexpr float ClockGlassRoughness = 0.12f;

    // 左の壁の古い鏡：鏡の板の厚み（面を壁の側へ半分ずらす）と、映り込みの計算の地の色
    constexpr float MirrorThickness = 0.6f;
    const Color MirrorColor(0.5f, 0.5f, 0.5f, 1.0f);
    // 鏡の枠（黒ずんだ木）：鏡の面より手前へ出す距離、枠の棒の奥行き・太さ、上下の棒を左右へはみ出させる長さ
    const Color MirrorFrameColor(0.065f, 0.048f, 0.034f, 1.0f);
    constexpr float MirrorFrameOffset = 0.3f;
    constexpr float MirrorFrameDepth = 1.8f;
    constexpr float MirrorFrameWidth = 2.0f;
    constexpr float MirrorFrameOverhang = 4.0f;
    constexpr float MirrorFrameShininess = 14.0f;
    // 古いニスの木の枠（PBR：金属でなく、少し艶のある粗さ）
    constexpr float MirrorFrameMetallic = 0.0f;
    constexpr float MirrorFrameRoughness = 0.48f;

    // 周回によって現れる赤い印と、入口側の右の壁の3本の傷の印（傷ごとに少し傾けている）
    const Color LoopMarkColor(0.22f, 0.01f, 0.006f, 1.0f);
    const Color CycleMarkColor(0.18f, 0.004f, 0.002f, 1.0f);
    constexpr const char* CycleMarkNames[] = { "Stage2CycleMark1", "Stage2CycleMark2", "Stage2CycleMark3" };
    constexpr float CycleMarkRotations[] = { 0.22f, -0.18f, 0.12f };

    // 出口の扉の上のランプ（開くまでは赤）
    const Color DoorIndicatorDiffuse(0.24f, 0.012f, 0.008f, 1.0f);
    const Color DoorIndicatorEmission(0.18f, 0.001f, 0.0f, 1.0f);
    constexpr float DoorIndicatorShininess = 24.0f;

    // 偽の扉：板の色、取っ手の色（作るときの色と、くすんだ真鍮の映り込みの色）、光沢となめらかさ
    const Color FalseDoorPanelColor(0.07f, 0.025f, 0.018f, 1.0f);
    const Color FalseDoorHandleColor(0.16f, 0.09f, 0.025f, 1.0f);
    const Color FalseDoorBrassColor(0.72f, 0.50f, 0.20f, 1.0f);
    constexpr float FalseDoorHandleShininess = 40.0f;
    constexpr float FalseDoorHandleMetallic = 0.85f;
    constexpr float FalseDoorHandleRoughness = 0.36f;
    // 板は本物の扉と同じ木の粗さ
    constexpr float FalseDoorPanelMetallic = 0.0f;
    constexpr float FalseDoorPanelRoughness = 0.58f;
    // 扉の絵の鍵穴を描く側（負は見て左。最初は左の壁に置くため。異変で置き直すときは Stage2ScenePuzzles が決め直す）
    constexpr float FalseDoorKeyholeLeft = -1.0f;

    // 壁の引っかき傷と、電池を載せる棚
    const Color ScratchColor(0.16f, 0.006f, 0.003f, 1.0f);
    const Color BatteryShelfColor(0.07f, 0.055f, 0.045f, 1.0f);

    // 天井照明（4つ。出口の扉の上の照明は1面と同じ名前"CeilingLight4"にしている）と、点滅の周期をずらす値
    constexpr std::pair<const char*, float> CeilingLights[] =
    {
        { "Stage2Light1", 0.2f },
        { "Stage2Light2", 1.4f },
        { "Stage2Light3", 2.8f },
        { "CeilingLight4", 4.1f },
    };
    // 吊るした裸電球の揺れ方を決める値（1つ目の値と、電球ごとにずらす幅。2つが同じ揺れ方にならないように）
    constexpr const char* HangingBulbNames[] = { "Stage2HangingBulb1", "Stage2HangingBulb2" };
    constexpr float HangingBulbSeedBase = 1.3f;
    constexpr float HangingBulbSeedStep = 2.1f;

    // 隠れられるロッカーの本体の色
    const Color LockerColor(0.16f, 0.19f, 0.17f, 1.0f);

    // 2面の廊下を作る手順をまとめている。置き場所の読み方（配置のファイル）と、読み直したときの置き直し方を、手順のあいだで共有している
    class Stage2Builder final
    {
    public:
        Stage2Builder(Core::Game& game, Stage2Objects& objects, const PropLayout& layout)
            : m_Builder(game, objects.objectNames), m_Objects(objects), m_Layout(layout)
        {
        }

        // プレイヤー（廊下の入口側。2面は最後の追跡まで走れない）と、影たち（どれも最初は隠している）
        void BuildPlayerAndShadows()
        {
            m_Objects.player = m_Builder.Create<Player>("Player");
            PropLayoutFile::ApplyPlacement(*m_Objects.player, At("Player"));
            m_Objects.player->SetSprintAllowed(false);

            // 謎解きと最後の追跡で使う影
            m_Objects.shadow = CreateHiddenShadow("Stage2Shadow");
            Place(*m_Objects.shadow, "Stage2Shadow");
            // 謎解き用の影と分け、足音だけに反応する追跡者を別に管理している。
            m_Objects.noiseShadow = CreateHiddenShadow("Stage2NoiseShadow");
            Place(*m_Objects.noiseShadow, "Stage2NoiseShadow");
            // 2周目以降、視界の外から静かに近づく「背後の気配」。
            m_Objects.presence = CreateHiddenShadow("Stage2Presence");
        }

        // 異常確認のスイッチ・非常用充電器・残された記録の端末（2台）・信号盤（3台）
        void BuildTerminals()
        {
            // 異常確認のスイッチ（出口の扉の横。異変を見つけた後に押せるようになる）
            m_Objects.confirmationPanel = CreateTerminal("Stage2ConfirmationPanel", "異常確認スイッチを押す", false);
            // 非常用充電器（入口側の左の壁）
            m_Objects.emergencyCharger = CreateTerminal("Stage2EmergencyCharger", "非常用充電器を使う", true);
            m_Objects.evidenceTerminals = {
                CreateTerminal("Stage2EvidenceTerminal1", "残された記録を回収する", true),
                CreateTerminal("Stage2EvidenceTerminal2", "残された記録を回収する", true) };
            static_assert(std::size(Signals) == std::tuple_size_v<decltype(m_Objects.signalTerminals)>,
                "信号盤の数をそろえる");
            for (std::size_t signalIndex = 0; signalIndex < std::size(Signals); ++signalIndex)
            {
                m_Objects.signalTerminals[signalIndex] =
                    CreateTerminal(Signals[signalIndex].TerminalName, Signals[signalIndex].Prompt, false);
            }
        }

        // 廊下の左右の壁・奥の壁・出口の扉の左右の壁・天井・床際の縁取り・天井近くの配管（prop 行）と、床
        void BuildLayoutPropsAndGround()
        {
            // room を付けた壁は、部屋の角の暗がりの計算用に、上から見た長方形を記録している
            m_Objects.layoutProps = PropLayoutFile::Spawn(m_Builder, m_Layout);
            m_Objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(m_Layout);

            // 床は1面と同じ濡れた床（Ground）にし、タイルの継ぎ目・汚れ・水たまりの反射と波を描いている。
            // 水たまりは安全な近道と静かな回り道の間に置き、上を通るほど（走るほど）敵へ音が伝わるようにしている
            // （足音が水音になるのは、Groundの水たまりの判定を Player が使っているため）。
            Ground* ground = m_Builder.Create<Ground>("Stage2Ground");
            Place(*ground, "Stage2Ground");
            // 形の値（0〜1）で、横のずれ（0なら左へ9、1なら右へ9）と奥行きのずれ・幅を決め、片側に回り道を残している。
            WaterEffectSystem::PuddleLayout puddleLayout;
            puddleLayout.GridOffset = PuddleGridOffset;
            puddleLayout.Cells = {
                Vector4(0.0f, -1.0f, 0.0f, 0.5f),   // 入口側：中心(-9, -82)。右側を回れる
                Vector4(0.0f, 0.0f, 1.0f, 1.0f),    // 中央：中心(9, 9)。大きく、左側を回れる
                Vector4(0.0f, 1.0f, 0.0f, 1.0f) };  // 出口側：中心(-9, 91)。右側を回れる
            ground->SetPuddleLayout(puddleLayout);
            m_Objects.ground = ground;
            // 周回ごとに増える天井の雨漏りの位置と、広がりきったときの水たまりの大きさ（size の幅 = 直径）
            constexpr const char* loopLeakNames[Stage2FinalLoopCount] = { "Stage2LoopLeak1", "Stage2LoopLeak2", "Stage2LoopLeak3" };
            for (std::size_t leakIndex = 0; leakIndex < m_Objects.loopLeaks.size(); ++leakIndex)
            {
                const PropPlacement& leak = At(loopLeakNames[leakIndex]);
                m_Objects.loopLeaks[leakIndex] = Vector3(leak.Position.x, leak.Position.z, leak.Size.x * 0.5f);
            }
        }

        // 残された記録の端末の上の緑の目印と、信号盤の上の色付きで光る目印（信号の面として、シェーダーで模様を描いている）
        void BuildTerminalMarkers()
        {
            const auto createEvidenceMarker = [this](const char* name)
            {
                Wall* marker = CreatePlacedWall(name, EvidenceMarkerDiffuse, false);
                marker->SetAppearance(EvidenceMarkerDiffuse, EvidenceMarkerEmission, EvidenceMarkerShininess);
                marker->SetCastsShadow(false);
                return marker;
            };
            m_Objects.evidenceMarkers = {
                createEvidenceMarker("Stage2EvidenceMarker1"),
                createEvidenceMarker("Stage2EvidenceMarker2") };

            for (std::size_t signalIndex = 0; signalIndex < std::size(Signals); ++signalIndex)
            {
                const SignalSpec& signal = Signals[signalIndex];
                Wall* marker = CreatePlacedWall(signal.MarkerName, signal.MarkerDiffuse, false);
                marker->SetAppearance(signal.MarkerDiffuse, signal.MarkerEmission, SignalMarkerShininess);
                marker->SetSignalSurface(true);
                marker->SetCastsShadow(false);
                m_Objects.signalMarkers[signalIndex] = marker;
            }
        }

        // 肖像画と額縁
        void BuildPortrait()
        {
            Wall* portrait = CreatePlacedWall("Stage2Portrait", PortraitColor, false);
            // 古い油絵のように、ニスの弱い光沢を返している
            portrait->SetAppearance(PortraitColor, NoEmission, PortraitShininess);
            // 絵は、古い油絵の人物をシェーダーで描いている（目は閉じていて、異変では絵の中で目が開き、見る人を目で追う）
            portrait->SetPortraitSurface();
            portrait->SetCastsShadow(false);
            m_Objects.portrait = portrait;
            // 肖像画の額縁（くすんだ金色の4本）。時計の枠と同じく、壁から少し浮かせている。
            for (const char* frame : { "Stage2PortraitFrameTop", "Stage2PortraitFrameBottom", "Stage2PortraitFrameNear", "Stage2PortraitFrameFar" })
            {
                Wall* piece = CreatePlacedWall(frame, PortraitFrameColor, false);
                piece->SetAppearance(PortraitFrameColor, NoEmission, PortraitFrameShininess);
                piece->SetSurfaceFinish(PortraitFrameMetallic, PortraitFrameRoughness);
                piece->SetCastsShadow(false);
                // 見た目は、溝の入った金の額縁の棒（Mayaで作ったモデル）
                piece->SetVisualModel("frame_rail");
            }
        }

        // 時計（文字盤と枠）。文字盤・目盛り・ローマ数字・針・ガラスは、文字盤のシェーダー（clockPS）で描いている
        void BuildClock(float hourAngle, float minuteAngle)
        {
            Wall* clockFace = CreatePlacedWall("Stage2ClockFace", ClockFaceColor, false);
            clockFace->SetCastsShadow(false);
            // 時計の枠の見た目は、鏡や肖像画と同じ溝の入った額縁の棒（Mayaで作ったモデル）
            std::array<Wall*, 4> frames{};
            const char* frameNames[] = { "Stage2ClockFrameTop", "Stage2ClockFrameBottom", "Stage2ClockFrameNear", "Stage2ClockFrameFar" };
            for (std::size_t index = 0; index < frames.size(); ++index)
            {
                frames[index] = CreatePlacedWall(frameNames[index], TrimColor, false);
            }
            for (Wall* frame : frames)
            {
                frame->SetCastsShadow(false);
                frame->SetVisualModel("frame_rail");
            }
            clockFace->SetClockSurface();
            // 文字盤はガラスで覆われているので、なめらかな面の映り込みにしている（PBR）
            clockFace->SetSurfaceFinish(ClockGlassMetallic, ClockGlassRoughness);
            clockFace->SetClockHands(hourAngle, minuteAngle);
            m_Objects.clockFace = clockFace;
        }

        // 左の壁の古い鏡（どの周回にもある）。鏡の面は廊下の側（+x）を向き、MirrorAnomaly の面の位置・大きさと合わせている。
        // 映る景色は、鏡のシェーダー（mirrorPS）が、鏡で反射した光線を部屋の形・小物の箱・画面の中（SSR）へ飛ばして求めている。
        void BuildMirror()
        {
            Wall* mirror = CreateWall(
                "Stage2Mirror", Vector3(MirrorAnomaly::SurfaceX - MirrorThickness * 0.5f, MirrorAnomaly::CenterY, MirrorAnomaly::CenterZ),
                Vector3(MirrorThickness, MirrorAnomaly::HalfHeight * 2.0f, MirrorAnomaly::HalfWidth * 2.0f),
                MirrorColor, false);
            mirror->SetMirrorSurface();
            mirror->SetCastsShadow(false);
            // 鏡の枠（黒ずんだ木の4本）。鏡の面より少し手前へ出し、剥げた縁を半分隠している
            const float frameX = MirrorAnomaly::SurfaceX + MirrorFrameOffset;
            const float frameTop = MirrorAnomaly::CenterY + MirrorAnomaly::HalfHeight;
            const float frameBottom = MirrorAnomaly::CenterY - MirrorAnomaly::HalfHeight;
            const float frameNear = MirrorAnomaly::CenterZ - MirrorAnomaly::HalfWidth;
            const float frameFar = MirrorAnomaly::CenterZ + MirrorAnomaly::HalfWidth;
            const Vector3 horizontalBar(MirrorFrameDepth, MirrorFrameWidth, MirrorAnomaly::HalfWidth * 2.0f + MirrorFrameOverhang);
            const Vector3 verticalBar(MirrorFrameDepth, MirrorAnomaly::HalfHeight * 2.0f, MirrorFrameWidth);
            const struct { const char* Name; Vector3 Position; Vector3 Scale; } mirrorFrames[] =
            {
                { "Stage2MirrorFrameTop", Vector3(frameX, frameTop, MirrorAnomaly::CenterZ), horizontalBar },
                { "Stage2MirrorFrameBottom", Vector3(frameX, frameBottom, MirrorAnomaly::CenterZ), horizontalBar },
                { "Stage2MirrorFrameNear", Vector3(frameX, MirrorAnomaly::CenterY, frameNear), verticalBar },
                { "Stage2MirrorFrameFar", Vector3(frameX, MirrorAnomaly::CenterY, frameFar), verticalBar },
            };
            for (const auto& frame : mirrorFrames)
            {
                Wall* piece = CreateWall(frame.Name, frame.Position, frame.Scale, MirrorFrameColor, false);
                piece->SetAppearance(MirrorFrameColor, NoEmission, MirrorFrameShininess);
                piece->SetSurfaceFinish(MirrorFrameMetallic, MirrorFrameRoughness);
                piece->SetCastsShadow(false);
                // 見た目は、溝の入った木の額縁の棒（Mayaで作ったモデル。当たり判定はない）
                piece->SetVisualModel("frame_rail");
            }
            // 鏡にだけ映る自分の姿（一人称で体がないため、位置と向きを鏡のシェーダーへ渡し、鏡の中にだけ体を描いている）
            m_Objects.mirrorSelf = m_Builder.Create<MirrorSelf>("Stage2MirrorSelf");
            m_Objects.mirror = mirror;
        }

        // 周回によって現れる赤い印と、入口側の右の壁に周回の数だけ現れる3本の傷の印（どれも最初は隠している）
        void BuildLoopMarks()
        {
            Wall* loopMark = CreatePlacedWall("Stage2LoopMark", LoopMarkColor, false);
            loopMark->SetCastsShadow(false);
            loopMark->SetVisible(false);
            m_Objects.loopMark = loopMark;

            static_assert(std::size(CycleMarkNames) == std::tuple_size_v<decltype(m_Objects.cycleMarks)>, "傷の印の数をそろえる");
            for (std::size_t markIndex = 0; markIndex < std::size(CycleMarkNames); ++markIndex)
            {
                Wall* cycleMark = CreatePlacedWall(CycleMarkNames[markIndex], CycleMarkColor, false);
                cycleMark->SetRotation(Vector3(CycleMarkRotations[markIndex], 0.0f, 0.0f));
                cycleMark->SetCastsShadow(false);
                cycleMark->SetVisible(false);
                m_Objects.cycleMarks[markIndex] = cycleMark;
            }
        }

        // 出口の扉の上のランプ（開くまでは赤）
        void BuildDoorIndicator()
        {
            Wall* doorIndicator = CreatePlacedWall("Stage2DoorIndicator", DoorIndicatorDiffuse, false);
            doorIndicator->SetCastsShadow(false);
            doorIndicator->SetAppearance(DoorIndicatorDiffuse, DoorIndicatorEmission, DoorIndicatorShininess);
            // 見た目は、光る表示面を金属の枠で囲んだ表示灯（Mayaで作ったモデル。光るのは表示面だけ）
            doorIndicator->SetVisualModel("door_indicator");
            m_Objects.doorIndicator = doorIndicator;
        }

        // 偽の扉（板・枠・取っ手）。異変の周回だけ表示している。板は、本物の扉と見分けがつかないよう、
        // 羽目板・木目・剥げた塗装・部屋番号の札・鍵穴をシェーダー（doorPS）で描いている
        void BuildFalseDoor()
        {
            Wall* panel = CreatePlacedWall("Stage2FalseDoorPanel", FalseDoorPanelColor, false);
            Wall* frameNear = CreatePlacedWall("Stage2FalseDoorFrameNear", TrimColor, false);
            Wall* frameFar = CreatePlacedWall("Stage2FalseDoorFrameFar", TrimColor, false);
            Wall* frameTop = CreatePlacedWall("Stage2FalseDoorFrameTop", TrimColor, false);
            Wall* handle = CreatePlacedWall("Stage2FalseDoorHandle", FalseDoorHandleColor, false);
            // 偽の扉の枠の見た目は、額縁と同じ溝の入った棒（Mayaで作ったモデル）
            for (Wall* frame : { frameNear, frameFar, frameTop })
            {
                frame->SetVisualModel("frame_rail");
            }
            panel->SetDoorSurface();
            panel->SetSurfaceParams(Color(FalseDoorKeyholeLeft, 0.0f, 0.0f, 0.0f));
            // 取っ手は、くすんだ真鍮の金属にしている（PBR。色は真鍮の映り込みの色）
            handle->SetAppearance(FalseDoorBrassColor, NoEmission, FalseDoorHandleShininess);
            handle->SetSurfaceFinish(FalseDoorHandleMetallic, FalseDoorHandleRoughness);
            panel->SetSurfaceFinish(FalseDoorPanelMetallic, FalseDoorPanelRoughness);
            for (Wall* piece : { panel, frameNear, frameFar, frameTop, handle })
            {
                piece->SetCastsShadow(false);
                piece->SetVisible(false);
            }
            m_Objects.falseDoorPanel = panel;
            m_Objects.falseDoorFrameNear = frameNear;
            m_Objects.falseDoorFrameFar = frameFar;
            m_Objects.falseDoorFrameTop = frameTop;
            m_Objects.falseDoorHandle = handle;
        }

        // 壁の引っかき傷（周回が進むほど増えていく）と、棚とその上の電池（進行に合わせて出している）
        void BuildScratchesAndBattery()
        {
            for (int index = 0; index < Stage2ScratchCount; ++index)
            {
                Wall* scratch = CreatePlacedWall(Stage2ScratchNames[index], ScratchColor, false);
                scratch->SetCastsShadow(false);
                scratch->SetVisible(false);
                m_Objects.scratches[index] = scratch;
            }

            Wall* batteryShelf = CreatePlacedWall("Stage2BatteryShelf", BatteryShelfColor, false);
            batteryShelf->SetCastsShadow(false);
            BatteryItem* battery = m_Builder.Create<BatteryItem>("Stage2Battery");
            Place(*battery, "Stage2Battery");
            battery->SetActive(false);
            m_Objects.battery = battery;
        }

        // 天井照明・吊るした裸電球・配置ファイルの ExtraLight_・ExtraBattery_ で始まる照明と電池
        void BuildLights()
        {
            static_assert(std::size(CeilingLights) == std::tuple_size_v<decltype(m_Objects.lights)>, "天井照明の数をそろえる");
            for (std::size_t index = 0; index < std::size(CeilingLights); ++index)
            {
                const auto& [name, flickerOffset] = CeilingLights[index];
                CeilingLight* light = m_Builder.Create<CeilingLight>(name);
                Place(*light, name);
                light->SetEmergencyLight(false, flickerOffset);
                m_Objects.lights[index] = light;
            }
            // 廊下の中ほどに、天井から吊るした裸電球を2つ下げている（すきま風でゆっくり揺れ、異変や影が近いときに大きく揺れる）
            // 吊るす付け根の位置は、配置のファイルの place Stage2HangingBulb1・2 から読んでいる
            static_assert(std::size(HangingBulbNames) == std::tuple_size_v<decltype(m_Objects.hangingBulbs)>, "裸電球の数をそろえる");
            for (std::size_t index = 0; index < m_Objects.hangingBulbs.size(); ++index)
            {
                const char* name = HangingBulbNames[index];
                HangingBulb* bulb = m_Builder.Create<HangingBulb>(name);
                const float seed = HangingBulbSeedBase + static_cast<float>(index) * HangingBulbSeedStep;
                bulb->Place(At(name).Position, seed);
                Bind(name, [bulb, seed](const PropPlacement& moved) { bulb->Place(moved.Position, seed); });
                m_Objects.hangingBulbs[index] = bulb;
            }

            // 2面の廊下は、P.T. の廊下のような暖かい白熱電球の光にしている（1面は古い蛍光灯のまま）
            for (CeilingLight* bulb : m_Objects.lights)
            {
                bulb->SetWarmBulb(true);
            }
            // 何個でも置ける照明と電池（Unity で足したり消したりできる）
            const LayoutExtras::Spawned extras = LayoutExtras::Spawn(m_Builder, m_Layout, m_Objects.placementBindings);
            for (CeilingLight* bulb : extras.Lights)
            {
                bulb->SetWarmBulb(true);
            }
        }

        // 出口の扉と、その先の出口。最後のループで扉を抜けると、階段を上って3面へ移る（Stage2Scene が階段の演出を始める）。
        // 自動撮影では、道順の時間がずれないよう、階段の演出をせず、これまでどおり調べてリザルト画面へ移る
        void BuildExit()
        {
            Door* door = m_Builder.Create<Door>("Stage2Door");
            Place(*door, "Stage2Door");
            door->ResetClosed(0);
            m_Objects.door = door;

            const bool capture = Tools::CaptureMode::IsActive();
            ExitTrigger* exit = m_Builder.Create<ExitTrigger>("Stage2Exit");
            Place(*exit, "Stage2Exit");
            exit->SetNextScene(capture ? SceneName::Result : SceneName::Stage3);
            exit->SetStairClimb(!capture);
            exit->SetInteractionEnabled(false);
            m_Objects.exit = exit;
        }

        // 隠れられるロッカー。本体は当たり判定のある箱（Wall）、扉と「隠れる」操作はLockerが担当している。
        // 時計・偽の扉・肖像画・端末・信号盤・電池と重ならない位置に、左右1つずつ置いている。
        // 本体（当たり判定のある箱）の位置と大きさ、扉の中心の位置と正面の向きは、配置のファイルの place 行から読んでいる
        void BuildLockers()
        {
            const auto createLocker = [this](const char* name, const char* bodyName)
            {
                // 本体の見た目は、上の通気口・脚・継ぎ目のあるロッカー（Mayaで作ったモデル。当たり判定は箱のまま）
                CreatePlacedWall(bodyName, LockerColor, true)->SetVisualModel("locker_body");
                Locker* locker = m_Builder.Create<Locker>(name);
                const PropPlacement& door = At(name);
                locker->Place(door.Position, door.Yaw);
                Bind(name, [locker](const PropPlacement& moved) { locker->Place(moved.Position, moved.Yaw); });
                return locker;
            };
            m_Objects.lockers = {
                createLocker("Stage2LockerLeft", "Stage2LockerLeftBody"),
                createLocker("Stage2LockerRight", "Stage2LockerRightBody") };
        }

    private:
        SceneLayoutBuilder m_Builder;
        Stage2Objects& m_Objects;
        const PropLayout& m_Layout;

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

        // 置き場所から別の形で置く物（ロッカー・裸電球など）の置き直し方を覚えている
        void Bind(const char* name, std::function<void(const PropPlacement&)> apply)
        {
            m_Objects.placementBindings.emplace_back(name, std::move(apply));
        }

        // Wallを、色と当たり判定の有無を指定して作る
        Wall* CreateWall(const char* name, const Vector3& position, const Vector3& scale, const Color& diffuse, bool collision)
        {
            Wall* wall = m_Builder.Create<Wall>(name);
            wall->SetPosition(position.x, position.y, position.z);
            wall->SetScale(scale.x, scale.y, scale.z);
            wall->SetAppearance(diffuse, NoEmission, PlainWallShininess);
            wall->SetCollisionEnabled(collision);
            return wall;
        }

        // 置き場所（位置と大きさ）を配置のファイルの place 行から読んで、Wall を作る
        Wall* CreatePlacedWall(const char* name, const Color& diffuse, bool collision)
        {
            const PropPlacement& placement = At(name);
            Wall* wall = CreateWall(name, placement.Position, placement.Size, diffuse, collision);
            Bind(name, [wall](const PropPlacement& moved)
            {
                wall->SetPosition(moved.Position.x, moved.Position.y, moved.Position.z);
                wall->SetScale(moved.Size.x, moved.Size.y, moved.Size.z);
                wall->SetVisualModel(wall->GetVisualModel());
            });
            return wall;
        }

        // 調べて操作する端末（配電盤と同じ FuseBox）を作り、案内の文と、最初から操作できるかを決めて置いている
        FuseBox* CreateTerminal(const char* name, const char* prompt, bool interactionAllowed)
        {
            FuseBox* terminal = m_Builder.Create<FuseBox>(name);
            terminal->SetManualControl(prompt);
            terminal->SetManualInteractionAllowed(interactionAllowed);
            Place(*terminal, name);
            return terminal;
        }

        // 最初は隠しておき、時間切れで破棄せずに隠す影を作っている
        ShadowMan* CreateHiddenShadow(const char* name)
        {
            ShadowMan* shadow = m_Builder.Create<ShadowMan>(name);
            shadow->SetDeactivateOnExpire(true);
            shadow->SetActive(false);
            return shadow;
        }
    };
}

namespace Stage2Layout
{
    // 2面の廊下（幅84・長さ300のまっすぐな廊下）を組み立て、進行で使うObjectのポインタを入れて返している。
    // 作る順番は前と同じにしている（描く順番と、置き直しの登録の順番が変わらないように）
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle)
    {
        Stage2Objects objects;
        // 廊下の壁・天井・縁取り・配管（prop 行）と、扉・照明・電池・端末などの置き場所（place 行）は、
        // テキストファイル（assets/layout/stage2_props.txt）から読んでいる（PropLayoutFile。1面と同じ書き方）
        const PropLayout propLayout = PropLayoutFile::Load(PropLayoutPath);
        if (!propLayout.IsValid())
        {
            utility::ReportFatalError("2面の配置を読めませんでした（" + std::to_string(propLayout.ErrorLine) +
                "行目）: " + propLayout.Error);
        }

        Stage2Builder builder(game, objects, propLayout);
        builder.BuildPlayerAndShadows();
        builder.BuildTerminals();
        builder.BuildLayoutPropsAndGround();
        builder.BuildTerminalMarkers();
        builder.BuildPortrait();
        builder.BuildClock(clockHourAngle, clockMinuteAngle);
        builder.BuildMirror();
        builder.BuildLoopMarks();
        builder.BuildDoorIndicator();
        builder.BuildFalseDoor();
        builder.BuildScratchesAndBattery();
        builder.BuildLights();
        builder.BuildExit();
        builder.BuildLockers();
        return objects;
    }
}
