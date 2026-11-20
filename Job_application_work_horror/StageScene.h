// ============================================================================
// ファイルの役割: 1面の進行（ヒューズ探し・ループ廊下・西棟・電力の復旧・出口まで）と、その演出を管理している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、進行の状態の管理、周りの物で物語を伝える演出
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "ScareLightSequence.h"
#include "StagePowerSequence.h"
#include "ExitOmenSequence.h"
#include "StageSurveillanceController.h"
#include "Stage1Layout.h"
#include "Stage1Objective.h"
#include "Stage1AmbientSounds.h"
#include "Stage1CeilingStalker.h"
#include "Stage1CorridorStretch.h"
#include "Stage1CorridorFlood.h"
#include "Stage1BreathingGroup.h"
#include "TensionPulse.h"
#include "Stage1LightStalker.h"
#include "RadioStatic.h"
#include "Stage1Transmissions.h"
#include "PropLayoutEditor.h"
#include "FootprintFollower.h"
#include "CeilingReflectionScare.h"

#include <array>
#include <cstddef>
#include <random>
#include <string_view>
#include <vector>

class Player;
class ShadowMan;
class FuseBox;
class Wall;
class Door;
class ExitTrigger;
class Item;
class CeilingLight;

// 1面のシーン。処理はStageScene.cpp（初期化・更新）、StageSceneEvents.cpp（各演出）、StageSceneDraw.cpp（描画と目的表示）に分けている。
class StageScene : public Scene
{
private:
    // 初期化と終了。配置はStage1Layoutに任せ、Uninitでは配置のときに記録した名前の一覧で破棄している。
    void Init();
    void Uninit();
    // 看板・ランプなどの小さな光源を設定している（タイルベースライティングで数を増やせるため）。
    void SetupPracticalLights();
    // 進む先の天井照明を1灯だけ、不規則にちかちかさせている
    void UpdateRouteLights();
    // 進行に合わせて、次に向かう場所の照明の組を選んでいる
    Stage1RouteLight SelectRouteLight() const;

