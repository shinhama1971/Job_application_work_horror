// HorrorStageScene.h
#pragma once
#include "Scene.h"
#include "Object.h"

class StageScene : public Scene
{
private:
    std::vector<Object*> m_MySceneObjects;

    void Init();
    void Uninit();

public:
    StageScene();
    ~StageScene();

    void Update() override;
};