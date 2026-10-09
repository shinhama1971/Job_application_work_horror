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
#include "TensionPulse.h"
#include "Stage1LightStalker.h"

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
    // 目的表示の文章を選ぶために、今の状態を集めている（StageSceneDraw.cpp）。
    Stage1ObjectiveInput MakeObjectiveInput() const;
    // 姿の見えない物音を鳴らしてよいか（台本の演出や監視映像と重ならないか）を判断している。
    bool IsAmbientSoundAllowed() const;
    // 姿の見えない物音を更新し、鳴らしている
    void UpdateAmbientSounds();
    // 懐中電灯で照らすと浮かぶ壁の文字。書き換わった文字を読んだら物音の演出を起こしている。
    void UpdateWallWritings(class Player& player);
    // 今の危険度（0〜1）を返している。一番近くに出ている影までの距離と、隠し部屋に閉じ込められているかから求めている。
    float ComputeThreatRate(const class Player& player) const;
    // 危険度に合わせて、自分の心拍音と呼吸音を鳴らしている。
    void UpdateTensionPulse(const class Player& player, float deltaTime);
    // 書類保管室の「照らすと止まる影」を進め、表示・足音・触れられたときの電池の減少を行っている。
    void UpdateArchiveStalker(class Player& player, float deltaTime);
    // 点が懐中電灯で照らされているか（光の円の中にあり、間に壁や扉がないか）を返している。
    bool IsLitByFlashlight(const class Player& player, const DirectX::SimpleMath::Vector3& target) const;

    // 配置で作った、進行で使うObject
    StageObjects m_Objects;

    // 1面の進行は「入口の演出 → ヒューズ探し → 電力の復旧 → 出口の演出」の順。
    // 各演出は段階と経過秒で管理し、Updateを止めずに少しずつ進めている。
    // ループ廊下の通過・入口の演出・左の倉庫の演出・ループ廊下を1周進める処理
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void UpdateStorageScare(class Player& player);
    void AdvanceCorridorLoop(class Player& player);

    // 照明が次々に明滅する演出、電力が戻る演出、出口へ送電する演出、ヒューズを拾った後の影、出口の前兆
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    void UpdateExitOmen(class Player& player);

    // InteractionSystemは視線の先の調べる対象、Hudは今の目的と操作のヒントを担当している。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    // 監視カメラの巡回（映像・報告・現地確認・捕まる）はこのクラスに任せ、Sceneは呼び出すだけにしている。
    StageSurveillanceController m_Surveillance;
    // 天井裏の足音・配管を叩く音・遠くの扉。いつどこで鳴らすかはこのクラスが決めている。
    Stage1AmbientSounds m_AmbientSounds;
    // このフレームに鳴らす物音（使い回している）
    std::vector<AmbientSoundCue> m_AmbientCues;
    // 壁の文字を読んだか、いつ書き換えるかを管理している（文字のObjectはStage1Layoutが配置している）。
    Stage1WallWritings m_WallWritings;
    // 暗証番号の扉。入力画面の操作と、番号の手がかりの配置を管理している。
    Stage1KeypadDoor m_KeypadDoor;
    // 暗証番号の扉の先の部屋で、閉じ込められて鍵を探すイベント。
    Stage1HiddenRoomEvent m_HiddenRoom;
    // 西棟（浸水した機械室）。鍵を拾って扉を開け、奥で3本目のヒューズを取る必須の区画。
    Stage1WestWing m_WestWing;
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
    int m_FuseWatcherState = 0;
    // 充電の知らせと使ったか、監視カメラの知らせ、ループを続けて進めないための待ち、ループの知らせ
    float m_ChargerNoticeTimer = 0.0f;
    bool m_ChargerHandled = false;
    float m_EvidenceNoticeTimer = 0.0f;
    float m_LoopCooldown = 0.0f;
    float m_LoopNoticeTimer = 0.0f;
    // 入口の演出（起きたか・経過秒・段階）、左の倉庫の演出（段階・経過秒・知らせ）
    bool m_EntranceEventTriggered = false;
    float m_EntranceEventTimer = -1.0f;
    int m_EntranceEventPhase = -1;
    int m_StorageScarePhase = 0;
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
};
