#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"

class StageScene : public Scene
{
private:
    void Init();
    void Uninit();
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void Draw(Camera* camera) override;
};
