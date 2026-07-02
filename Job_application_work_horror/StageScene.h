#pragma once

#include "Scene.h"

class StageScene : public Scene
{
private:
    void Init();
    void Uninit();

public:
    StageScene();
    ~StageScene();

    void Update() override;
};