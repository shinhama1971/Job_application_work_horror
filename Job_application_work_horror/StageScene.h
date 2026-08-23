#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"

class StageScene : public Scene
{
private:
    void Init();
    void Uninit();
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void AdvanceCorridorLoop(class Player& player);
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    void UpdateExitOmen(class Player& player);
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
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
    float m_ScareLightTimer = -1.0f;
    int m_ScareLightPhase = -1;
    float m_ScareMessageTimer = 0.0f;
    bool m_WasPowerRestored = false;
    float m_PowerRestoreTimer = -1.0f;
    int m_PowerRestorePhase = -1;
    float m_ExitPowerEventTimer = -1.0f;
    int m_ExitPowerEventPhase = -1;
    bool m_ExitPowerSequenceComplete = false;
    float m_StageVisualTimer = 0.0f;
    bool m_ExitOmenTriggered = false;
    float m_ExitOmenTimer = 0.0f;
    int m_ExitOmenPhase = -1;
    float m_ProgressHintTimer = 0.0f;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void Draw(Camera* camera) override;
};
