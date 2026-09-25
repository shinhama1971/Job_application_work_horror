// ============================================================================
// ファイルの役割: 1面のステージ配置、ヒューズ探索、電力復旧、出口までの進行を管理します
// 主な技術: シーン構成、オブジェクト配置、進行状態、環境ストーリーテリング
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "ScareLightSequence.h"
#include "StagePowerSequence.h"
#include "ExitOmenSequence.h"
#include "RenderTexture.h"
#include "SurveillancePatrol.h"
#include "CaughtSequence.h"
#include "StageSurveillanceCameras.h"

#include <array>
#include <cstddef>
#include <random>

class Player;
class ShadowMan;
class FuseBox;
class Wall;
class Door;
class ExitTrigger;
class Item;
class CeilingLight;

// Init直後に名前で一度だけ取得し、以後は毎フレームの名前検索をせずに使うObject群です。
// 実体はObjectManagerが所有し、どれもSceneの終了まで破棄されないため非所有ポインタで保持します。
struct StageObjects
{
    static constexpr int CeilingLightCount = 8;

    Player* player = nullptr;
    ShadowMan* fuseWatcher = nullptr;
    ShadowMan* storageShadow = nullptr;
    ShadowMan* evidenceShadow = nullptr;
    ShadowMan* exitOmen = nullptr;
    FuseBox* emergencyCharger = nullptr;
    FuseBox* evidenceTerminal = nullptr;
    FuseBox* exitPowerPanel = nullptr;
    Wall* evidenceMarker = nullptr;
    Wall* exitSign = nullptr;
    Wall* doorIndicator = nullptr;
    std::array<Wall*, 3> loopMarkers{};
    Door* loopDoor = nullptr;
    Door* exitDoor = nullptr;
    ExitTrigger* exitTrigger = nullptr;
    Item* secondFuse = nullptr;
    Item* thirdFuse = nullptr;
    std::array<CeilingLight*, CeilingLightCount> ceilingLights{};
    std::array<Door*, StageSealedDoorCount> sealedDoors{};

    // 配置名"CeilingLight1"〜"CeilingLight8"と同じ1始まりの番号で照明を取得します。
    CeilingLight* CeilingLightAt(int number) const
    {
        return ceilingLights[static_cast<std::size_t>(number - 1)];
    }
};

class StageScene : public Scene
{
private:
    // 初期配置と破棄。Initで生成した名前付きObjectはUninitで対応して破棄します。
    void Init();
    void Uninit();
    void CacheObjects();
    // 看板・表示灯などの小さな光源を設定します（タイルベースライティングで数を増やせるため）。
    void SetupPracticalLights();

    StageObjects m_Objects;

    // 1面の進行は「入口演出 → ヒューズ探索 → 電力復旧 → 出口演出」の順です。
    // 各演出はphaseとtimerで管理し、Updateを止めずに段階的に進めます。
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void UpdateStorageScare(class Player& player);
    void AdvanceCorridorLoop(class Player& player);

    // 監視カメラ巡回。進行判定はSurveillancePatrol、見た目の異常と入力・演出はSceneが担当します。
    void UpdateSurveillancePatrol(class Player& player, float deltaTime);
    void UpdatePatrolViewing(class Player& player);
    void UpdatePatrolDispatch(class Player& player, float deltaTime);
    SurveillancePatrol::Anomaly ChoosePatrolAnomaly();
    void SetPatrolAnomalyVisible(const SurveillancePatrol::Anomaly& anomaly, bool visible);
    bool IsIlluminatingPatrolAnomaly(const class Player& player) const;
    void EndPatrolViewing(class Player& player);
    void StartPatrolCaught(class Player& player);
    void UpdatePatrolCaught(class Player& player, float deltaTime);
    void CompletePatrol(class Player& player);
    void ShowPatrolNotice(const char* text, float seconds);
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    void UpdateExitOmen(class Player& player);

    // InteractionSystemは視線先、Hudは現在目的と操作ヒントを担当します。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    Graphics::RenderTexture m_SurveillanceFeed;
    std::array<Graphics::RenderTexture, StageSurveillanceCameraCount>
        m_SurveillanceReferences;
    Shader m_SurveillanceShader;
    ScareLightSequence m_ScareLightSequence;
    StagePowerSequence m_PowerSequence;
    ExitOmenSequence m_ExitOmenSequence;

    // 0以上のtimerは演出実行中、-1は未実行または終了を表します。
    int m_CorridorLoopCount = 0;
    int m_LastFuseCount = 0;
    float m_FuseNoticeTimer = 0.0f;
    float m_FuseWatcherNoticeTimer = 0.0f;
    int m_FuseWatcherState = 0;
    float m_ChargerNoticeTimer = 0.0f;
    bool m_ChargerHandled = false;
    float m_EvidenceNoticeTimer = 0.0f;
    bool m_EvidenceHandled = false;
    float m_LoopCooldown = 0.0f;
    float m_LoopNoticeTimer = 0.0f;
    bool m_EntranceEventTriggered = false;
    float m_EntranceEventTimer = -1.0f;
    int m_EntranceEventPhase = -1;
    int m_StorageScarePhase = 0;
    float m_StorageScareTimer = 0.0f;
    float m_StorageScareNoticeTimer = 0.0f;
    SurveillancePatrol m_Patrol;
    CaughtSequence m_PatrolCaught;
    std::mt19937 m_PatrolRandom{ std::random_device{}() };
    // 映像を開いてからの経過秒。開いた直後の誤入力を防ぐために使います。
    float m_PatrolViewTimer = 0.0f;
    bool m_PatrolZoomed = false;
    bool m_PatrolShowReference = false;
    bool m_PatrolReferenceCapturePending = false;
    int m_PatrolReferenceCaptureIndex = 0;
    float m_PatrolWrongTimer = 0.0f;
    float m_PatrolWarningCooldown = 0.0f;
    float m_PatrolNoticeTimer = 0.0f;
    const char* m_PatrolNoticeText = "";
    float m_StageVisualTimer = 0.0f;
    float m_ProgressHintTimer = 0.0f;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void RenderOffscreen() override;
    void Draw(Camera* camera) override;
};