    // --- 1フレームの進行の段階（StageScene.cpp。Update から順に呼んでいる） ---
    // 経過時間・ヒント・光の安全地帯・知らせの残り時間・配置の読み直し
    void UpdateTimersAndHints(class Player& player, float deltaTime);
    // ヒューズを拾った瞬間の知らせと、電力が戻った後の影の片付け
    void UpdateFusePickup();
    // 非常用充電器を使ったとき
    void UpdateEmergencyCharger(class Player& player);
    // 各演出と仕組みを順に更新する
    void UpdateStageEvents(class Player& player, float deltaTime);
    // 最後のドアの鍵と、2面への脱出の始まり
    void UpdateExitDoor(class Player& player);
    // 出口の表示・開始地点のドアの裏の表示・扉のランプ
    void UpdateExitSigns();
    // 電池の少なさ・走っているか・廊下の奥への近さに合わせた画面効果
    void UpdateScreenAtmosphere(const class Player& player);
    // 3本目のヒューズの2つの手に入れ方（西棟の奥か、暗証番号の扉の先の部屋）を切り替えている。
    // 3本目を探す段階で暗証番号の部屋のヒューズを出し、どちらか一方を取ったら、もう一方を消している
    void UpdateThirdFuseRoutes();
    // 3本目を探す段階か（ヒューズ2本・ループのドアを2回通った・電力はまだ）
    bool IsThirdFuseStage() const;
    // 小物の配置のファイルを読み直して置き直している（Debug版の F5）
    bool ReloadPropLayout();
    // 目的表示の文章を選ぶために、今の状態を集めている（StageSceneDraw.cpp）。
    Stage1ObjectiveInput MakeObjectiveInput() const;
    // Draw の中身：配置エディタ（Debug版だけ）・ポーズメニュー・目的地の矢印が指す所・始まりと捕まったときと階段の暗転
    void DrawLayoutEditor(Camera* camera);
    void DrawPauseMenu();
    DirectX::SimpleMath::Vector3 SelectGuideTarget(int fuseCount, bool exitPowerActivated);
    void DrawScreenFades();
    // 姿の見えない物音を鳴らしてよいか（台本の演出や監視映像と重ならないか）を判断している。
    bool IsAmbientSoundAllowed() const;
    // 電力が戻った後に一度だけ、天井裏の足音が照明を消しながら迫ってくる演出を進めている
    void UpdateCeilingStalker(const class Player& player, float deltaTime);
    // ループ廊下で振り返ると、廊下が伸びて見える（Stage1CorridorStretch の倍率をカメラへ渡している）
    void UpdateCorridorStretch(const class Player& player, float deltaTime);
    // 呼吸する壁（Stage1EventSettings.h の BreathingWallFaces）を探して格子にし、壁と一緒に動く扉・照明・小物を記録している（配置を読み直したら、もう一度探す）
    void SetupBreathingWall();
    void UpdateBreathingFixtures(bool restore = false);
    // 呼吸する壁：組ごとに、周回とヒューズの数で膨らむ量を決め、息を吐くたびに一番近い壁の中から息の音を鳴らす
    void UpdateBreathingWall(float deltaTime);
    // 西棟の水を抜いた後、ループ廊下の奥から赤い水が押し寄せる（Stage1CorridorFlood の合図で照明・音・床の水・プレイヤーを動かす）
    void UpdateCorridorFlood(class Player& player, float deltaTime);
    // 姿の見えない物音を更新し、鳴らしている
    void UpdateAmbientSounds();
    // 懐中電灯で照らすと浮かぶ壁の文字。書き換わった文字を読んだら物音の演出を起こしている。
    void UpdateWallWritings(class Player& player);
    void UpdateDarkCorridorWriting(const class Player& player);
    // ボイスレコーダーを調べたら録音の字幕を流し、1面の途中で届く無線（嘘の指示）を進めている
    void UpdateTransmissions(const class Player& player, float deltaTime, bool archiveStalkerLit);
    // 誰かの濡れた足跡：西棟の水から初めて乾いた床へ出ると、同じ水から裸足の足跡が一歩ずつ後をついて来る（1回だけ）
    void UpdateFootprintFollower(class Player& player);
    // 西棟の水面にだけ映る、天井に逆さまに張り付いた人影（見上げると何もいない。1回だけ）
    void UpdateCeilingReflection(const class Player& player, float deltaTime);
    // 今の危険度（0〜1）を返している。一番近くに出ている影までの距離と、隠し部屋に閉じ込められているかから求めている。
    float ComputeThreatRate(const class Player& player) const;
    // 危険度に合わせて、自分の心拍音と呼吸音を鳴らしている。
    void UpdateTensionPulse(const class Player& player, float deltaTime);
    // 出ている影のうち近いものの画面の位置・大きさ・近さを、画面効果（影のまわりの空間のゆがみ）へ渡している
    void UpdatePresenceWarp(const class Player& player);
    // 書類保管室の「照らすと止まる影」を進め、表示・足音・触れられたときの電池の減少を行っている。
    bool UpdateArchiveStalker(class Player& player, float deltaTime);
    // 点が懐中電灯で照らされているか（光の円の中にあり、間に壁や扉がないか）を返している。
    bool IsLitByFlashlight(const class Player& player, const DirectX::SimpleMath::Vector3& target) const;

    // 配置で作った、進行で使うObject
    StageObjects m_Objects;
    // 今ちかちかさせている照明の組（進み具合）と、その組の中で何番目の灯か
    std::size_t m_ActiveRoute = static_cast<std::size_t>(Stage1RouteLight::Count);
    std::size_t m_RouteFixtureIndex = 0;

    // 1面の進行は「入口の演出 → ヒューズ探し → 電力の復旧 → 出口の演出」の順。
    // 各演出は段階と経過秒で管理し、Updateを止めずに少しずつ進めている。
    // ループ廊下の通過・入口の演出・左の倉庫の演出・ループ廊下を1周進める処理
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void UpdateStorageScare(class Player& player);
    void AdvanceCorridorLoop(class Player& player);
    // AdvanceCorridorLoop の中身：壁の文字と周回の印・周回ごとの廊下の変化
    void UpdateCorridorLoopMarks(int loopPhase);
    void ApplyCorridorLoopChanges(int loopPhase);
    // 突き当たりの最後のドアが開いていて、プレイヤーがその向こう側へ出たか
    bool IsThroughEndDoor(const DirectX::SimpleMath::Vector3& position) const;

    // 照明が次々に明滅する演出、電力が戻る演出、出口へ送電する演出、ヒューズを拾った後の影、出口の前兆
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    // ヒューズを拾った瞬間の演出：近くの照明が消え、懐中電灯がちらつき、すぐ後ろで足音がする（本数が多いほど強い）
    void StartFuseScare(int fuseCount);
    void UpdateFuseScare(float deltaTime);
    void EndFuseScare();
    void UpdateExitOmen(class Player& player);

