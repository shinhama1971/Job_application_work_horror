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
    void AdvanceCorridorLoop(class Player& player);
    void StartScareLightSequence();
    void UpdateScareLightSequence();
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    int m_CorridorLoopCount = 0;
    float m_LoopCooldown = 0.0f;
    float m_LoopNoticeTimer = 0.0f;
    float m_ScareLightTimer = -1.0f;
    int m_ScareLightPhase = -1;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void Draw(Camera* camera) override;
};
