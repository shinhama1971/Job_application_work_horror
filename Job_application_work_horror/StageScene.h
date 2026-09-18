// ============================================================================
// ファイルの役割: 1面のステージ配置、ヒューズ探索、電力復旧、出口までの進行を管理します
// 主な技術: シーン構成、オブジェクト配置、進行状態、環境ストーリーテリング
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "ScareLightSequence.h"
#include "StagePowerSequence.h"
#include "ExitOmenSequence.h"

class StageScene : public Scene
{
private:
    // 初期配置と破棄。Initで生成した名前付きObjectはUninitで対応して破棄します。
    void Init();
    void Uninit();

    // 1面の進行は「入口演出 → ヒューズ探索 → 電力復旧 → 出口演出」の順です。
    // 各演出はphaseとtimerで管理し、Updateを止めずに段階的に進めます。
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void AdvanceCorridorLoop(class Player& player);
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    void UpdateExitOmen(class Player& player);

    // InteractionSystemは視線先、Hudは現在目的と操作ヒントを担当します。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
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
    float m_StageVisualTimer = 0.0f;
    float m_ProgressHintTimer = 0.0f;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void Draw(Camera* camera) override;
};