    // InteractionSystemは視線の先の調べる対象、Hudは今の目的と操作のヒントを担当している。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    // 監視カメラの巡回（映像・報告・現地確認・捕まる）はこのクラスに任せ、Sceneは呼び出すだけにしている。
    StageSurveillanceController m_Surveillance;
    // 天井裏の足音・配管を叩く音・遠くの扉。いつどこで鳴らすかはこのクラスが決めている。
    Stage1AmbientSounds m_AmbientSounds;
    // 天井裏の足音の演出、電力が戻ってからの秒数（演出を始めてよい時間になったかを見る）、このフレームの合図
    Stage1CeilingStalker m_CeilingStalker;
    Stage1CorridorStretch m_CorridorStretch;
    // 呼吸する壁（壁・膨らませる面の向き・組）と、呼吸の時間（秒）・組ごとの今の膨らむ量・前のフレームの呼吸の何回目か（息の音を1回ずつ鳴らすため）
    struct BreathingWall
    {
        class Wall* Surface;
        DirectX::SimpleMath::Vector3 Normal;
        Stage1BreathingGroup Group;
    };
    std::vector<BreathingWall> m_BreathingWalls;
    struct BreathingFixture
    {
        Object* Fixture;
        Wall* Surface;
        DirectX::SimpleMath::Vector3 BasePosition;
        DirectX::SimpleMath::Vector3 Normal;
        DirectX::SimpleMath::Vector2 Sample;
    };
    std::vector<BreathingFixture> m_BreathingFixtures;
    float m_BreathingWallTime = 0.0f;
    std::array<float, Stage1BreathingGroupCount> m_BreathingGroupAmplitudes{};
    // 組ごとの、今の膨らむ量の目標（周回とヒューズの数から決める）
    float GetBreathingGroupTarget(Stage1BreathingGroup group) const;
    int m_BreathingWallCycle = 0;
    // ヒューズを拾った瞬間の演出：経過秒（-1 なら起きていない）、何本目か（0 から）、鳴らした足音の数、消した照明、足音を鳴らす所
    float m_FuseScareTimer = -1.0f;
    int m_FuseScareLevel = 0;
    std::size_t m_FuseScareStepsPlayed = 0;
    std::vector<class CeilingLight*> m_FuseScareLights;
    DirectX::SimpleMath::Vector3 m_FuseScareBehind = DirectX::SimpleMath::Vector3::Zero;
    float m_CeilingStalkerWait = 0.0f;
    std::vector<Stage1CeilingStalker::Cue> m_CeilingStalkerCues;
    // ループ廊下に押し寄せる水の演出、このフレームの合図、水で消した照明（戻すため）、床の浸水範囲をこの演出が使っているか
    Stage1CorridorFlood m_CorridorFlood;
    std::vector<Stage1CorridorFlood::Cue> m_CorridorFloodCues;
    std::vector<class CeilingLight*> m_CorridorFloodLights;
    bool m_CorridorFloodOwnsWater = false;
    // このフレームに鳴らす物音（使い回している）
    std::vector<AmbientSoundCue> m_AmbientCues;
    // 壁の文字を読んだか、いつ書き換えるかを管理している（文字のObjectはStage1Layoutが配置している）。
    Stage1WallWritings m_WallWritings;
    // 暗証番号の扉。入力画面の操作と、番号の手がかりの配置を管理している。
    Stage1KeypadDoor m_KeypadDoor;
    // 暗証番号の扉の先の部屋で、閉じ込められて鍵を探すイベント。
    Stage1HiddenRoomEvent m_HiddenRoom;
    // 西棟（浸水した機械室）。鍵を拾って扉を開け、排水バルブで水を抜き、奥で3本目のヒューズを取る必須の区画。
    Stage1WestWing m_WestWing;
    // 西棟の水面にだけ映る、天井の人影の演出
    CeilingReflectionScare m_CeilingScare;
    // 光の安全地帯（暖かい灯りの下）。落ち着きの度合いと、休んでいる間の電池の回復を管理している。
    Stage1SafeLights m_SafeLights;
    // 小物の配置のファイルを読み直したときの知らせと、その残り秒数（Debug版の F5）
    std::string m_LayoutNotice;
    float m_LayoutNoticeTimer = 0.0f;
    // 配置のファイルが書き換わったかを見張っている（Debug版で、書き換わったら自動で読み直す）
    PropLayoutFile::FileChangeWatcher m_LayoutWatcher;
    // Debug版のデバッグ画面（F1）に出す、小物の配置エディタ（選んで動かし、配置のファイルへ書き戻す）
    PropLayoutEditor m_PropLayoutEditor{ Stage1Layout::PropLayoutPath };
    // 西棟の水から後をついて来る誰かの濡れた足跡と、もう出したか（1回のプレイで1回だけ）
    FootprintFollower m_FootprintFollower;
    bool m_FootprintFollowerUsed = false;
    // 影が近いと鳴る携帯ラジオの雑音
    RadioStatic m_RadioStatic;
    // 画面の下に流す録音と無線の字幕、無線の嘘の進み具合
    Stage1Transmissions m_Transmissions;
    // 西棟で鳴らす物音（使い回している）
    std::vector<AmbientSoundCue> m_WestWingCues;
    // 書類保管室の「照らすと止まる影」。動きのルールはこのクラス、影の表示・音・電池はSceneが行っている。
    Stage1LightStalker m_ArchiveStalker;
    // 照らされているかの判定と、影を棚や壁から押し戻すのに使う壁と扉（Sceneの初期化時に一度だけ集めている）
    std::vector<class Wall*> m_StalkerWalls;
    std::vector<class Door*> m_StalkerDoors;
    // 影についての知らせの文章と、その残り秒数
    std::string_view m_ArchiveStalkerNotice;
    float m_ArchiveStalkerNoticeTimer = 0.0f;
    // 照明の演出・電力の演出・出口の前兆の、時間と段階
    ScareLightSequence m_ScareLightSequence;
    StagePowerSequence m_PowerSequence;
    ExitOmenSequence m_ExitOmenSequence;
    // 自分の心拍と呼吸をいつ鳴らすか。
    TensionPulse m_TensionPulse;

