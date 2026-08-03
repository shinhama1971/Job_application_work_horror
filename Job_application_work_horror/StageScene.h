#pragma once

#include "Scene.h"
#include "InteractionSystem.h"

class StageScene : public Scene
{
private:
    void Init();
    void Uninit();
    InteractionSystem m_InteractionSystem;
public:
    StageScene();
    ~StageScene();

    void Update() override;
};
