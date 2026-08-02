#pragma once

#include "Scene.h"
#include <memory>
#include "MovieTrigger.h"

class StageScene : public Scene
{
private:
    void Init();
    void Uninit();
    std::unique_ptr<MovieTrigger> m_movieTrigger;
public:
    StageScene();
    ~StageScene();

    void Update() override;
};