    // 0以上のtimerは演出の途中、-1は実行していないか終わったことを表している。
    // ループ廊下を通った回数、前フレームのヒューズの数、ヒューズの知らせ、影の知らせと状態
    int m_CorridorLoopCount = 0;
    int m_LastFuseCount = 0;
    float m_FuseNoticeTimer = 0.0f;
    float m_FuseWatcherNoticeTimer = 0.0f;
    FuseWatcherState m_FuseWatcherState = FuseWatcherState::None;
    // 充電の知らせと使ったか、監視カメラの知らせ、ループを続けて進めないための待ち、ループの知らせ
    float m_ChargerNoticeTimer = 0.0f;
    bool m_ChargerHandled = false;
    float m_EvidenceNoticeTimer = 0.0f;
    float m_LoopCooldown = 0.0f;
    float m_LoopNoticeTimer = 0.0f;
    // 暗い廊下の文字は、目を止めて読んだときだけ一度反応する。
    float m_DarkWritingReadTime = 0.0f;
    bool m_DarkWritingTriggered = false;
    // 暗証番号の部屋のヒューズ：空の箱の知らせを出す残り秒数、箱に近づいている間か（近づいた瞬間だけ知らせる）、
    // 3本目の段階になったときに、先に開けていた部屋の箱へヒューズが戻ったか、前のフレームで3本目の段階だったか
    float m_EmptyFuseCaseNoticeTimer = 0.0f;
    bool m_NearEmptyFuseCase = false;
    bool m_KeypadFuseReturned = false;
    bool m_WasThirdFuseStage = false;
    // 2周目以降、開始地点の奥のドアの向こうから叩く音：次に鳴らすまでの秒数、続けて叩く残りの回数、これまでに鳴らした回数
    float m_StartDoorKnockTimer = 0.0f;
    int m_StartDoorKnocksLeft = 0;
    int m_StartDoorKnockEvents = 0;
    // 入口の演出（起きたか・経過秒・段階）、左の倉庫の演出（段階・経過秒・知らせ）
    bool m_EntranceEventTriggered = false;
    float m_EntranceEventTimer = -1.0f;
    // 入口の演出の段階（始まっていない・始まった・前の照明を明滅させた・終わった）
    enum class EntranceEventPhase
    {
        NotStarted,
        Started,
        AheadFlickered,
        Finished
    };
    EntranceEventPhase m_EntranceEventPhase = EntranceEventPhase::NotStarted;
    StorageScarePhase m_StorageScarePhase = StorageScarePhase::Waiting;
    float m_StorageScareTimer = 0.0f;
    float m_StorageScareNoticeTimer = 0.0f;
    // 見た目の演出に使う経過時間、進行が止まっている時間（ヒント用）
    float m_StageVisualTimer = 0.0f;
    float m_ProgressHintTimer = 0.0f;
public:
    // コンストラクタでInitを、デストラクタでUninitを呼んでいる
    StageScene();
    ~StageScene();

    // 1フレーム分の進行を更新している
    void Update() override;
    // 本描画の前に、監視カメラの映像を描いている
    void RenderOffscreen() override;
    // HUD・目的表示・監視映像などを描いている
    void Draw(Camera* camera) override;
    // 自動撮影から、最後のドアを開ける操作を受け付けている
    void RequestDebugAction(SceneDebugAction action) override;
};
